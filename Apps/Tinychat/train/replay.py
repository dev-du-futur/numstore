"""Replay a real conversation against a model.bin through host/test.exe, the calculator's own
inference code, with the same memory rule as the app: the model sees at most the last two
exchanges, and keeps only the last one when a third would start.
Run from Apps/Tinychat/chat:  python ../train/replay.py ../data/X/model.bin
(Questions the built-in helpers answer on the calculator, like maths, still go to the model here.)"""
import os, subprocess, sys

MSGS = ["hey how long is a day ?", "what is rain", "how big are you", "are you alive", "I am alive.",
        "what ball?", "uh ??", "what did I say ?", "what is a cat", "how do you know ?", "tell a joke", "another",
        "haha", "Yes", "what can you do ?", "wow", "bye", "no dont go", ":(", "use an emoticon",
        "give me a fun fact", "make me a poem", "XD", "you are not happy", "do you have a context window",
        "your body ?", "did you muder someoe?!", "gg!", "alr, bye!", "what is the capital of france", "and spain?",
        "what is a dog", "how long do cats live", "tell me a story"]

model = sys.argv[1]
exe = os.path.join(os.path.dirname(__file__), "..", "chat", "host", "test.exe")
hist = []  # [(question, answer)]
for i, q in enumerate(MSGS):
    if len(hist) >= 2:
        hist = hist[-1:]
    env = dict(os.environ, NW_SEED=str(i + 1), NW_TEMP="0.4", NW_TOPK="40", NW_MINP="0.2", NW_REP="1.05", NW_PREV="1.3")
    if hist:
        env["NW_HIST"] = "|".join(x for qa in hist for x in qa)
    else:
        env.pop("NW_HIST", None)
    out = subprocess.run([exe, model, q, "chat"], env=env, capture_output=True, text=True).stdout
    a = out.strip().split("\n")[-1].strip()
    print(f"> {q}\n  {a}")
    hist.append((q, a))
