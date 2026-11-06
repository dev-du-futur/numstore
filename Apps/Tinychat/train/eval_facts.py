"""How many facts does a model.bin get right? Asks every fact of knowledge.py once, in a
casual wording it may not have seen exactly, through the calculator's own C code (host/test.exe),
with the calculator's sampling settings.
usage (from chat/): python ../train/eval_facts.py ../data/run5/model.bin [n_facts]"""
import os, random, re, subprocess, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import knowledge

model = sys.argv[1]
items = knowledge.all_items()
rng = random.Random(7)
if len(sys.argv) > 2:
    items = rng.sample(items, int(sys.argv[2]))


def casual(q):
    q = q.lower().replace("'", "")
    return q.rstrip("?.!") + rng.choice(["", " ?", "?"])


def key(ans):
    """what must appear in the reply: the city for capitals, else the first 3 distinctive words"""
    m = re.match(r"The capital of .* is ([A-Z][\w ,.]+?)(?: \(|\.$)", ans)
    if m:
        return [m.group(1).rstrip(".")]
    words = [w for w in re.findall(r"[A-Za-z0-9,]+", ans) if len(w) > 3][:3]
    return words


env = dict(NW_TEMP="0.4", NW_MINP="0.2", NW_REP="1.2", NW_TOPK="40", NW_SEED="1")
env.update({k: v for k, v in os.environ.items() if k.startswith("NW_")})
env = dict(os.environ, **env)
ok = 0
for qs, answers in items:
    q = casual(rng.choice(qs))
    out = subprocess.run(["./host/test.exe", model, q, "chat"], capture_output=True, text=True, env=env).stdout
    reply = out.strip().split("\n")[-1]
    good = any(all(k.lower() in reply.lower() for k in key(a)) for a in answers)
    ok += good
    print(("OK  " if good else "--  ") + q + "  ->  " + reply[:100])
print(f"\n{ok}/{len(items)} facts right")
