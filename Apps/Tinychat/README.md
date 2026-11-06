# TinyChat

A small chatbot that runs on the calculator itself, without a connection.

- The language model is Llama-style, with 3.7 million parameters stored in 4 bits (trained with quantisation-aware training) and a 1024-piece vocabulary. It runs at about 14 tokens per second.
- Built-in helpers answer maths (`12*(3+4)`, `sqrt 2`, `15% of 80`), prime checks and factors, and unit conversions directly. The model answers everything else.
- It remembers the previous exchange, so follow-ups work: "another" after a joke, "yes" after "Want another one?", "how do you know?", "and Spain?" after a capital. It forgets anything older, which keeps it fast.
- It comes in two sizes. **Full** (`models/tinychat-4m-4bit.bin`, 2.19 MB installed) gets 449 of 497 facts right. **Small** (`models/tinychat-2m-4bit.bin`, 1.31 MB installed) gets 423 of 497 and leaves about 1.1 MB for other apps. Both were fine-tuned on 2026-11-06 with everyday facts, reactions, questions about TinyChat itself and follow-up chats (`train/everyday.py`); `train/score_chat.py` scores 30 such situations (Full 24/30, Small 22/30, the earlier Full about 5/30).
- The engine also reads the older int8 format: `models/tinychat-2m-int8.bin` is the previous 2-million-parameter model, faster (about 18 tokens per second) but weaker.
- Keys:
  - EXE or OK: send
  - Backspace: delete
  - Shift+Backspace: new chat
  - Back: stop the reply, or quit
  - Up and Down: scroll

### Layout

| Path | What it is |
|---|---|
| `chat/src/` | The app: `main.c` (chat screen), `nwai.c` (model inference, int8/4-bit, DSP kernels), `helpers.c` (maths/units), `sbrk.c`, `icon.png` |
| `chat/host/` | PC tools: `test.c` runs the same engine on a PC, `helpers_test.c` tests the helpers, `make_icon.py` draws the icon |
| `train/` | Training: `prep.py` (dataset), `knowledge.py` (facts, small talk), `everyday.py` (everyday facts, reactions, questions about itself, follow-up chats), `train.py`, `model.py`, `export.py` (writes `model.bin`), `check_logits.py`, `eval_facts.py`, `replay.py` (replays a real conversation with the app's memory), `sweep.sh` |
| `models/` | Ready-to-install model files |

### Build and install

You need the [NumWorks SDK tools](https://www.numworks.com/engineering/software/build/) (`arm-none-eabi-gcc`, `nwlink`).

```bash
cp models/tinychat-4m-4bit.bin chat/model.bin
cd chat
make build check     # check = full link, catches missing functions
nwlink install-nwa --external-data model.bin output/app.nwa
```

Warning: `install-nwa` replaces all apps installed on the calculator. It takes about 45 seconds.

### Calculator limits learnt the hard way

- Apps get about 2.5 MB of flash (code plus model) and about 150 KB of heap. The largest heap block is about 128 KB.
- External data is memory-mapped but misaligned.
- Running code from RAM crashes the app.
- A long computation without yielding resets the calculator, so the app sleeps 1 ms per token.
- Speed is limited by flash reads, which must stay sequential. That's why each 4-bit group keeps its scale next to its weights.

### Training

Training uses Python with torch, sentencepiece and pyarrow. The datasets go in `data/`, which is not in the repo. `prep.py` lists them. One of them, starhopp3r/TinyChat, is CC BY-NC, so the trained models are for personal, non-commercial use.

```bash
python train/prep.py --knowledge=2000
python train/train.py --out ckpt.pt          # see --help; 4-bit: --qbits 4 --qat_from N
python train/export.py ckpt.pt               # writes model.bin
```
