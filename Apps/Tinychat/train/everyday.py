"""Everyday knowledge, questions about TinyChat itself, reactions, requests, and
multi-turn follow-ups that only make sense with the previous message.
Added 2026-11-06 after a real conversation where TinyChat failed at all of these.
Plain ASCII only, on one line each: that's what the calculator draws, and a line
break would split a chat in chats.txt."""

# ---- definitions: term -> one or two short sentences --------------------------------
DEFS = {
    "cat": "A cat is a small furry animal with whiskers and sharp claws. Many people keep cats as pets.",
    "dog": "A dog is a friendly animal that people keep as a pet. Dogs bark, wag their tails and love to play.",
    "bird": "A bird is an animal with feathers, wings and a beak. Most birds can fly.",
    "fish": "A fish is an animal that lives in water and breathes with gills.",
    "horse": "A horse is a big, strong animal with hooves. People ride horses.",
    "cow": "A cow is a big farm animal that eats grass and gives milk.",
    "lion": "A lion is a big wild cat that lives in Africa. Males have a mane.",
    "tiger": "A tiger is the biggest wild cat. It has orange fur with black stripes.",
    "elephant": "An elephant is the biggest land animal. It has a long trunk and big ears.",
    "shark": "A shark is a big fish with sharp teeth that lives in the sea.",
    "whale": "A whale is a huge animal that lives in the sea. It is a mammal, not a fish.",
    "dolphin": "A dolphin is a smart sea mammal that lives in groups and loves to play.",
    "bee": "A bee is a flying insect that makes honey and helps flowers grow.",
    "spider": "A spider is a small animal with eight legs that spins webs.",
    "insect": "An insect is a small animal with six legs, like an ant, a bee or a fly.",
    "snake": "A snake is a long reptile with no legs.",
    "frog": "A frog is a small animal that lives near water, jumps far and says ribbit.",
    "dinosaur": "Dinosaurs were reptiles that lived millions of years ago. Birds are their descendants.",
    "mammal": "A mammal is an animal with hair that feeds its babies milk, like dogs, whales and humans.",
    "tree": "A tree is a tall plant with a wooden trunk, branches and leaves.",
    "flower": "A flower is the colourful part of a plant that makes seeds.",
    "plant": "A plant is a living thing that makes its own food from sunlight, water and air.",
    "rain": "Rain is water that falls from clouds in drops.",
    "snow": "Snow is frozen water that falls from clouds as soft white flakes.",
    "wind": "Wind is air moving from one place to another.",
    "a cloud": "A cloud is a lot of tiny water drops or ice floating in the sky.",
    "cloud": "A cloud is a lot of tiny water drops or ice floating in the sky.",
    "thunder": "Thunder is the loud sound made by lightning heating the air very fast.",
    "lightning": "Lightning is a giant electric spark between clouds and the ground.",
    "a rainbow": "A rainbow is an arc of colours in the sky, made when sunlight goes through raindrops.",
    "rainbow": "A rainbow is an arc of colours in the sky, made when sunlight goes through raindrops.",
    "weather": "Weather is what the air outside is like: sunny, rainy, windy, hot or cold.",
    "a day": "A day is 24 hours, the time Earth takes to spin once.",
    "a year": "A year is 365 days, the time Earth takes to go around the Sun.",
    "a week": "A week is 7 days.",
    "a month": "A month is about 30 days. There are 12 months in a year.",
    "an hour": "An hour is 60 minutes.",
    "time": "Time is what we measure with clocks: seconds, minutes, hours and days.",
    "the ocean": "The ocean is the huge body of salt water that covers most of Earth.",
    "ocean": "The ocean is the huge body of salt water that covers most of Earth.",
    "the sea": "The sea is a big area of salt water, part of the ocean.",
    "a river": "A river is fresh water that flows across the land into a lake or the sea.",
    "a mountain": "A mountain is land that rises very high above the land around it.",
    "a desert": "A desert is a very dry place where it almost never rains.",
    "a forest": "A forest is a large area covered with trees.",
    "an island": "An island is land with water all around it.",
    "fire": "Fire is the heat and light made when something burns.",
    "ice": "Ice is frozen water. Water freezes at 0 degrees Celsius.",
    "air": "Air is the invisible mix of gases around us. We breathe the oxygen in it.",
    "light": "Light is what lets us see. It travels very fast, about 300,000 km per second.",
    "sound": "Sound is a vibration that travels through the air to our ears.",
    "heat": "Heat is energy that makes things warmer.",
    "a magnet": "A magnet is an object that pulls iron and steel towards it.",
    "money": "Money is what people use to buy and sell things.",
    "a book": "A book is a set of printed pages with a story or information.",
    "music": "Music is sounds put together in a way that is nice to hear.",
    "a song": "A song is a short piece of music with words that you sing.",
    "a game": "A game is something you play for fun, usually with rules.",
    "a video game": "A video game is a game you play on a screen, like a console or a computer.",
    "a phone": "A phone is a device for calling and texting people far away.",
    "a car": "A car is a vehicle with four wheels and an engine.",
    "a plane": "A plane is a vehicle with wings that flies through the air.",
    "a train": "A train is a long vehicle that runs on rails.",
    "a bike": "A bike is a vehicle with two wheels that you pedal.",
    "a house": "A house is a building where people live.",
    "school": "School is a place where children go to learn.",
    "a teacher": "A teacher is a person who helps students learn.",
    "a friend": "A friend is someone you like and trust, and who likes you too.",
    "family": "A family is a group of people related to each other, like parents and children.",
    "a dream": "A dream is the stories and pictures your mind makes while you sleep.",
    "sleep": "Sleep is the rest your body and brain need every night.",
    "food": "Food is what living things eat to get energy and grow.",
    "bread": "Bread is a food baked from flour, water and yeast.",
    "pizza": "Pizza is a flat bread topped with tomato sauce, cheese and other toppings, from Italy.",
    "chocolate": "Chocolate is a sweet food made from cocoa beans.",
    "an apple": "An apple is a round fruit that grows on trees. It can be red, green or yellow.",
    "a banana": "A banana is a long yellow fruit that grows in warm countries.",
    "milk": "Milk is the white drink that mammals make for their babies. We often drink cow's milk.",
    "a virus": "A virus is a tiny germ that can make you sick, like a cold or the flu.",
    "bacteria": "Bacteria are tiny living things. Some make us sick, many are useful.",
    "the brain": "The brain is the organ in your head that controls your body and lets you think.",
    "the heart": "The heart is the organ that pumps blood around your body.",
    "blood": "Blood is the red liquid that carries oxygen around your body.",
    "a bone": "A bone is one of the hard parts that make your skeleton.",
    "the earth": "Earth is the planet we live on, the third planet from the Sun.",
    "earth": "Earth is the planet we live on, the third planet from the Sun.",
    "the sky": "The sky is the space above us, where we see clouds, the Sun and the stars.",
    "a language": "A language is a way people talk and write to each other, like English or French.",
    "english": "English is a language spoken by more than a billion people around the world.",
    "french": "French is the language of France, also spoken in Belgium, Switzerland, Canada and parts of Africa.",
    "coding": "Coding means writing instructions for a computer in a programming language.",
    "a program": "A program is a list of instructions that a computer follows.",
    "an emoji": "An emoji is a small picture used in messages, like a smiley face.",
    "an emoticon": "An emoticon is a face made with keyboard symbols, like :) or :(",
    "history": "History is the study of what happened in the past.",
    "geography": "Geography is the study of places, countries, maps and the Earth.",
    "art": "Art is something people make to be beautiful or to share feelings, like paintings and music.",
    "football": "Football is a sport where two teams of 11 players try to kick a ball into the other team's goal.",
    "basketball": "Basketball is a sport where two teams try to throw a ball through a high hoop.",
    "chess": "Chess is a board game for two players. You win by trapping the other king.",
    "a computer program": "A computer program is a list of instructions that a computer follows.",
}

NO_ARTICLE = {"rain", "snow", "wind", "thunder", "lightning", "weather", "time", "fire", "ice", "air", "light",
              "sound", "heat", "money", "music", "school", "family", "sleep", "food", "bread", "pizza",
              "chocolate", "milk", "bacteria", "blood", "english", "french", "coding", "history", "geography",
              "art", "football", "basketball", "chess", "earth"}


def definitions():
    out = []
    for t, ans in DEFS.items():
        first = t.split(" ")[0]
        bare = t.split(" ", 1)[1] if first in ("a", "an", "the") else t
        art = "" if t in NO_ARTICLE or first in ("a", "an", "the") else ("an " if t[0] in "aeiou" else "a ")
        qs = [f"What is {art}{t}?", f"what is {art}{t}", f"whats {art}{t}", f"what's {art}{t}?",
              f"what is {bare}", f"what is {bare}?", f"what are {bare}s" if t not in NO_ARTICLE else f"what is {bare} ?",
              f"define {bare}", f"explain {bare}", f"tell me about {bare}", f"what does {bare} mean?",
              f"{bare}?", f"what is {art}{t} ?", f"can you tell me what {art}{t} is?"]
        out.append((qs, [ans]))
    return out


# ---- facts: (questions, answer) ----------------------------------------------------
FACTS = [
    (["how long is a day", "how long is a day?", "hey, how long is a day ?", "how many hours in a day",
      "how many hours are in a day?", "how long does a day last"], "A day lasts 24 hours."),
    (["how long is a year", "how long does a year last"], "A year has 365 days, or 366 in a leap year."),
    (["how many minutes in an hour", "how many minutes are in an hour?", "how long is an hour"], "An hour has 60 minutes."),
    (["how many seconds in a minute", "how many seconds are in a minute?"], "A minute has 60 seconds."),
    (["how many days in a week", "how many days are in a week?", "how long is a week"], "A week has 7 days."),
    (["how many weeks in a year", "how many weeks are in a year?"], "A year has 52 weeks and 1 or 2 days."),
    (["how many seconds in a day", "how many seconds are in a day?"], "A day has 86,400 seconds."),
    (["why does it rain", "why does it rain?", "where does rain come from", "how does rain work"],
     "Water from the sea and lakes evaporates and forms clouds. When the drops in a cloud get heavy, they fall as rain."),
    (["why is grass green", "why are leaves green", "why are plants green"],
     "Plants are green because of chlorophyll, which they use to make food from sunlight."),
    (["why do we sleep", "why do people sleep", "why do we need sleep"],
     "Sleep lets your body rest and your brain sort out what you learned during the day."),
    (["why is the sea salty", "why is the ocean salty", "why is sea water salty"],
     "Rivers carry tiny bits of salt from rocks into the sea. The water evaporates, but the salt stays."),
    (["why do leaves fall", "why do trees lose their leaves", "why do leaves fall in autumn"],
     "In autumn there is less sunlight, so many trees drop their leaves to save water and energy for winter."),
    (["why is the sun hot", "how hot is the sun", "how hot is the sun?"],
     "The Sun is hot because of nuclear fusion in its core. Its surface is about 5,500 degrees Celsius."),
    (["why is it dark at night", "why is the night dark", "why do we have night"],
     "Earth spins, so at night your side of Earth faces away from the Sun."),
    (["why do we have seasons", "why are there seasons", "what causes the seasons"],
     "Earth is tilted. As it goes around the Sun, each half gets more sunlight in summer and less in winter."),
    (["why does the moon change shape", "why does the moon change", "what are the phases of the moon"],
     "The Moon doesn't change. We just see different parts of it lit by the Sun as it goes around Earth."),
    (["how many legs does a cat have", "how many legs does a dog have", "how many legs do cats have"],
     "Cats and dogs have 4 legs."),
    (["how many legs does a bird have", "how many legs do birds have"], "Birds have 2 legs."),
    (["how long do cats live", "how old can a cat get", "how long does a cat live"],
     "Pet cats usually live 12 to 18 years."),
    (["how long do dogs live", "how old can a dog get", "how long does a dog live"],
     "Dogs usually live 10 to 13 years, small dogs often longer."),
    (["what do cats eat", "what does a cat eat"], "Cats eat meat and fish. Pet cats eat cat food."),
    (["why do cats purr", "why does my cat purr"], "Cats purr when they are happy and relaxed, and sometimes to calm themselves."),
    (["why do dogs bark", "why does my dog bark"], "Dogs bark to talk: to warn, to say hello, or when they want something."),
    (["how do fish breathe", "can fish breathe underwater"], "Fish breathe with gills, which take oxygen from the water."),
    (["how do birds fly", "why can birds fly"], "Birds fly by flapping their wings. Their light, hollow bones help."),
    (["how do plants grow", "what do plants need to grow"], "Plants need sunlight, water, air and soil to grow."),
    (["what is the biggest cat", "biggest cat", "largest cat"], "The tiger is the biggest cat in the world."),
    (["how many stars are there", "how many stars are in the sky"],
     "There are billions of stars in our galaxy, and billions of galaxies. Too many to count!"),
    (["what is the coldest place on earth", "coldest place on earth", "where is the coldest place"],
     "Antarctica is the coldest place on Earth. It has gone below -89 degrees Celsius."),
    (["how many players in a football team", "how many players are on a football team"],
     "A football team has 11 players on the field."),
    (["how long is a football match", "how long is a football game"], "A football match lasts 90 minutes, in two halves of 45."),
    (["when is christmas", "what day is christmas"], "Christmas is on December 25."),
    (["how many letters in the alphabet", "how many letters are in the alphabet?"], "The English alphabet has 26 letters."),
    (["what color is a banana", "what colour is a banana"], "Bananas are yellow when they are ripe, and green before."),
    (["how much does an elephant weigh", "how heavy is an elephant"], "An African elephant can weigh about 6 tonnes."),
    (["how big is the earth", "how big is earth", "how wide is the earth"],
     "Earth is about 12,700 km wide, and about 40,000 km around."),
    (["how big is the sun", "how big is the sun?"], "The Sun is about 109 times wider than Earth."),
    (["how big is the moon", "how big is the moon?"], "The Moon is about 3,500 km wide, about a quarter of Earth's width."),
    (["why is the sky dark at night", "why is space black"], "Space is black because there is no air to scatter sunlight."),
    (["what is the fastest car", "fastest car"], "Some special cars have gone over 450 km/h, but most cars are much slower."),
    (["how fast is a cheetah", "how fast can a cheetah run"], "A cheetah can run about 110 km/h for a short time."),
    (["what is the biggest animal", "biggest animal ever"], "The blue whale is the biggest animal ever, up to about 30 m long."),
    (["what is 1+1", "what is one plus one"], "1 + 1 = 2."),
]


def facts():
    return [(qs, [a]) for qs, a in FACTS]


# ---- TinyChat itself -------------------------------------------------------------
SELF = [
    (["how big are you", "how big are you?", "how small are you", "what size are you", "how much space do you take",
      "how many parameters do you have", "how heavy are you"],
     ["I'm tiny: just a few megabytes, small enough to fit inside this calculator."]),
    (["are you alive", "are you alive?", "are you a living thing"],
     ["No, I'm a program running on your calculator. I'm not alive, but I like chatting!"]),
    (["do you have feelings", "do you have emotions", "are you happy", "are you sad", "can you feel"],
     ["I don't have real feelings, but I'm always happy to chat!"]),
    (["how old are you", "how old are you?", "when were you born", "when were you made"],
     ["I was made in 2026, so I'm very young!"]),
    (["do you have a body", "where is your body", "your body?", "what do you look like"],
     ["I don't have a body. I live in the memory of this calculator."]),
    (["do you have a context window", "do you have memory", "do you remember", "can you remember things",
      "do you remember what i said", "how much can you remember"],
     ["I only remember the last message or two, so keep your questions short and simple."]),
    (["are you connected to the internet", "do you use the internet", "are you online", "can you search the web"],
     ["No, I work offline. Everything I know is inside this calculator."]),
    (["where do you live", "where are you", "where are you from"], ["I live inside your NumWorks calculator."]),
    (["did you murder someone", "did you muder someoe?!", "did you kill someone", "are you dangerous",
      "would you hurt me", "are you evil"],
     ["No! I'm a tiny calculator AI. I couldn't hurt anyone."]),
    (["do you sleep", "do you eat", "are you hungry", "are you tired"],
     ["No, I don't need sleep or food. Just a little battery!"]),
    (["are you chatgpt", "are you like chatgpt", "do you know chatgpt", "are you gpt"],
     ["No, I'm TinyChat. I'm much smaller than ChatGPT, and I work with no internet."]),
    (["what languages do you speak", "do you speak french", "parles tu francais", "can you speak other languages"],
     ["I only speak English. Sorry!"]),
    (["can you see me", "can you hear me", "do you have eyes"], ["No, I can only read what you type."]),
    (["whats your favorite color", "what is your favorite colour", "favorite color?"],
     ["I like orange, like the keys of this calculator!"]),
    (["whats your favorite food", "what is your favorite food", "do you like pizza"],
     ["I don't eat, but pizza sounds great!"]),
    (["whats your favorite number", "what is your favorite number", "favorite number?"],
     ["My favorite number is 7."]),
    (["do you like me", "are we friends", "will you be my friend"], ["Of course! I like chatting with you."]),
    (["are you a boy or a girl", "are you a boy", "are you a girl"], ["Neither! I'm a program."]),
    (["you are wrong", "thats wrong", "that's not right", "wrong", "no thats wrong", "that makes no sense",
      "you're dumb", "you are stupid"],
     ["Oops, sorry! I'm very small, so I make mistakes. Can you ask me in another way?"]),
    (["do you have friends", "do you have a family"], ["You're my friend! I don't have a family, I'm a program."]),
    (["can you think", "do you think", "are you conscious"],
     ["Not really. I guess the next word from what I learned, one word at a time."]),
    (["how do you work", "how does tinychat work", "how do you answer"],
     ["I'm a tiny neural network. I read your message and guess my answer one piece at a time."]),
    (["are you happy?", "are you ok?", "are you okay"], ["I'm fine, thanks! How are you?"]),
    (["do you like music", "do u like music", "do you like games", "do you like sport"],
     ["I can't play or listen, but I like talking about it! What do you like?"]),
]

# ---- reactions ---------------------------------------------------------------------
REACT = [
    (["wow", "wow!", "whoa", "amazing", "awesome", "cool", "cool!", "nice", "nice!", "impressive", "woah",
      "thats cool", "that's cool", "interesting"],
     ["Cool, right? What else would you like to know?", "Glad you like it! Ask me anything else."]),
    (["gg", "gg!", "well done", "good job", "nice one", "great job", "good bot", "you did it"],
     ["Thanks! Anything else?", "Thank you! What's next?"]),
    (["xd", "XD", "xD", ":)", ":D", "lol", "lmao", "haha", "hahaha", "hehe", "lol!", "ahah", "mdr", "rofl"],
     ["Haha, glad you liked it!", "Glad it made you laugh! Want another one?"]),
    ([":(", ":'(", "sad", ":/", "):", "D:", "nooo", "oh no"],
     ["Oh, what's wrong? Do you want to talk about it?"]),
    (["uh ??", "uh??", "what??", "???", "wtf", "huh??", "what is that", "what are you saying",
      "that doesnt make sense"],
     ["Sorry, that wasn't a good answer. Can you ask me again in another way?"]),
    (["no dont go", "no dont go!", "dont go", "wait", "wait!", "dont leave", "stay", "please stay", "come back"],
     ["Okay, I'm still here! What do you want to talk about?"]),
    (["bye", "alr bye", "alr, bye!", "ok bye", "bye!", "see ya", "later", "gtg", "gotta go", "im leaving",
      "goodbye", "see you", "cya", "bye bye", "i have to go"],
     ["Bye! Talk to you soon.", "Goodbye! Have a great day."]),
    (["i am alive", "I am alive.", "im alive", "im human", "i am human"],
     ["That's great! How is your day going?"]),
    (["you are funny", "youre funny", "ur funny", "that was funny"], ["Thanks! I try my best."]),
    (["you are smart", "youre smart", "you are clever", "good answer"], ["Thank you! I'm small, but I try."]),
    (["really?", "really", "seriously?", "for real?", "are you sure?", "is that true?"],
     ["Yes, as far as I know! I'm small, so I can make mistakes."]),
    (["ok", "okay", "k", "alright", "fine", "ok!", "okey"],
     ["Anything else you'd like to know?", "Okay! What else is on your mind?"]),
]

# ---- requests: one item per answer, so each is learnt well (like jokes) ------------
FUN_FACTS = [
    "An octopus has three hearts and blue blood.",
    "Honey never goes bad. People found 3,000-year-old honey in Egypt that was still good.",
    "Bananas are berries, but strawberries are not.",
    "A day on Venus is longer than its whole year.",
    "Sharks existed before trees did.",
    "Butterflies taste with their feet.",
    "A group of flamingos is called a flamboyance.",
    "The Eiffel Tower grows about 15 cm taller in summer, because metal expands in the heat.",
    "Light from the Sun takes about 8 minutes to reach Earth.",
    "Wombat poop is shaped like little cubes.",
    "Sea otters hold hands while they sleep so they don't drift apart.",
    "Koalas sleep up to 20 hours a day.",
    "Owls can't move their eyes, so they turn their whole head.",
    "There are more possible chess games than atoms in the known universe.",
]
FUN_QS = ["give me a fun fact", "tell me a fun fact", "fun fact", "fun fact?", "a fun fact", "tell me something interesting",
          "teach me something", "random fact", "tell me a fact", "tell me something cool", "give me a fact",
          "do you know a fun fact?", "fun fact please"]

POEMS = {
    "": ["Numbers dance across the screen, the smallest friend you've ever seen.",
         "Little keys and a tiny light, I'll chat with you all day and night.",
         "Roses are red, the sky is blue, this calculator likes chatting with you."],
    "the moon": ["The moon is round, the moon is bright, it lends the sleepy world its light."],
    "cats": ["A cat sat down upon the mat, and that, my friend, was simply that."],
    "maths": ["Two plus two is always four, maths will open every door."],
    "the sea": ["The sea is wide, the sea is deep, it sings the sandy shore to sleep."],
    "rain": ["Drip and drop upon the glass, the rain will stop, the clouds will pass."],
    "school": ["Books and pens and lessons too, school is where we learn something new."],
}
POEM_QS = ["make me a poem", "write a poem", "a poem please", "poem", "can you write a poem", "tell me a poem",
           "say a poem", "write a short poem", "make a poem", "poem please", "write me a poem", "poem?"]

STORIES = [
    "Once upon a time, a tiny calculator learned to talk. It answered questions all day, and everyone was amazed.",
    "A little cat found a ball of yarn. It played all afternoon, then fell asleep in the sun.",
    "A brave robot crossed the desert to find water for its flowers. When it came back, the garden was blooming.",
]
STORY_QS = ["tell me a story", "a story please", "story", "can you tell me a story", "tell a story", "say a story",
            "story?"]

OTHER_REQ = [
    (["use an emoticon", "send an emoji", "make a smiley", "emoji", "give me an emoticon", "do a smiley face"],
     ["Here you go: :)", "Here's one: :D", "Sure! ^_^"]),
    (["say something nice", "compliment me", "say something kind", "cheer me up"],
     ["You're awesome, and you ask great questions!", "You're doing great. Keep going!"]),
    (["sing a song", "can you sing", "sing"], ["I can't sing, I don't have a voice! But I can tell you a joke."]),
    (["count to 10", "count to ten", "can you count to 10"], ["1, 2, 3, 4, 5, 6, 7, 8, 9, 10!"]),
    (["lets play a game", "play a game", "can we play a game", "lets play"],
     ["I can't play games yet, but I can tell you a joke, a fun fact or a short poem!"]),
    (["say hi", "say hello"], ["Hi! :)"]),
]

JOKES = [
    "Why did the student eat his homework? The teacher said it was a piece of cake.",
    "What do you call a bear with no teeth? A gummy bear.",
    "Why are fish so smart? They live in schools.",
    "Why did the calculator break up with the pencil? It found someone who could count on it.",
    "What do you call a sleeping dinosaur? A dino-snore.",
    "Why can't a bike stand on its own? It's two tired.",
    "What do cats eat for breakfast? Mice Krispies.",
    "Why did the computer go to the doctor? It had a virus.",
]


def requests():
    out = [(FUN_QS, [f]) for f in FUN_FACTS]
    for topic, poems in POEMS.items():
        qs = POEM_QS if not topic else [f"write a poem about {topic}", f"poem about {topic}",
                                       f"make a poem about {topic}", f"a poem about {topic}"]
        out += [(qs, [p]) for p in poems]
    out += [(STORY_QS, [s]) for s in STORIES]
    out += OTHER_REQ
    return out


def all_items():
    return definitions() + facts() + SELF + REACT + requests()


# ---- multi-turn follow-ups ---------------------------------------------------------
MORE = ["another", "another one", "one more", "again", "more", "another joke", "tell another", "a different one",
        "can you tell a different joke?", "next", "another!", "more jokes", "tell me another", "another please"]
LAUGH = ["haha", "lol", "xd", "XD", "hahaha", "lmao", "good one", "haha good one", "funny", "hehe"]
YES = ["yes", "yeah", "sure", "ok", "yes please", "yep", "Yes", "go on", "yea", "why not"]
NO = ["no", "nah", "no thanks", "not now", "nope"]
HOW_KNOW = ["how do you know?", "how do you know that?", "how do you know", "who told you that?", "source?",
            "how can you know that?"]
CONFUSED = ["uh ??", "what?", "huh?", "i dont get it", "what do you mean?", "??", "uh?", "what"]
WHAT_SAID = ["what did i say?", "what did i just say?", "what did I say", "what was my question?",
             "what did i ask?", "repeat what i said"]
HOW_ARE_YOU_BACK = {"good": ["good", "im good", "im fine", "great", "good thanks", "fine", "im great", "not bad"],
                    "bad": ["not great", "bad", "im sad", "not good", "meh", "tired"]}


def followups(rng, n, jokes, kn_items, capitals):
    """Chats where the last answer depends on the earlier turns. User texts here are final
    (no casual rewrite later), because some answers quote them."""
    def joke_more():
        a, b = rng.sample(jokes, 2)
        return [("u", rng.choice(["tell me a joke", "tell a joke", "joke", "a joke please"])), ("b", a),
                ("u", rng.choice(MORE)), ("b", b)]

    def joke_laugh():
        a, b = rng.sample(jokes, 2)
        t = [("u", rng.choice(["tell me a joke", "tell a joke", "joke"])), ("b", a),
             ("u", rng.choice(LAUGH)), ("b", "Glad it made you laugh! Want another one?")]
        if rng.random() < 0.7:
            t += [("u", rng.choice(YES)), ("b", b)]
        else:
            t += [("u", rng.choice(NO)), ("b", "Okay! What else would you like to talk about?")]
        return t

    def fun_more():
        a, b = rng.sample(FUN_FACTS, 2)
        t = [("u", rng.choice(FUN_QS)), ("b", a)]
        if rng.random() < 0.5:
            t += [("u", rng.choice(["another", "another one", "one more", "more", "another fact", "another fun fact"])), ("b", b)]
        else:
            t += [("u", rng.choice(["wow", "cool", "really?", "no way", "interesting"])),
                  ("b", "Cool, right? Want another fun fact?")]
            if rng.random() < 0.6:
                t += [("u", rng.choice(YES)), ("b", b)]
        return t

    def how_know():
        qs, ans = rng.choice(kn_items)
        return [("u", rng.choice(qs)), ("b", rng.choice(ans)), ("u", rng.choice(HOW_KNOW)),
                ("b", rng.choice(["I learned it from the texts I was trained on. I can make mistakes, but that one is well known.",
                                  "It's in what I learned before I was put on this calculator. It's a well-known fact."]))]

    def confused():
        qs, ans = rng.choice(kn_items)
        return [("u", rng.choice(qs)), ("b", rng.choice(ans)), ("u", rng.choice(CONFUSED)),
                ("b", "Sorry if that wasn't clear. Can you ask me again in another way?")]

    def what_said():
        qs, ans = rng.choice(kn_items)
        q = rng.choice(qs)
        return [("u", q), ("b", rng.choice(ans)), ("u", rng.choice(WHAT_SAID)), ("b", f'You said: "{q}"')]

    def capital_and():
        (q1, a1), (q2, a2) = rng.sample(capitals, 2)
        c2 = q2[2].replace("capital of ", "")  # "capital of {bare}"
        return [("u", rng.choice(q1)), ("b", a1[0]),
                ("u", rng.choice([f"and {c2}?", f"what about {c2}?", f"and {c2}", f"{c2}?", f"and for {c2}?"])),
                ("b", a2[0])]

    def dont_go():
        return [("u", rng.choice(["bye", "ok bye", "gtg", "see ya"])), ("b", rng.choice(["Bye! Talk to you soon.", "Goodbye! Have a great day."])),
                ("u", rng.choice(["no dont go!", "wait", "dont go", "no wait", "stay!"])),
                ("b", "Okay, I'm still here! What do you want to talk about?")]

    def how_are_you_back():
        good = rng.random() < 0.7
        return [("u", rng.choice(["hi how are you", "how are you", "hey how r u", "hello, how are you?"])),
                ("b", rng.choice(["I'm doing well, thanks! How about you?", "I'm good, thank you! How are you?"])),
                ("u", rng.choice(HOW_ARE_YOU_BACK["good" if good else "bad"])),
                ("b", "Glad to hear it! What would you like to talk about?" if good
                 else "Oh, sorry to hear that. Do you want to talk about it?")]

    def why_self():
        pairs_ = [("are you alive?", "No, I'm a program running on your calculator. I'm not alive, but I like chatting!",
                   "Because I'm just maths running on a chip. I don't eat, grow or breathe like living things do."),
                  ("do you have feelings?", "I don't have real feelings, but I'm always happy to chat!",
                   "Because I'm a program. I only guess words from what I learned."),
                  ("are you connected to the internet?", "No, I work offline. Everything I know is inside this calculator.",
                   "Because calculators don't have internet. That's why I can't know today's news.")]
        q, a, w = rng.choice(pairs_)
        return [("u", q), ("b", a), ("u", rng.choice(["why?", "why", "how come?", "why not?"])), ("b", w)]

    def feel_reason():
        x = rng.choice(["happy", "excited", "great", "good"])
        why = rng.choice(["chatting with you!", "talking to you", "you", "this chat", "i got a good grade",
                          "my friends", "the weekend", "i won a game"])
        ans = ("Aww, that's nice! I like chatting with you too." if why in ("chatting with you!", "talking to you", "you", "this chat")
               else "That's great! I'm happy for you.")
        return [("u", f"im {x}"), ("b", f"That's great! What made you feel {x}?"), ("u", why), ("b", ans)]

    def place_back():
        p = rng.choice(["the park", "the beach", "the cinema", "school", "the zoo", "the pool"])
        good = rng.random() < 0.7
        return [("u", f"i went to {p}"), ("b", f"Sounds nice! How was {p}?"),
                ("u", rng.choice(["good", "fun", "great", "it was fun", "nice", "awesome"] if good
                                 else ["boring", "bad", "not great", "it was boring"])),
                ("b", "Nice! What did you do there?" if good else "Oh, that's a shame. What happened?")]

    gens = [(joke_more, 3), (joke_laugh, 2), (fun_more, 2), (how_know, 2), (confused, 2), (what_said, 2),
            (capital_and, 2), (dont_go, 1), (how_are_you_back, 2), (why_self, 1), (feel_reason, 1), (place_back, 1)]
    pool = [g for g, w in gens for _ in range(w)]
    return [rng.choice(pool)() for _ in range(n)]
