"""Facts for TinyChat to learn by heart: capitals, definitions, common questions.

Each fact becomes many short chats, each with a different wording of the question
(prep.py also makes the wording casual: lowercase, no punctuation, "u" for "you"...),
so the model learns to give the right answer however it is asked.
knowledge_raw.json holds the tables that used to be built into the app.
"""
import json, random
from pathlib import Path

RAW = json.load(open(Path(__file__).with_name("knowledge_raw.json")))


def pairs(lst):
    return [(lst[i], lst[i + 1]) for i in range(0, len(lst), 2)]


SHOW = dict(pairs(RAW["COUNTRY_SHOW"]))


def country_name(c):
    return SHOW.get(c, " ".join(w.capitalize() for w in c.split()))


def capitals():
    out = []
    seen = {}
    for c, cap in pairs(RAW["CAPITALS"]):
        name = country_name(c)
        bare = name[4:] if name.startswith("the ") else name
        city = cap.split(" (")[0]
        answers = [f"The capital of {name} is {cap}.", f"{city} is the capital of {name}.",
                   f"It's {city}."]
        qs = [f"What is the capital of {name}?", f"what is the capital of {bare}", f"capital of {bare}",
              f"whats the capital of {bare}", f"What's {bare}'s capital?", f"what is {bare}{'' if bare.endswith('s') else 's'} capital",
              f"Which city is the capital of {name}?", f"capital of {bare}?", f"tell me the capital of {bare}",
              f"do you know the capital of {bare}?", f"what city is the capital of {bare}"]
        out.append((qs, answers[:2]))
        seen.setdefault(cap, []).append(name)
    return out


def definitions():
    out = []
    people = ("einstein", "newton", "napoleon", "shakespeare", "mozart", "vinci", "curie", "darwin", "galileo",
              "gandhi", "pythagoras")
    for term, ans in pairs(RAW["DEFS"]):
        t = term
        bare = t.split(" ", 1)[1] if t.split(" ")[0] in ("a", "an", "the") and " " in t else t
        if any(p in t for p in people):
            qs = [f"Who was {t.title()}?", f"who was {t}", f"who is {t}", f"tell me about {t}",
                  f"who was {t}?", f"do you know {t}?", f"what did {t} do?"]
        else:
            art = "" if t in ("ai", "artificial intelligence", "internet", "gravity", "photosynthesis", "energy",
                              "electricity", "water", "oxygen", "democracy", "math", "maths", "mathematics",
                              "algebra", "geometry", "pi", "physics", "chemistry", "biology", "python", "dna",
                              "h2o", "co2", "numworks") or t.split(" ")[0] in ("a", "an", "the") else (
                "an " if t[0] in "aeiou" else "a ")
            qs = [f"What is {art}{t}?", f"what is {art}{t}", f"whats {art}{t}", f"what's {art}{t}?",
                  f"define {bare}", f"explain {bare}", f"tell me about {bare}", f"what does {bare} mean?",
                  f"can you explain {art}{t}?", f"{bare}?"]
        out.append((qs, [ans]))
    return out


# hand-written questions for the keyword facts (answer text comes from the old table)
FACT_QS = {
    "sky blue": ["Why is the sky blue?", "why is the sky blue", "why is sky blue", "what makes the sky blue?",
                 "how come the sky is blue"],
    "sunset* red": ["Why are sunsets red?", "why is the sunset red", "why is the sky red at sunset"],
    "boil* water": ["At what temperature does water boil?", "when does water boil", "what temp does water boil at",
                    "boiling point of water?", "how hot is boiling water"],
    "freez* water": ["When does water freeze?", "at what temperature does water freeze", "freezing point of water",
                     "what temp does water freeze"],
    "speed light": ["What is the speed of light?", "how fast is light", "speed of light?", "how fast does light travel"],
    "speed sound": ["What is the speed of sound?", "how fast is sound", "speed of sound?"],
    "how many planets": ["How many planets are there?", "how many planets are in the solar system",
                         "how many planets", "name the planets", "what are the planets"],
    "largest planet": ["What is the largest planet?", "biggest planet?", "which planet is the biggest",
                       "whats the largest planet"],
    "smallest planet": ["What is the smallest planet?", "smallest planet?", "which planet is the smallest"],
    "hottest planet": ["What is the hottest planet?", "hottest planet?", "which planet is the hottest"],
    "closest planet sun": ["Which planet is closest to the sun?", "closest planet to the sun", "nearest planet to the sun"],
    "pluto planet": ["Is Pluto a planet?", "is pluto a planet", "why is pluto not a planet"],
    "far sun": ["How far is the sun?", "how far away is the sun", "distance to the sun?", "how far is the sun from earth"],
    "far moon": ["How far is the moon?", "how far away is the moon", "distance to the moon?"],
    "old earth": ["How old is the Earth?", "how old is earth", "age of the earth?"],
    "old universe": ["How old is the universe?", "how old is the universe", "age of the universe?"],
    "old sun": ["How old is the sun?", "how old is the sun"],
    "how many continents": ["How many continents are there?", "how many continents", "name the continents"],
    "how many oceans": ["How many oceans are there?", "how many oceans", "name the oceans"],
    "largest ocean": ["What is the largest ocean?", "biggest ocean?", "which ocean is the biggest"],
    "longest river": ["What is the longest river?", "longest river in the world?", "which river is the longest"],
    "highest mountain": ["What is the highest mountain?", "tallest mountain in the world?", "how tall is mount everest",
                         "how high is everest", "what is the tallest mountain"],
    "largest country": ["What is the largest country?", "biggest country in the world?", "which country is the biggest"],
    "smallest country": ["What is the smallest country?", "smallest country in the world?"],
    "most populous country": ["Which country has the most people?", "most populated country?",
                              "what country has the biggest population"],
    "world population": ["How many people are on Earth?", "how many people live in the world",
                         "world population?", "how many humans are there"],
    "largest desert": ["What is the largest desert?", "biggest desert?", "which desert is the biggest"],
    "largest city": ["What is the biggest city in the world?", "largest city?", "biggest city in the world"],
    "largest animal": ["What is the largest animal?", "biggest animal ever?", "which animal is the biggest"],
    "fastest animal": ["What is the fastest animal?", "fastest animal?", "which animal is the fastest"],
    "tallest animal": ["What is the tallest animal?", "tallest animal?"],
    "how many bones": ["How many bones are in the human body?", "how many bones do we have", "how many bones in a human"],
    "how many teeth": ["How many teeth do adults have?", "how many teeth do we have", "how many teeth"],
    "how many hearts octopus": ["How many hearts does an octopus have?", "how many hearts does an octopus have"],
    "how many legs spider": ["How many legs does a spider have?", "how many legs do spiders have"],
    "how many legs insect": ["How many legs does an insect have?", "how many legs do insects have"],
    "leap year": ["What is a leap year?", "whats a leap year", "how often is a leap year"],
    "days february": ["How many days are in February?", "how many days in february"],
    "how many days year": ["How many days are in a year?", "how many days in a year"],
    "how many months": ["How many months are in a year?", "how many months in a year"],
    "who wrote romeo": ["Who wrote Romeo and Juliet?", "who wrote romeo and juliet"],
    "who wrote hamlet": ["Who wrote Hamlet?", "who wrote hamlet"],
    "who wrote harry potter": ["Who wrote Harry Potter?", "who wrote harry potter", "who is the author of harry potter"],
    "who painted mona lisa": ["Who painted the Mona Lisa?", "who painted the mona lisa", "who made the mona lisa"],
    "who discovered gravity": ["Who discovered gravity?", "who discovered gravity"],
    "who discovered penicillin": ["Who discovered penicillin?", "who discovered penicillin"],
    "who invented telephone": ["Who invented the telephone?", "who invented the phone", "who made the first telephone"],
    "who invented light bulb": ["Who invented the light bulb?", "who invented the lightbulb"],
    "who invented web": ["Who invented the World Wide Web?", "who invented the web"],
    "who invented internet": ["Who invented the internet?", "who made the internet"],
    "who invented computer": ["Who invented the computer?", "who made the first computer"],
    "first man moon": ["Who was the first man on the moon?", "who walked on the moon first",
                       "first person on the moon?", "when did we land on the moon"],
    "first president": ["Who was the first president of the United States?", "first us president?",
                        "who was the first american president"],
    "world war 2": ["When was World War 2?", "when did ww2 start", "when did world war 2 end", "when was ww2"],
    "world war 1": ["When was World War 1?", "when did ww1 start", "when was world war one"],
    "french revolution": ["When was the French Revolution?", "when did the french revolution start"],
    "who discovered america": ["Who discovered America?", "who discovered america", "when did columbus reach america"],
    "symbol gold": ["What is the chemical symbol for gold?", "symbol for gold?"],
    "symbol iron": ["What is the chemical symbol for iron?", "symbol for iron?"],
    "symbol silver": ["What is the chemical symbol for silver?", "symbol for silver?"],
    "how many elements": ["How many elements are there?", "how many elements in the periodic table"],
    "pythagor*": ["What is the Pythagorean theorem?", "whats pythagoras theorem", "pythagorean theorem?"],
    "area circle": ["What is the area of a circle?", "how do i find the area of a circle", "area of a circle formula"],
    "circumference circle": ["What is the circumference of a circle?", "perimeter of a circle?",
                             "how do i find the circumference of a circle"],
    "area triangle": ["What is the area of a triangle?", "how do i find the area of a triangle"],
    "area rectangle": ["What is the area of a rectangle?", "area of a rectangle?"],
    "area square": ["What is the area of a square?", "area of a square?"],
    "volume sphere": ["What is the volume of a sphere?", "volume of a sphere formula"],
    "volume cube": ["What is the volume of a cube?", "volume of a cube?"],
    "angles triangle": ["What do the angles of a triangle add up to?", "sum of angles in a triangle"],
    "sides triangle": ["How many sides does a triangle have?"],
    "sides pentagon": ["How many sides does a pentagon have?"],
    "sides hexagon": ["How many sides does a hexagon have?"],
    "sides octagon": ["How many sides does an octagon have?"],
    "colors rainbow": ["What are the colors of the rainbow?", "colors of the rainbow?", "rainbow colours"],
    "primary colors": ["What are the primary colors?", "primary colours?"],
    "tomato fruit": ["Is a tomato a fruit?", "is tomato a fruit or a vegetable"],
    "meaning life": ["What is the meaning of life?", "meaning of life?"],
    "what time": ["What time is it?", "what time is it", "whats the time"],
    "weather": ["What's the weather like?", "how is the weather today", "is it going to rain"],
    "e mc2": ["What is E=mc2?", "what does e=mc2 mean"],
}


def facts():
    ans = dict(pairs(RAW["FACTS"]))
    return [(qs, [ans[k]]) for k, qs in FACT_QS.items()]


def jokes():
    """One item per joke, so each joke is learnt as well as a fact (sharing one item split it 6 ways)."""
    qs = ["Tell me a joke.", "tell me a joke", "tell a joke", "tell me a joke!", "say something funny",
          "do you know any jokes?", "joke please", "a joke", "joke", "make me laugh", "can you tell me a joke?",
          "tell me a funny joke", "another joke", "tell me another joke", "know any jokes", "say a joke"]
    return [(qs, [j]) for j in RAW["JOKES"]]


def smalltalk():
    """Greetings and questions about TinyChat itself, however they're typed."""
    hi = ["", "hi ", "hello ", "hey ", "Hi! ", "Hello, "]
    how = ["how are you", "how are you doing", "how r u", "how are u", "hows it going", "how is it going",
           "how do you do", "are you ok", "how have you been", "how are you today"]
    return [
        ([h + q for h in hi for q in how],
         ["I'm doing well, thanks! How about you?", "I'm good, thank you! How are you?",
          "I'm great, thanks for asking! What's up?"]),
        (["who are you", "what are you", "introduce yourself", "tell me about yourself", "who r u",
          "are you a robot", "are you an ai", "are you human", "are you real", "what is tinychat",
          "hi who are you", "hello what are you"],
         ["I'm TinyChat, a tiny AI that runs right here on your calculator.",
          "I'm TinyChat! I'm a very small AI living in this calculator, with no internet."]),
        (["whats your name", "what is your name", "your name?", "do you have a name", "what should i call you",
          "hi whats your name", "what's ur name", "whats ur name", "name?", "what is ur name", "ur name",
          "tell me your name", "what are you called", "whats your name?"],
         ["My name is TinyChat.", "I'm TinyChat!"]),
        (["who made you", "who created you", "who built you", "where do you come from", "how were you made"],
         ["I was trained on a computer, then shrunk to fit on this NumWorks calculator."]),
        (["what can you do", "what do you do", "how can you help me", "what are you good at",
          "what can i ask you"],
         ["I can chat, answer simple questions and do maths. I'm small, so I sometimes make mistakes."]),
        (["are you smart", "are you clever", "how smart are you", "are you dumb", "you are dumb"],
         ["I'm a very small AI, so I know some things but I make mistakes. Ask me and we'll see!"]),
        (["thanks", "thank you", "thx", "thanks a lot", "thank u", "ok thanks", "great thanks"],
         ["You're welcome!", "Happy to help!", "Anytime!"]),
        (["can you help me", "help me", "can you help me with my homework", "i need help", "help",
          "can u help me", "i have a question"],
         ["Sure! What do you need help with?", "Of course! What's your question?"]),
        (["bye", "goodbye", "see you", "see you later", "bye bye", "i have to go", "cya"],
         ["Bye! Talk to you soon.", "Goodbye! Have a great day."]),
        (["good night", "goodnight", "night", "im going to sleep"],
         ["Good night! Sleep well."]),
        (["good morning", "morning"], ["Good morning! How can I help you today?"]),
        (["whats up", "sup", "wassup", "what are you doing"],
         ["Not much, just living in your calculator! What's up with you?"]),
        (["im bored", "i am bored", "im so bored", "entertain me"],
         ["Let's chat! Ask me a question, or ask me for a joke."]),
        (["haha", "hahaha", "lol", "lmao", "thats funny", "funny", "xd", "hehe", "haha good one"],
         ["Glad it made you laugh! Want another one?", "Haha, glad you liked it!"]),
        (["ok", "okay", "cool", "nice", "wow"],
         ["Anything else you'd like to know?", "Glad you like it! What else can I do for you?"]),
        (["i love you", "you are cool", "you are nice", "i like you"],
         ["Thank you! I like chatting with you too."]),
    ]


FOODS = ["pizza", "pasta", "chocolate", "ice cream", "burgers", "sushi", "apples", "bananas", "cake", "cookies",
         "soup", "rice", "cheese", "fries", "pancakes", "salad", "tacos", "strawberries", "bread", "noodles"]
THINGS = ["music", "football", "video games", "maths", "drawing", "reading", "science", "basketball", "movies",
          "dancing", "swimming", "chess", "coding", "history", "art", "minecraft", "tennis", "cats", "dogs",
          "space", "cars", "anime", "physics", "running", "cooking", "school", "rain", "winter", "summer"]
PETS = ["dog", "cat", "hamster", "rabbit", "fish", "parrot", "turtle", "guinea pig", "horse", "snake"]
PLACES = ["school", "the park", "the beach", "the cinema", "my friend's house", "the zoo", "the mall",
          "the library", "the pool", "the museum", "paris", "the gym", "my grandma's house", "a concert"]
GOOD = ["happy", "great", "excited", "good", "fine", "proud", "relaxed"]
BAD = ["sad", "tired", "bored", "angry", "stressed", "sick", "lonely", "worried", "upset"]
WORDS = ["pro", "car", "tree", "phone", "house", "blue", "dog", "book", "game", "water", "food", "school", "music",
         "money", "time", "love", "friend", "sport", "movie", "song", "paper", "pen", "door", "window", "table",
         "red", "green", "sky", "sea", "city", "train", "bike", "shoe", "hat", "box", "ball", "star", "fire"]


def open_chat(rng, n):
    """Chats with no clear question: vague messages, single words, gibberish and plain statements.
    The bot asks what is meant, or picks up what was said and asks about it, instead of reciting
    a learnt fact (before this, "pro" got "Prague is the capital of Croatia")."""
    def one():
        k = rng.random()
        if k < 0.14:
            q = rng.choice(["what", "what ?", "what?", "huh", "huh?", "?", "??", "hmm", "what do you mean",
                            "i dont understand", "what is it", "say what", "eh", "um", "uh", "and", "so", "wait"])
            a = rng.choice(["Sorry, I didn't get that. Can you ask in another way?", "What would you like to know?",
                            "Hmm, can you tell me a bit more?"])
        elif k < 0.24:
            q = "".join(rng.choice("abcdefghijklmnopqrstuvwxyz") for _ in range(rng.randint(2, 7)))
            a = rng.choice(["Sorry, I didn't understand that. Could you say it again?",
                            "Hmm, I don't know that word. Can you ask me a full question?"])
        elif k < 0.38:
            w = rng.choice(WORDS + THINGS + FOODS)
            q = rng.choice([w, w + "?", w + " ?", "about " + w])
            a = rng.choice([f"{w.capitalize()}? What would you like to know about {w}?",
                            f"What about {w}? Ask me a question about it!"])
        elif k < 0.45:
            x = rng.choice(FOODS + THINGS)
            q = rng.choice([f"i like {x}", f"i love {x}", f"{x} is my favorite", f"i really like {x}"])
            a = rng.choice([f"Nice! What do you like most about {x}?", f"Cool, {x} is great! Why do you like it?",
                            f"That's nice! How often do you have {x}?" if x in FOODS else
                            f"That's nice! How did you get into {x}?"])
        elif k < 0.52:
            x = rng.choice(FOODS + THINGS)
            q = rng.choice([f"i hate {x}", f"i dont like {x}", f"{x} is boring", f"i dont really like {x}"])
            a = rng.choice([f"Oh, why don't you like {x}?", "Fair enough! What do you like instead?"])
        elif k < 0.62:
            x = rng.choice(PLACES)
            q = rng.choice([f"i went to {x}", f"i was at {x} today", f"im going to {x}", f"i went to {x} yesterday"])
            y = x.replace("my ", "your ")
            a = rng.choice([f"Sounds nice! How was {y}?", f"Cool! What did you do at {y}?"]) if "going" not in q \
                else rng.choice([f"Have fun at {y}!", f"Nice! What will you do at {y}?"])
        elif k < 0.72:
            x = rng.choice(PETS)
            q = rng.choice([f"i have a {x}", f"my {x} is cute", f"i got a new {x}", f"i want a {x}"])
            a = rng.choice([f"What's your {x}'s name?", f"That's great! Tell me about your {x}."]) if "want" not in q \
                else rng.choice([f"A {x} would be fun! Why do you want one?"])
        elif k < 0.80:
            x = rng.choice(GOOD)
            q = rng.choice([f"im {x}", f"i feel {x}", f"i am so {x}", f"im {x} today", f"i feel {x} today"])
            a = rng.choice([f"That's great! What made you feel {x}?", f"Glad to hear it! What happened?"])
        elif k < 0.90:
            x = rng.choice(BAD)
            q = rng.choice([f"im {x}", f"i feel {x}", f"i am so {x}", f"im {x} today", f"i feel {x} today"])
            a = rng.choice([f"Oh no, sorry you feel {x}. Do you want to talk about it?",
                            f"I'm sorry you're {x}. What happened?"])
        else:
            q = rng.choice(["yes", "no", "maybe", "sure", "idk", "i dont know", "nothing", "not really", "yeah",
                            "nope", "of course", "i guess", "whatever"])
            a = rng.choice(["Okay! Is there anything you'd like to talk about?", "No problem. Ask me anything!",
                            "Alright! What else is on your mind?"])
        return [("u", q), ("b", a)]
    return [one() for _ in range(n)]


def all_items():
    return capitals() + definitions() + facts() + jokes() + smalltalk()


def chats(n_per_fact, casual, rng):
    """n_per_fact chats per fact; some are a follow-up after a greeting or another fact."""
    items = all_items()
    greet = [("hi", "Hi! How can I help you today?"), ("hello", "Hello! What would you like to know?"),
             ("hey", "Hey! What's on your mind?")]
    out = []
    for qs, answers in items:
        for _ in range(n_per_fact):
            turns = []
            r = rng.random()
            if r < 0.15:
                g = rng.choice(greet)
                turns += [("u", casual(g[0])), ("b", g[1])]
            elif r < 0.30:
                q2, a2 = rng.choice(items)
                turns += [("u", casual(rng.choice(q2))), ("b", rng.choice(a2))]
            turns += [("u", casual(rng.choice(qs))), ("b", rng.choice(answers))]
            out.append(turns)
    return out, len(items)


if __name__ == "__main__":
    rng = random.Random(0)
    c, n = chats(1, lambda s: s, rng)
    print(n, "facts")
    for t in c[:: max(1, len(c) // 15)]:
        print(t)
