"""First 8 logits after a prompt, from PyTorch with the exact weights stored in model.bin
(read back from the blob), with float or int8-rounded activations like the C engine.
usage: python check_logits.py ckpt.pt model.bin "prompt"   (paths relative to ../data)"""
import struct, sys
from pathlib import Path
from types import SimpleNamespace

import numpy as np
import sentencepiece as spm
import torch

sys.path.insert(0, str(Path(__file__).parent))
from model import Model

DATA = Path(__file__).resolve().parent.parent / "data"
ck = torch.load(DATA / sys.argv[1], map_location="cpu")
a = ck["args"]
blob = (DATA / sys.argv[2]).read_bytes()
sp = spm.SentencePieceProcessor(model_file=str(DATA / "tok.model"))
h = struct.unpack_from("<10i", blob, 4)
ver, D, Hd, L, nh, kvh, V, seq, G, _ = h
off = 44
EG, BITS = (struct.unpack_from("<2i", blob, off) if ver == 2 else (G, 8))
off += 8 if ver == 2 else 0
for _ in range(V):
    off += 5 + blob[off + 4]
off = (off + 3) & ~3


def a4():
    global off
    off = (off + 3) & ~3


def take_f(n):
    global off
    v = np.frombuffer(blob, "<f4", n, off).copy(); off += 4 * n
    return torch.from_numpy(v)


def take_q(shape, group, bits):
    global off
    n = shape[0] * shape[1]
    if bits == 8:
        sc = np.frombuffer(blob, "<f4", n // group, off).astype(np.float32); off += 4 * (n // group)
        q = np.frombuffer(blob, np.int8, n, off).astype(np.float32); off += n
    else:
        nb = n // group
        blk = np.frombuffer(blob, np.uint8, nb * (2 + group // 2), off).reshape(nb, -1); off += blk.size
        sc = blk[:, :2].copy().view("<f2").reshape(-1).astype(np.float32)
        p = blk[:, 2:].reshape(-1, 4)
        lo, hi = (p & 15).astype(np.float32) - 8, (p >> 4).astype(np.float32) - 8
        q = np.concatenate([lo, hi], 1).reshape(-1)
    a4()
    return torch.from_numpy((q.reshape(-1, group) * sc[:, None]).reshape(shape))


hd = D // nh
sd = {"emb.weight": take_q((V, D), EG, 8)}
for l in range(L):
    p = f"blocks.{l}."
    sd[p + "n1.w"] = take_f(D)
    sd[p + "wq.weight"] = take_q((D, D), G, BITS)
    sd[p + "wk.weight"] = take_q((kvh * hd, D), G, BITS)
    sd[p + "wv.weight"] = take_q((kvh * hd, D), G, BITS)
    sd[p + "wo.weight"] = take_q((D, D), G, BITS)
    sd[p + "n2.w"] = take_f(D)
    sd[p + "w1.weight"] = take_q((Hd, D), G, BITS)
    sd[p + "w3.weight"] = take_q((Hd, D), G, BITS)
    sd[p + "w2.weight"] = take_q((D, Hd), G, BITS)
sd["norm.w"] = take_f(D)
print("blob read to byte", off, "of", len(blob))

model = Model(SimpleNamespace(**a), V)
model.load_state_dict(sd)
model.eval()
ids = [sp.piece_to_id("<u>")] + sp.encode(sys.argv[3]) + [sp.piece_to_id("<b>")]
with torch.no_grad():
    lg = model(torch.tensor([ids]))[0, -1]
print("pytorch (blob weights):", " ".join(f"{v:.4f}" for v in lg[:8].tolist()))
# also compare with the checkpoint's own weights after export rounding
