/* PC check of the built-in helpers: prints each question and what the helper says
 * ("-> model" when it leaves the question to the AI). */
#include <stdio.h>
#include "../src/helpers.h"
static const char *Q[] = {
  "what is the capital of France ?", "capital of japan", "What's Germany's capital?", "capital of the united states",
  "what is the capital of Atlantis?", "2+2", "what is 12 times 7?", "what's 15% of 80", "square root of 2",
  "sqrt(16)+1", "5 squared", "2^10", "100/3", "1/0", "what is 3.5 * 2 ?", "-(4-10)*2", "2 pi",
  "is 97 prime?", "is 91 a prime number", "factor 360", "prime factors of 1001",
  "10 km in miles", "convert 5 feet to cm", "100 f to c", "37 degrees c in f", "how many seconds in a day",
  "how many cm in an inch", "how many days are in a week", "1.5 kg to pounds", "5 km to kg",
  "why is the sky blue?", "how many planets are there?", "who painted the mona lisa", "what is a computer?",
  "what is gravity", "who was einstein", "tell me a joke", "how tall is mount everest", "what is the highest mountain?",
  "hi, how are you ?", "I love the blue sky", "what should I eat for dinner?", "who are you?",
  "what is 2 + 2 and why", "what time is it?", "is a tomato a fruit?", "what is the speed of light",
  "how far is the moon?", "what is pi", "who invented the telephone?", "my dog is 3 years old",
  0};
int main(void) {
  char out[256];
  for (int i = 0; Q[i]; i++) printf("%-36s %s\n", Q[i], helper_reply(Q[i], out, sizeof out, 12345 << 8) ? out : "-> model");
  return 0;
}
