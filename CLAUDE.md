# Almanac

Pebble watchface built with Pebble SDK 3. Displays the date, the time, the next calendar
event (ICS feed), and weather (condition icon, current temperature, today's high and low),
plus an optional top-left instrument (steps, heart rate, or battery).

Almanac is a fork of [Percival](https://github.com/barefootford/Percival) by Andrew Ford,
redesigned around the time / next-event / weather blocks. It carries its own UUID
(`8841834c-a447-43e2-b564-1f566e017987`) and is not yet published in the Pebble store.
Percival's own store listing is https://apps.repebble.com/2799cd581c2a4bbbade7f3da — do not
reuse its UUID.

## Build

```
pebble build
pebble install --emulator emery   # Time 2
pebble install --phone <IP>       # direct WebSocket (needs phone IP from Rebble app)
pebble install --cloudpebble      # via CloudPebble proxy — no IP needed, requires Developer Connection enabled in the Rebble app
```

Run `pebble clean` when adding or removing messageKeys in package.json — the build uses generated code that becomes stale otherwise.

## Structure

- `src/c/main.c` — watchface C code (UI, tick handler, persistent storage, weather/event message handling, `#ifdef DEMO` stub block for screenshots)
- `src/pkjs/index.js` — companion JS (geolocation, Open-Meteo weather API, ICS calendar fetch)
- `src/pkjs/ics.js` — pure ICS parser (VEVENT + DAILY/WEEKLY recurrence); testable standalone with `node`
- `src/pkjs/config.js` — Clay settings page config
- `package.json` — app metadata, message keys, font resources

## Key conventions

- Weather polling interval is defined as `WEATHER_POLL_MINUTES` in both `main.c` and `index.js` — keep them in sync
- Persistent storage keys: `SETTINGS_KEY = 1` (settings, append-only struct), `WEATHER_KEY = 3` (weather cache, ints), `EVENT_KEY = 4` (next-event cache). Key 2 held the v1.x string weather cache and is deleted on first run
- The condition enum (0 clear … 6 storm) is shared between `conditionFromWmo()` in `index.js` and `enum Condition` in `main.c` — keep them in sync
- The language values (0 English … 6 Dutch) are shared between the `Language` select in `config.js` and `enum Lang` in `main.c` — keep them in sync. All localized UI strings live in the `DAYS`/`STR_*` tables in `main.c`; accented glyphs must stay within the `À-ÿ` range covered by the font characterRegex in `package.json`
- Temperature unit (`TempUnit`) and calendar feed (`IcsUrl`) are Clay settings; the JS reads them from localStorage at fetch time and re-fetches on `webviewclosed` so changes apply immediately

## Marketing screenshots

Always use the `/marketing-screenshots` skill when generating or updating screenshots for README.md. It defines the full stubbing procedure (settings, weather, time, date, AppMessage disabling, emulator flags, and revert steps).

## Development workflow

After Claude implements a new feature, Claude always verifies the feature visually before considering it done:

1. `pebble clean && pebble build`
2. `pebble install --emulator emery`
3. `pebble screenshot --emulator emery` — capture a screenshot
4. Read the screenshot and evaluate whether the feature looks correct
5. Iterate if anything is broken or doesn't look right

Do not assume a feature works just because it compiles.

## Troubleshooting

### Emulator stuck at 100% CPU / `ConnectionResetError` on install

The SPI flash image occaisonally corrupts. The fix is to delete it so pebble-tool decompresses a fresh copy on next launch:

Replace `<platform>` with the emulator target (e.g. `emery`, `basalt`).
```
# macOS
rm "$HOME/Library/Application Support/Pebble SDK/4.9.148/<platform>/qemu_spi_flash.bin"
# Linux
rm "$HOME/.local/share/pebble-sdk/<sdk-version>/<platform>/qemu_spi_flash.bin"
```
