# Clock

A big 24-hour clock with the date, for the NumWorks N0120 (EADK, C).

## What the calculator allows

- **No real-time clock for apps.** EADK (`eadk.h`, the one nwlink builds against, and Epsilon's own
  `epsilon/eadk/include/eadk/eadk.h`) has no time or date call, and Epsilon 26 has no clock or date setting.
- **`eadk_timing_millis()`** counts milliseconds since the calculator started (Ion: "on the device, epoch is the
  boot time"). It keeps counting while you are in other apps, but it is driven by SysTick, which stops while the
  calculator sleeps (STOP mode), so **it pauses while the calculator is off**, and starts again from 0 after a reset.
- **Storage:** EADK has no file calls, but the app can keep a file in Epsilon's file list (where Python scripts
  live), the same way Rollmark and Fivefold save (`src/save.c`, shared with them). It survives leaving the app and
  turning the calculator off, and is lost on a reset (which deletes the app too).

## So the app

- asks for the time and date the first time (digits or Up/Down, Left/Right to move; OK starts at 00 seconds, so
  press it as the minute turns), then shows `time set + (timer now - timer then)`;
- saves (time set, timer then) in `clock.sav`, so it carries on when you come back while the calculator stayed on;
- after it comes back from a save it says so for 20 seconds: if the calculator was off in between, it is behind by
  that long. Up/Down nudge one minute, 0 rounds to :00, OK sets it again;
- the restart of the timer (it went back) is caught and opens the set screen.

## Build

```
make build check dist   # output/app.nwa, dist/clock.nwa
make host               # PC test: calendar 2000-2100, setting, counting, saving, coming back, random keys; host/out/*.ppm
make assets             # regenerate src/assets.h and src/icon.png (Pillow, DejaVu Sans Bold)
```

Sizes: app.bin about 61 KB (fonts about 45 KB), under 1 KB of RAM of its own, no heap.
Font: DejaVu Sans Bold (Bitstream Vera licence).
