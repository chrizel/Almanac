---
name: marketing-screenshots
description: Generate curated marketing screenshots for README.md by stubbing watchface data in the emulator
disable-model-invocation: false
---

Generate the curated marketing screenshots shown in README.md.

Since v2.0 all stubbing goes through the `#ifdef DEMO` block at the top of `src/c/main.c` —
no scattered edits. Enabling `DEMO` injects a fake weather cache, event cache, accent color,
time, date, steps, and battery, and disables AppMessage so the JS companion cannot overwrite
the stubs.

## For each screenshot variant

Read the HTML comments in README.md above each `<img>` tag — they define the exact accent
color, time, date, steps, battery, weather (temp/high/low, condition, sunrise/sunset), and
event (day, start, title, location) for that variant.

### 1. Enable DEMO and set the variant's values (main.c)

Uncomment `#define DEMO` and edit the `DEMO_*` defines to match the variant:

```c
#define DEMO

#define DEMO_ACCENT GColorBlue          // event accent color
#define DEMO_TIME_TEXT "14:28"          // displayed time
#define DEMO_NOW_MIN 868                // same time in minutes since midnight
#define DEMO_DOW "SATURDAY"             // uppercase weekday
#define DEMO_ISO_DATE "2026-08-15"      // ISO date line
#define DEMO_TODAY 20260815             // must match DEMO_ISO_DATE (event validity)
#define DEMO_STEPS 4800
#define DEMO_BATTERY 80
#define DEMO_TEMP 84
#define DEMO_HIGH 91
#define DEMO_LOW 67
#define DEMO_CONDITION COND_CLEAR       // COND_CLEAR/PARTLY/OVERCAST/FOG/RAIN/SNOW/STORM
#define DEMO_SUNRISE_MIN 381            // minutes since midnight
#define DEMO_SUNSET_MIN 1234
#define DEMO_EVENT_START_MIN 900
#define DEMO_EVENT_DAY 0                // 0 today · 1 tomorrow · -1 none (empty center)
#define DEMO_EVENT_TITLE "Design Review"
#define DEMO_EVENT_LOCATION "Office HQ, Room B2"
```

Rules the face applies automatically (no extra stubbing needed):

- `DEMO_NOW_MIN` outside sunrise–sunset → night: sun triangle hidden, clear → moon icon.
- `DEMO_EVENT_DAY 1` → "TOMORROW · H:MM" instead of a countdown.
- A today-event whose start is more than 5 minutes past `DEMO_NOW_MIN` hides the block.

### 2. Build, install, and capture

```sh
pebble build
pebble install --emulator emery
pebble screenshot --emulator emery
```

If the emulator runs headless (no X/Wayland), prefix the install with
`SDL_VIDEODRIVER=dummy`.

Move the resulting `pebble_screenshot_*.png` to `screenshots/` with the correct name.

### 3. Verify

Read the saved screenshot and compare it against the README comment's values. Flag any
differences.

### 4. Repeat or revert

Apply the next variant's defines and repeat. After capturing all variants, revert `main.c`
with `git checkout src/c/main.c` (the committed file has `#define DEMO` commented out and
the workday defaults in place).
