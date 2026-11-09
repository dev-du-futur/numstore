# Rollmark

A number-rolling game for the NumWorks N0120, inspired by Awe's RNGidle.

- Each roll draws a number the RNGidle way: a 0..999,999 seed, then every digit has a 1-in-100 chance to grow
  another; 1 roll in 20 is negative and 1 in 100 gets decimals (each further decimal 1 in 5).
- Patterns earn badges (53: endings, runs, steps, mirrors, primes, powers, Fibonacci, growth, fractions...).
  A badge pays 10 EP times its odds (1 in 500 pays 5,000 EP). In a family only the best badge pays.
- Total EP gives the tier, with RNGidle's names: Trashest (no badge), Trash, Common ... Mythic, Unreal,
  Aberration, Otherworldly, Cataclysmic, Divine, Transcendent; 404 is Error and 666 Devilish.
- Rolls come from a bank: 100 to start, one more every 2 seconds. OK generates (hold to keep going); Left/Right
  or keys 1-4 pick x1, x10, x100 or x1000 (shown in the footer and on the button). A bulk roll follows RNGidle's
  flow: a grid of cards turns over, the rare rolls (Mythic or better, at most 10, else the best roll) folded as
  "?" cards. OK reveals the outlined card (a folded one with its full reveal), EXE unfolds the rare rolls one by
  one, rarest last, with NEXT. From a roll opened from the grid, Back (or BACK TO THE BULK ROLL) returns to it;
  Back in the grid is done. On the roll view, EXE goes back to the last bulk's grid.
- Glim's shop, after RNGidle's (Glim, the orb shopkeeper, floats above the wares, greets, thanks and teases, and
  tells the outlined ware; every item has its own icon, drawn by tools/art.py): EP made by rolls is spent there (a save from before the shop starts with all its
  EP to spend). Six wares, new ones every 5 minutes, cheaper ones more often (weight price^-0.35):
  luck potions I-X (x1.1 for 2 rolls up to x5 for 25,000: each roll is the best of a few draws), rarity potions
  (the next roll is Mythic, Unreal, Aberration or Otherworldly or better) and RNGidle's number items (Roulette,
  Slot and Digit Splitters, Digit Swap, Digit Mirror, Gravity Potion, Grinder, Edge of Infinity). Left/Right
  switches between the shop and the bag; OK buys or uses. A number item works on a roll opened from the grid
  (EXE uses the one picked with Left/Right in the footer) or, from the bag, on the roll on view; one per number,
  and the EP it makes or loses counts. Number items are priced in rolls (Roulette Splitter = 10 average rolls,
  Edge of Infinity about 1,000): at RNGidle's own ratio to luck they cost hundreds of hours of rolling.
- Badge book and records. Progress is saved in the calculator's file list (rollmark.sav, see src/save.h).

The badges, their names and prices are Rollmark's own: no code, names or data from rngdle.com or from the
engine RNGidle's Websim version vendors. Badge prices and tier limits come from exact counts over every seed
(`tools/odds.c`), so the odds the book shows are true.

## Build

    make build check   # output/app.nwa, and app.bin to check it links for the device
    make host          # PC test: rules, primes, 2M-roll odds, every screen, random keys; screenshots in host/out
    make odds          # reprice badges and place tiers: writes src/odds.h (after changing a badge)
    make prices        # price the shop with RNGidle's rules, find the rarity potions' rolls: src/prices.h (~5 min)
    make dist          # dist/rollmark.nwa, the file Numstore ships
    make assets        # rebuild src/assets.h and src/icon.png (Python + Pillow, DejaVu Sans Bold)

`numstore.json` puts the app in the Numstore catalog (Store/scripts/build-catalog.js).
