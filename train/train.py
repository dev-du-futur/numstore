"""Train a tiny Llama-style chat model on ../data/train.bin.

Usage: python train.py [--dim 192 --layers 6 --heads 6 --kv_heads 2 --seq 256 --iters 20000]
Saves ../data/ckpt.pt (best validation loss, or the last with --keep last) and prints sample chats.
With --qbits 4, the block weights train against their 4-bit rounding from --qat_from on (QAT),
so the 4-bit export loses little.
"""
import argparse, math, time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

DATA = Path(__file__).resolve().parent.parent / "data"

p = argparse.ArgumentParser()
p.add_argument("--dim", type=int, default=192)
p.add_argument("--layers", type=int, default=6)
p.add_argument("--heads", type=int, default=6)
p.add_argument("--kv_heads", type=int, default=2)
p.add_argument("--hidden", type=int, default=0, help="FFN size, 0 = about 2.67x dim")
p.add_argument("--seq", type=int, default=256)
p.add_argument("--batch", type=int, default=64)
p.add_argument("--iters", type=int, default=20000)
p.add_argument("--lr", type=float, default=2e-3)
p.add_argument("--out", default="ckpt.pt")
p.add_argument("--init", default="", help="checkpoint to start from (fine-tuning)")
p.add_argument("--warm", type=int, default=500)
p.add_argument("--qbits", type=int, default=0, help="4: train block weights for 4-bit export (QAT)")
p.add_argument("--qgroup", type=int, default=32)
p.add_argument("--qat_from", type=int, default=0, help="iteration where quantization-aware training starts")
p.add_argument("--keep", choices=["best", "last"], default="best", help="which checkpoint to save")
args = p.parse_args()
if not args.hidden:
    args.hidden = int(round(args.dim * 8 / 3 / 16)) * 16


from model import Model


def main():
    import sentencepiece as spm
    sp = spm.SentencePieceProcessor(model_file=str(DATA / "tok.model"))
    vocab = sp.get_piece_size()
    train = np.memmap(DATA / "train.bin", dtype=np.uint16, mode="r")
    val = np.memmap(DATA / "val.bin", dtype=np.uint16, mode="r")
    # 1 where the target token is the bot's: only those count, so it learns to answer, not to ask
    masks = {id(train): np.memmap(DATA / "train_mask.bin", dtype=np.uint8, mode="r"),
             id(val): np.memmap(DATA / "val_mask.bin", dtype=np.uint8, mode="r")}
    dev = "cuda"
    torch.manual_seed(0)

    def batch(data):
        ix = np.random.randint(0, len(data) - args.seq - 1, args.batch)
        x = np.stack([data[i:i + args.seq] for i in ix]).astype(np.int64)
        y = np.stack([data[i + 1:i + 1 + args.seq] for i in ix]).astype(np.int64)
        mk = masks[id(data)]
        m = np.stack([mk[i + 1:i + 1 + args.seq] for i in ix])
        y[m == 0] = -100  # ignored by cross_entropy
        return torch.from_numpy(x).to(dev, non_blocking=True), torch.from_numpy(y).to(dev, non_blocking=True)

    model = Model(args, vocab).to(dev)
    if args.init:
        model.load_state_dict(torch.load(DATA / args.init, map_location=dev)["model"])
        print(f"starting from {args.init}")
    nparams = sum(p.numel() for p in model.parameters())
    print(f"params {nparams/1e6:.2f}M  ({vars(args)})  vocab {vocab}  train tokens {len(train):,}")
    decay = [p for p in model.parameters() if p.dim() >= 2]
    no_decay = [p for p in model.parameters() if p.dim() < 2]
    opt = torch.optim.AdamW([{"params": decay, "weight_decay": 0.1}, {"params": no_decay, "weight_decay": 0}],
                            lr=args.lr, betas=(0.9, 0.95), fused=True)
    warm = args.warm

    def lr_at(it):
        if it < warm:
            return args.lr * it / warm
        return args.lr * (0.1 + 0.9 * 0.5 * (1 + math.cos(math.pi * (it - warm) / (args.iters - warm))))

    @torch.no_grad()
    def evaluate():
        model.eval()
        losses = []
        for _ in range(20):
            x, y = batch(val)
            with torch.autocast("cuda", dtype=torch.bfloat16):
                losses.append(F.cross_entropy(model(x).float().view(-1, vocab), y.view(-1)).item())
        model.train()
        return sum(losses) / len(losses)

    @torch.no_grad()
    def chat(prompt):
        model.eval()
        ids = sp.encode("<u>" + prompt + "<b>")
        start = len(ids)
        for _ in range(60):
            logits = model(torch.tensor([ids[-args.seq:]], device=dev))[0, -1].float()
            probs = F.softmax(logits / 0.7, -1)
            t = torch.multinomial(probs, 1).item()
            if t in (sp.eos_id(), sp.piece_to_id("<u>")):
                break
            ids.append(t)
        model.train()
        return sp.decode(ids[start:])

    best = 1e9
    t0 = time.time()
    for it in range(args.iters + 1):
        for g in opt.param_groups:
            g["lr"] = lr_at(it)
        if args.qbits and it == args.qat_from:
            model.set_qat(args.qbits, args.qgroup)
            print(f"quantization-aware training on ({args.qbits}-bit, groups of {args.qgroup})", flush=True)
        if it % 1000 == 0:
            vl = evaluate()
            el = time.time() - t0
            print(f"it {it:6d}/{args.iters}  val {vl:.3f}  lr {lr_at(it):.2e}  {el/60:.1f} min", flush=True)
            for q in ["hi how are you", "what is the capital of france ?", "why is the sky blue", "whats a computer", "what should i eat for dinner"]:
                print(f"   > {q}\n   < {chat(q)}", flush=True)
            # --keep last: always save (fine-tunes on a new data mix score worse on the mixed
            # validation set at first, so "best" would keep the untouched starting weights)
            if vl < best or (args.keep == "last" and it > 0):
                best = min(best, vl)
                torch.save({"model": model.state_dict(), "args": vars(args), "vocab": vocab, "iter": it, "val": vl}, DATA / args.out)
        x, y = batch(train)
        with torch.autocast("cuda", dtype=torch.bfloat16):
            loss = F.cross_entropy(model(x).float().view(-1, vocab), y.view(-1))
        opt.zero_grad(set_to_none=True)
        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
        opt.step()
    print(f"done, best val {best:.3f}")


if __name__ == "__main__":
    main()
