# Inkwave for NumWorks

A blocky 3D remake of **Inkwave**'s Turf War for the NumWorks N0120 calculator. You and 3 bots play
against 4 bots for 3 minutes on Tidewater Plaza. The team that has inked more of the ground at the end wins.

Based on [INKWAVE](https://github.com/jaydendavisnc/inkwave) by Jayden Davis, MIT licence, (c) 2026 Jayden Davis.
This is a rewrite in C, not a port. Only `tools/make_map.mjs` follows Inkwave code (the box and ramp
maths of its `src/world/level.js`). The game uses Inkwave's stage layout (Tidewater Plaza),
names (Spritzer, Swell Roller, Glint Charger, the bot names, team colours) and numbers from its `src/config.js`
(speeds, ink costs, damage, ranges, the 3-minute match, bot reaction times). Inkwave's licence is
copied in `LICENSE-inkwave.txt`. The game credits it on its title and About screens.

## Controls

| Key | Action |
|---|---|
| `,` / √ | walk forward / back (the NumBlocks layout) |
| i / x^y | step left / right (π and x² work too) |
| Left / Right | turn (slow at first, faster when held) |
| Up / Down | look up / down (aims shots, bombs and the charger) |
| OK or EXE | fire (hold) |
| Shift | squid form (hold): swim fast in your own ink, hidden, and refill |
| Alpha | throw a bomb (70 % of your ink) |
| Back | pause, with a map of the stage. Back again quits the match |
| 0 | show or hide the frame counter |

## What's in it

- **Stage**: Tidewater Plaza as a heightfield of 0.5 m columns (`tools/make_map.mjs` turns Inkwave's layout
  boxes into `src/map_data.h`). Buildings are off-limits. Ledges higher than 1 m can be dropped from but
  not climbed, so stairs and ramps matter. Props (benches, lamps, the fountain) are left out.
- **Ink**: a 0.25 m grid on the tops of the columns, 2 bits per cell (21 KB). The score is a count of cells.
- **Weapons**: Spritzer (shooter), Swell Roller (flick, then hold to roll), Glint Charger (hold to charge,
  release to fire). Every kid also has a bomb.
- **Squid form**: 11.8 m/s in your own ink (2.9 on dry ground), refills 42 ink/s, heals faster, hidden from
  enemies. Enemy ink slows you and hurts you, up to 40 HP.
- **Bots**: Chill, Fresh or Fierce (Inkwave's reaction times, aim error and awareness). They pick turf to claim,
  path there with a breadth-first search on a 1 m grid, swim through their own ink, fight, retreat to heal,
  refill and throw bombs.
- **Left out**: online play, 3D character models, the locker, the boss mode, music, specials, jumping and
  wall climbing.

## Rendering

The renderer is software-only. It casts one ray per screen column through the heightfield, front to back,
over the right shoulder of your kid. Each 0.5 m column draws its front face and its top, and the tops are
inked per pixel. The screen is drawn and pushed in 10 strips of 32 columns, each with its own depth buffer, so
nothing is allocated (about 112 KB of static RAM in all, the ink grid included).

What it draws: a sky with clouds, sun and distant hills; the sea with waves, glints and foam on the shore;
brick (herringbone), terrazzo and stone-step floors; stone walls with windows; sun shadows and contact shadows
baked from the stage; kids as billboards seen from the front, side or back, with ink tanks, weapons, squid form
and swim ripples; shots, ink droplets, bomb blasts and charger sights.

**Graphics** (title menu): Fast (160 x 120), Balanced (160 x 240) or Full (320 x 240). On an N0120 the benchmark
(0 on the title screen) measured about 21, 28 and 46 ms a frame, so Full runs at about 22-25 fps. Auto, the default,
starts at Balanced and drops to Fast after 2 seconds under 29 fps. This takes the place of a small and a full build:
the whole app is about 120 KB, so one build with a setting is simpler than two downloads.

The frame counter (top right, on by default; key 0) shows frames per second and the milliseconds each frame
takes to simulate, render and push. The game sleeps the rest of a 33 ms frame, so the counter stays at 30 when
there is headroom. The ms figure shows how much headroom is left.

## Building

```
make build check     # output/app.nwa, output/app.bin (about 120 KB on the calculator)
make dist            # dist/inkwave.nwa, the file Numstore ships (debug info stripped)
make host            # PC check: a whole match with 8 bots, frame times, screenshots in host/out
```

To rebuild the stage from Inkwave's source (unzip `Sources/inkwave-main.zip` first):

```
node tools/make_map.mjs <unzipped>/inkwave-main tidewater preview.png > src/map_data.h
```

`nwlink install-nwa` replaces every app on the calculator, so install Inkwave together with the other apps
you want to keep (Numstore does this for you).
