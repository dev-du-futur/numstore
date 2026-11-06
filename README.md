# numstore

Apps for the NumWorks calculator (tested on an N0120).

## TinyChat

A small chatbot that runs on the calculator itself, without a connection.

- The language model is Llama-style, with about 2 million parameters, int8 weights and a 1024-piece vocabulary. It runs at about 18 tokens per second.
- Built-in helpers answer maths (`12*(3+4)`, `sqrt 2`, `15% of 80`), prime checks and factors, and unit conversions directly. The model answers everything else.
- The engine also supports 4-bit weights (format version 2), for a bigger model of about 3.7 million parameters at about 14 tokens per second.
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
| `train/` | Training: `prep.py` (dataset), `knowledge.py` (facts, small talk), `train.py`, `model.py`, `export.py` (writes `model.bin`), `check_logits.py`, `eval_facts.py`, `sweep.sh` |
| `models/` | Ready-to-install model files |

### Build and install

You need the [NumWorks SDK tools](https://www.numworks.com/engineering/software/build/) (`arm-none-eabi-gcc`, `nwlink`).

```bash
cp models/tinychat-2m-int8.bin chat/model.bin
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
