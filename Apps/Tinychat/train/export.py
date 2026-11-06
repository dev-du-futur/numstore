"""Pack the trained model + tokenizer into one binary blob for the calculator (../data/model.bin).

Layout (little-endian, every section 4-byte aligned):
  header  : 'NWAI' + 10 x int32 (version, dim, hidden, layers, heads, kv_heads, vocab, seq, group, max_piece_len)
            version 2 adds 2 x int32 (emb_group, bits): block weights in `bits` (4) as blocks of
            [float16 scale][group/2 bytes] (18 bytes for 32 values, read strictly in order: the
            calculator's flash is slow when reads jump between a scale table and the values),
            two values per byte stored +8; in each 4-byte word the low nibbles hold values 0-3 and
            the high nibbles values 4-7 of a run of 8.
            The embedding stays int8 (groups of emb_group, float32 scales).
  vocab   : per token: float32 score, uint8 len, bytes   (then pad to 4)
  weights : int8 matrices in groups of `group` values, each group with a float32 scale
            stored as: float32 scales[n/group], int8 q[n]
            order: emb, per layer [n1 f32, wq, wk, wv, wo, n2 f32, w1, w3, w2], final norm f32
Also writes ../data/ref.txt: a greedy reply from the quantized model, used to check the C code.
"""
import struct, sys
from pathlib import Path

import numpy as np
import sentencepiece as spm
import torch

DATA = Path(__file__).resolve().parent.parent / "data"
ck = torch.load(DATA / (sys.argv[1] if len(sys.argv) > 1 else "ckpt.pt"), map_location="cpu")
a, sd = ck["args"], ck["model"]
BITS = a.get("qbits") or 8
EG = 64                                   # embedding group (int8)
G = a.get("qgroup", 32) if BITS == 4 else 64
sp = spm.SentencePieceProcessor(model_file=str(DATA / "tok.model"))
V = sp.get_piece_size()

out = bytearray()
def pad():
    while len(out) % 4:
        out.append(0)

def piece_bytes(i):
    p = sp.id_to_piece(i)
    if sp.is_byte(i):
        return bytes([int(p[3:5], 16)])
    if sp.is_control(i) or sp.is_unknown(i):
        return b""
    return p.replace("▁", " ").encode("utf8")

pieces = [piece_bytes(i) for i in range(V)]
out += b"NWAI" + struct.pack("<10i", 1 if BITS == 8 else 2, a["dim"], a["hidden"], a["layers"], a["heads"],
                             a["kv_heads"], V, a["seq"], G, max(map(len, pieces)))
if BITS == 4:
    out += struct.pack("<2i", EG, 4)
for i in range(V):
    score = sp.get_score(i) if not sp.is_byte(i) else -1e6
    out += struct.pack("<fB", score, len(pieces[i])) + pieces[i]
pad()

deq = {}
def q8(name, G=G):
    w = sd[name].float().numpy()
    flat = w.reshape(-1, G)
    scale = np.abs(flat).max(1) / 127.0
    scale[scale == 0] = 1e-8
    q = np.clip(np.round(flat / scale[:, None]), -127, 127).astype(np.int8)
    out.extend(scale.astype("<f4").tobytes())
    out.extend(q.tobytes())
    pad()
    deq[name] = torch.from_numpy((q.astype(np.float32) * scale[:, None]).reshape(w.shape))

def q4(name):
    """same rounding as model.fake_quant: scale = max|w|/7 as float16, values -7..7"""
    w = sd[name].float().numpy()
    flat = w.reshape(-1, G)
    scale = (np.maximum(np.abs(flat).max(1), 1e-8) / 7.0).astype(np.float16)
    sc = scale.astype(np.float32)
    q = np.clip(np.round(flat / sc[:, None]), -7, 7).astype(np.int32)
    u = (q + 8).astype(np.uint8).reshape(-1, 8)          # runs of 8
    packed = (u[:, :4] | (u[:, 4:] << 4)).astype(np.uint8)  # byte k: value k low, value k+4 high
    blocks = np.concatenate([scale.astype("<f2").view(np.uint8).reshape(-1, 2),
                             packed.reshape(len(scale), -1)], 1)   # [scale][values] per group
    out.extend(blocks.tobytes())
    pad()
    deq[name] = torch.from_numpy((q.astype(np.float32) * sc[:, None]).reshape(w.shape))

qw = q4 if BITS == 4 else q8

def f32(name):
    out.extend(sd[name].float().numpy().astype("<f4").tobytes())

q8("emb.weight", EG)
for l in range(a["layers"]):
    p = f"blocks.{l}."
    f32(p + "n1.w")
    for n in ["wq", "wk", "wv", "wo"]:
        qw(p + n + ".weight")
    f32(p + "n2.w")
    for n in ["w1", "w3", "w2"]:
        qw(p + n + ".weight")
f32("norm.w")
(DATA / "model.bin").write_bytes(out)
print(f"model.bin: {len(out)/1024:.0f} KB ({BITS}-bit blocks)")

# Greedy reference reply from the quantized weights, to compare against the C code.
from types import SimpleNamespace
sys.path.insert(0, str(Path(__file__).parent))
from model import Model
model = Model(SimpleNamespace(**a), V)
sd2 = dict(sd); sd2.update(deq)
model.load_state_dict(sd2)
model.eval()
prompt = "Hi! How are you?"
ids = [sp.piece_to_id("<u>")] + sp.encode(prompt) + [sp.piece_to_id("<b>")]
start = len(ids)
with torch.no_grad():
    for _ in range(40):
        t = int(model(torch.tensor([ids]))[0, -1].argmax())
        if t in (sp.eos_id(), sp.piece_to_id("<u>")):
            break
        ids.append(t)
(DATA / "ref.txt").write_text(" ".join(map(str, ids)) + "\n" + sp.decode(ids[start:]) + "\n")
print("prompt ids + greedy reply:", ids)
print("reply:", sp.decode(ids[start:]))
