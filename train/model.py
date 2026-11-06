"""Tiny Llama-style model shared by train.py and export.py."""
import math

import torch
import torch.nn as nn
import torch.nn.functional as F


class RMSNorm(nn.Module):
    def __init__(self, d):
        super().__init__()
        self.w = nn.Parameter(torch.ones(d))

    def forward(self, x):
        return x * torch.rsqrt(x.pow(2).mean(-1, keepdim=True) + 1e-5) * self.w


def rope(x, cos, sin):  # x: B,H,T,hd ; rotate pairs (2i, 2i+1), same as the C code
    x1, x2 = x[..., 0::2], x[..., 1::2]
    return torch.stack((x1 * cos - x2 * sin, x1 * sin + x2 * cos), -1).flatten(-2)


QMAX = {4: 7, 8: 127}


def fake_quant(w, bits, group):
    """Round w the way export.py stores it (symmetric, one scale per `group` values along each row,
    scale kept as float16 for 4-bit), but let gradients pass straight through (STE)."""
    qm = QMAX[bits]
    g = w.reshape(-1, group)
    scale = g.abs().amax(1, keepdim=True).clamp_min(1e-8) / qm
    if bits == 4:
        scale = scale.half().float()
    q = (g / scale).round().clamp(-qm, qm) * scale
    return w + (q.reshape(w.shape) - w).detach()


class QLinear(nn.Linear):
    """nn.Linear that trains against its quantized weights when .qbits is set (QAT)."""
    qbits, qgroup = 0, 32

    def forward(self, x):
        w = fake_quant(self.weight, self.qbits, self.qgroup) if self.qbits else self.weight
        return F.linear(x, w)


class Block(nn.Module):
    def __init__(self, a):
        super().__init__()
        self.h, self.kvh, self.hd = a.heads, a.kv_heads, a.dim // a.heads
        self.wq = QLinear(a.dim, a.heads * self.hd, bias=False)
        self.wk = QLinear(a.dim, a.kv_heads * self.hd, bias=False)
        self.wv = QLinear(a.dim, a.kv_heads * self.hd, bias=False)
        self.wo = QLinear(a.heads * self.hd, a.dim, bias=False)
        self.w1 = QLinear(a.dim, a.hidden, bias=False)
        self.w3 = QLinear(a.dim, a.hidden, bias=False)
        self.w2 = QLinear(a.hidden, a.dim, bias=False)
        self.n1, self.n2 = RMSNorm(a.dim), RMSNorm(a.dim)

    def forward(self, x, cos, sin):
        B, T, _ = x.shape
        h = self.n1(x)
        q = self.wq(h).view(B, T, self.h, self.hd).transpose(1, 2)
        k = self.wk(h).view(B, T, self.kvh, self.hd).transpose(1, 2)
        v = self.wv(h).view(B, T, self.kvh, self.hd).transpose(1, 2)
        q, k = rope(q, cos, sin), rope(k, cos, sin)
        rep = self.h // self.kvh
        k, v = k.repeat_interleave(rep, 1), v.repeat_interleave(rep, 1)
        y = F.scaled_dot_product_attention(q, k, v, is_causal=True)
        x = x + self.wo(y.transpose(1, 2).reshape(B, T, -1))
        h = self.n2(x)
        return x + self.w2(F.silu(self.w1(h)) * self.w3(h))


class Model(nn.Module):
    def __init__(self, a, vocab):
        super().__init__()
        self.a = a
        self.emb = nn.Embedding(vocab, a.dim)
        self.blocks = nn.ModuleList(Block(a) for _ in range(a.layers))
        self.norm = RMSNorm(a.dim)
        hd = a.dim // a.heads
        freqs = 1.0 / (10000 ** (torch.arange(0, hd, 2).float() / hd))
        ang = torch.outer(torch.arange(a.seq).float(), freqs)
        self.register_buffer("cos", ang.cos(), persistent=False)
        self.register_buffer("sin", ang.sin(), persistent=False)
        self.apply(self._init)
        for n, prm in self.named_parameters():
            if n.endswith("wo.weight") or n.endswith("w2.weight"):
                nn.init.normal_(prm, 0, 0.02 / math.sqrt(2 * a.layers))

    def set_qat(self, bits, group=32):
        """Train the block weights against their 4-bit (or 8-bit) rounding from now on."""
        for m in self.modules():
            if isinstance(m, QLinear):
                m.qbits, m.qgroup = bits, group

    @staticmethod
    def _init(m):
        if isinstance(m, (nn.Linear, nn.Embedding)):  # QLinear is an nn.Linear too
            nn.init.normal_(m.weight, 0, 0.02)

    def forward(self, idx):
        T = idx.shape[1]
        x = self.emb(idx)
        for b in self.blocks:
            x = b(x, self.cos[:T], self.sin[:T])
        return self.norm(x) @ self.emb.weight.T  # tied output layer
