"""Score a model on everyday questions and follow-ups, several seeds each, through host/test.exe.
Run from Apps/Tinychat/chat:  python ../train/score_chat.py ../data/X/model.bin [seeds]
Each case: (earlier turns or "", question, words the answer must contain (any of))."""
import os, subprocess, sys

J1 = "What do cats eat for breakfast? Mice Krispies."
CASES = [
    ("", "hey how long is a day ?", ["24"]),
    ("", "what is rain", ["clouds"]),
    ("", "how big are you", ["tiny", "megabytes"]),
    ("", "are you alive", ["program"]),
    ("", "what is a cat", ["whiskers"]),
    ("", "wow", ["Cool", "Glad"]),
    ("", "gg!", ["Thank"]),
    ("", "XD", ["laugh", "Haha"]),
    ("", ":(", ["wrong"]),
    ("", "uh ??", ["Sorry"]),
    ("", "no dont go", ["still here"]),
    ("", "alr, bye!", ["Bye", "Goodbye"]),
    ("", "use an emoticon", [":)", ":D", "^_^"]),
    ("", "give me a fun fact", ["octopus", "Honey", "Bananas", "Venus", "Sharks", "Butterflies", "flamingos",
                                "Eiffel", "Light", "Wombat", "otters", "Koalas", "Owls", "chess"]),
    ("", "make me a poem", ["Numbers dance", "Little keys", "Roses"]),
    ("", "do you have a context window", ["remember"]),
    ("", "your body ?", ["body"]),
    ("", "did you muder someoe?!", ["couldn't hurt"]),
    ("", "what can you do ?", ["chat"]),
    ("", "how long do cats live", ["12 to 18"]),
    ("", "tell me a story", ["Once upon", "little cat", "brave robot"]),
    ("tell a joke|" + J1, "another", ["?"]),  # a joke, checked below to differ
    ("tell a joke|" + J1 + "|haha|Glad it made you laugh! Want another one?", "Yes", ["?"]),
    ("what is a cat|A cat is a small furry animal with whiskers and sharp claws. Many people keep cats as pets.",
     "how do you know ?", ["learned"]),
    ("what is the capital of france|The capital of France is Paris.", "and spain?", ["Madrid"]),
    ("what is rain|Rain is water that falls from clouds in drops.", "what did I say ?", ['"what is rain"']),
    ("bye|Bye! Talk to you soon.", "no dont go", ["still here"]),
    ("give me a fun fact|Sharks existed before trees did.", "another", ["octopus", "Honey", "Bananas", "Venus",
     "Butterflies", "flamingos", "Eiffel", "Light", "Wombat", "otters", "Koalas", "Owls", "chess"]),
    ("tell a joke|" + J1, "what is a dog", ["friendly"]),       # an unrelated question after a joke
    ("give me a fun fact|Sharks existed before trees did.|wow|Cool, right? Want another fun fact?", "bye",
     ["Bye", "Goodbye"]),
]

model = sys.argv[1]
seeds = int(sys.argv[2]) if len(sys.argv) > 2 else 5
exe = os.path.join(os.path.dirname(__file__), "..", "chat", "host", "test.exe")
total = good = 0
for hist, q, want in CASES:
    ok, last = 0, ""
    for s in range(seeds):
        env = dict(os.environ, NW_SEED=str(s + 1), NW_TEMP="0.4", NW_TOPK="40", NW_MINP="0.2", NW_REP="1.05",
                   NW_PREV="1.3")
        if hist:
            env["NW_HIST"] = hist
        else:
            env.pop("NW_HIST", None)
        a = subprocess.run([exe, model, q, "chat"], env=env, capture_output=True, text=True).stdout.strip().split("\n")[-1]
        hit = any(w in a for w in want) and not (q in ("another", "Yes") and a.strip() == J1)
        ok += hit
        if not hit and not last:
            last = a
    total += seeds
    good += ok
    print(f"{ok}/{seeds}  {q:32s} {'' if ok == seeds else '<- e.g. ' + last[:70]}")
print(f"TOTAL {good}/{total}")
