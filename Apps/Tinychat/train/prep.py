"""Build the training set for TinyChat: user/assistant chats where the bot answers what was asked.

Every conversation becomes:  <u>user text<b>bot text<u>user text<b>bot text</s>
Training only learns the bot's words (mask.bin marks them), so the model learns to answer,
not to imitate the user. Only plain ASCII is kept, because that is what the calculator draws.

Sources (in ../data), with how many times each is repeated:
  smol-smoltalk (Apache-2.0)         x3  short questions, answer trimmed to its first sentences
  everyday-conversations (Apache-2.0) x5  simple assistant chats
  starhopp3r/TinyChat (CC BY-NC)      x1  simple user/assistant chats (personal use only)
  raincandy-u/TinyChat (Apache-2.0)   40k kid-to-kid chats, for plain simple English
  identity (written here)             x4  who/what TinyChat is
  greetings (written here)            x6  greetings answered as greetings
  knowledge (knowledge.py)          300/fact  capitals, definitions, common facts
Users' messages are half the time rewritten casually (lowercase, no punctuation...).
Outputs: chats.txt, tok.model/.vocab, train.bin/val.bin (uint16 tokens), train_mask.bin/val_mask.bin (uint8)
"""
import json, random, re, sys
from pathlib import Path

import numpy as np
import pyarrow.parquet as pq
import sentencepiece as spm

DATA = Path(__file__).resolve().parent.parent / "data"
VOCAB = int(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1].isdigit() else 1024
MAX_TURN = 200          # characters; the screen is small
BAD = set("*#`|$" + chr(92) + "<>[]{}")
random.seed(0)


def clean(s):
    s = (s.replace("’", "'").replace("‘", "'").replace("“", '"').replace("”", '"')
          .replace("—", " - ").replace("–", "-").replace("…", "..."))
    return re.sub(r"\s+", " ", s).strip()


def first_sentences(a, limit=180):
    out = ""
    for s in re.split(r"(?<=[.!?])\s+", a):
        if len(out) + len(s) + 1 > limit:
            break
        out = (out + " " + s).strip()
    return out


def ok(turns):
    return len(turns) >= 2 and all(t and len(t) <= MAX_TURN and t.isascii() and not (set(t) & BAD)
                                    for _, t in turns)


CASUAL_RNG = random.Random(1)


def casual(t, r=CASUAL_RNG):
    """How people type on a calculator: often lowercase, little punctuation, no apostrophes,
    sometimes "u" for "you" or a space before "?"."""
    if r.random() < 0.65:
        t = t.lower()
    if r.random() < 0.5:
        t = t.replace("'", "")
    if r.random() < 0.5:
        t = t.replace(",", "")
    x = r.random()
    if x < 0.45:
        t = t.rstrip(".!?")
    elif x < 0.6 and t.endswith("?"):
        t = t[:-1].rstrip() + " ?"
    if r.random() < 0.08:
        t = re.sub(r"\byou\b", "u", t)
        t = re.sub(r"\byour\b", "ur", t)
    if r.random() < 0.05:
        t = re.sub(r"\bplease\b", "pls", t)
    return t.strip() or "hi"


def fmt(turns, casual_rate=0.5):
    """Half of the users' messages are rewritten the casual way, so both styles are understood."""
    return "".join(("<u>" + (casual(t) if CASUAL_RNG.random() < casual_rate else t)) if who == "u" else "<b>" + t
                   for who, t in turns)


sources = {}

# smol-smoltalk: first user question + first sentences of the answer
pairs = []
for f in sorted(DATA.glob("smoltalk_*.parquet")):
    for msgs in pq.read_table(f, columns=["messages"]).column("messages").to_pylist():
        if len(msgs) < 2 or msgs[0]["role"] != "user" or msgs[1]["role"] != "assistant":
            continue
        if "\n" in msgs[0]["content"].strip():
            continue
        q, a = clean(msgs[0]["content"]), first_sentences(clean(msgs[1]["content"]))
        if len(q) <= 120 and len(a) >= 15 and ok([("u", q), ("b", a)]):
            pairs.append(fmt([("u", q), ("b", a)]))
sources["smoltalk"] = (pairs, 3)

# everyday-conversations
ev = []
for msgs in pq.read_table(DATA / "everyday.parquet").column("messages").to_pylist():
    turns = [("u" if m["role"] == "user" else "b", clean(m["content"]))
             for m in msgs if m["role"] in ("user", "assistant")]
    if ok(turns):
        ev.append(fmt(turns))
sources["everyday"] = (ev, 5)

# starhopp3r/TinyChat: "[INST] user [/INST] bot [INST] ..."
st = []
for name in ["tinychat_starhopp3r_part.txt", "tinychat_starhopp3r_rest.txt"]:
    with open(DATA / name, encoding="utf8", errors="ignore") as f:
        lines = f.readlines()
    for line in lines[1:-1]:  # first/last lines may be cut where the download was split
        turns = []
        for m in re.finditer(r"\[INST\](.*?)\[/INST\](.*?)(?=\[INST\]|$)", line, re.S):
            turns += [("u", clean(m.group(1))), ("b", clean(m.group(2)))]
        if ok(turns):
            st.append(fmt(turns))
sources["starhopp3r"] = (st, 1)

# raincandy-u/TinyChat: kid-to-kid, a subset only
rc = []
for c in json.load(open(DATA / "tinychat_raincandy.json", encoding="utf8")):
    turns = [("u" if m.group(1) == "A" else "b", clean(m.group(2)))
             for m in re.finditer(r"<(A|B)>(.*?)<end>", c, re.S)]
    if ok(turns):
        rc.append(fmt(turns))
random.shuffle(rc)
sources["raincandy"] = (rc[:40000], 1)

# identity
asks = ["Who are you?", "What are you?", "What is your name?", "What's your name?", "Are you a robot?",
        "Are you an AI?", "Where do you live?", "Who made you?", "Are you human?", "What can you do?",
        "Tell me about yourself.", "Are you smart?", "How do you work?", "Are you ChatGPT?",
        "who are you", "what are you", "whats your name", "are you real?", "Introduce yourself."]
answers = {
    "name": ["I'm TinyChat, a very small AI that lives in your calculator.",
             "My name is TinyChat. I'm a tiny AI running on this calculator.",
             "I'm TinyChat! I'm a small language model inside your NumWorks calculator."],
    "what": ["I'm a tiny AI. I run right here on your calculator, with no internet.",
             "I'm a small AI program. I live in this calculator and I like to chat.",
             "I'm an AI, but a very small one. I fit inside your calculator!"],
    "can": ["I can chat with you about simple things. I'm small, so I make mistakes.",
            "I can answer simple questions and talk with you. I'm not very smart, but I try!"],
}
ident = []
for q in asks:
    key = "can" if ("do" in q.lower() or "smart" in q.lower() or "work" in q.lower()) else (
        "name" if ("name" in q.lower() or "who" in q.lower() or "yourself" in q.lower()) else "what")
    for a in answers[key]:
        ident.append(fmt([("u", q), ("b", a)]))
        ident.append(fmt([("u", "Hi!"), ("b", "Hello! How can I help you today?"), ("u", q), ("b", a)]))
sources["identity"] = (ident, 4)

# greetings: answered as greetings, not with the self-introduction
greets = ["hi", "Hi!", "hello", "Hello!", "hey", "Hey there!", "how are you?", "How are you?", "hi, how are you ?",
          "Hi! How are you?", "hello, how are you doing?", "good morning", "Good morning!", "good evening",
          "what's up?", "whats up", "how's it going?", "How is your day?", "yo", "Hi there, how are you today?"]
greet_replies = ["Hi! I'm doing well, thanks. How can I help you today?",
                 "Hello! I'm good, thank you. What would you like to talk about?",
                 "Hey! I'm fine, thanks for asking. How are you?",
                 "Hello! How can I help you today?",
                 "Hi there! I'm doing great. What's on your mind?"]
greet = [fmt([("u", g), ("b", r)]) for g in greets for r in greet_replies]
greet += [fmt([("u", g), ("b", r), ("u", "I'm good too, thanks."), ("b", "Great! What would you like to talk about?")])
          for g in greets[:8] for r in greet_replies[:3]]
sources["greetings"] = (greet, 6)

# knowledge: capitals, definitions and common facts, learnt by heart (see knowledge.py)
import knowledge
KN = int(next((a.split("=")[1] for a in sys.argv if a.startswith("--knowledge=")), 300))
kn, nfacts = knowledge.chats(KN, lambda t: t, random.Random(2))
sources["knowledge"] = ([fmt(t, casual_rate=0.7) for t in kn if ok(t)], 1)
print(f"knowledge: {nfacts} facts")
# open chat: vague messages, single words and statements (see knowledge.open_chat)
sources["openchat"] = ([fmt(t, casual_rate=0.3) for t in knowledge.open_chat(random.Random(3), 150000) if ok(t)], 1)

convs = []
for name, (items, rep) in sources.items():
    print(f"{name:11s} {len(items):8d} conversations x{rep}")
    convs += items * rep
random.shuffle(convs)
print(f"total {len(convs):,}")
(DATA / "chats.txt").write_text("\n".join(convs), encoding="ascii")

if "--keep-tokenizer" not in sys.argv:  # fine-tuning must keep the same vocabulary
  spm.SentencePieceTrainer.train(
    input=str(DATA / "chats.txt"), model_prefix=str(DATA / "tok"),
    model_type="bpe", vocab_size=VOCAB, character_coverage=1.0, byte_fallback=True,
    user_defined_symbols=["<u>", "<b>"], normalization_rule_name="identity", add_dummy_prefix=False,
    split_digits=True, input_sentence_size=600000, shuffle_input_sentence=True,
    pad_id=-1, unk_id=0, bos_id=1, eos_id=2, num_threads=8, minloglevel=2)
sp = spm.SentencePieceProcessor(model_file=str(DATA / "tok.model"))
U, B, EOS = sp.piece_to_id("<u>"), sp.piece_to_id("<b>"), sp.eos_id()

id_parts, mask_parts = [], []
CHUNK = 100000
for start in range(0, len(convs), CHUNK):
    for t in sp.encode(convs[start:start + CHUNK], num_threads=8):
        t = np.array(t + [EOS], dtype=np.uint16)
        # mask[j] = 1 when token j is the bot's: everything after <b>, up to and including
        # the <u> or end-of-chat that closes its reply (so it learns when to stop)
        nb = np.cumsum(t == B)
        nu = np.cumsum(t == U)
        bot_open = (nb == nu) & (nb > 0)   # turns alternate <u>,<b>,<u>,...: equal counts = inside a reply
        m = np.zeros(len(t), dtype=np.uint8)
        m[1:] = bot_open[:-1]              # token j is the bot's if a reply was open before it
        id_parts.append(t)
        mask_parts.append(m)
    print(f"  encoded {min(start + CHUNK, len(convs)):,}", flush=True)

ids = np.concatenate(id_parts)
mask = np.concatenate(mask_parts)
split = int(len(ids) * 0.995)
ids[:split].tofile(DATA / "train.bin"); ids[split:].tofile(DATA / "val.bin")
mask[:split].tofile(DATA / "train_mask.bin"); mask[split:].tofile(DATA / "val_mask.bin")
print(f"tokens {len(ids):,}, of which bot tokens {int(mask.sum()):,}")
