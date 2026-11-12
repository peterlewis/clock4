# Precision Clock Mk IV: extended firmware

mitxela's firmware for the [Precision Clock Mk IV](https://mitxela.com/projects/precision_clock_mk_iv), with new display modes, a settings menu on the clock, better holdover when GPS is lost, timing telemetry, and fixes.

**Time board 0.0.5 · date board 0.0.2**, released by date while in development. Each image also carries its build time.

**[⬇ Latest release](https://github.com/peterlewis/clock4/releases/latest)** · **[Install](#install)** · **[Manual](docs/firmware-additions.md)** · **[▶ Try it in your browser](https://pcc.ptrl.ws/)**

> [!NOTE]
> Independent work. Not affiliated with mitxela, and not part of his releases. The official firmware is [mitxela/clock4](https://github.com/mitxela/clock4); the official documentation is [here](https://mitxela.com/projects/precision_clock_mk_iv/docs).

> [!WARNING]
> Draft firmware, used at your own risk. It runs on my own clock and every change is tested in an emulator first, but that one clock is all the hardware testing it has had. The bootloader is untouched, so stock is always one file copy away.

## How this firmware differs from stock

What improves the clock is on by default: holdover, brightness equalisation, the timing sentence and the watchdog. What changes what it shows is opt-in: the new display modes and the chimes. Set any key to `off` in `config.txt` to return that part to stock.

| | Stock (0.0.4 / 0.0.1) | This firmware |
|---|---|---|
| Display modes | Calendar, timezone, countdown, text, satellite and diagnostic modes | The same, plus sun, moon, twilight, grid locator, position, sidereal time, solar time, a second timezone, star transits and oscillator stability |
| Changing a setting | Edit `config.txt` on a computer | Also a two-button menu on the clock, saved across power-off |
| GPS lost | Sub-second digits are dashed after fixed times | Each digit fades as the measured uncertainty reaches it, and the clock holds the frequency it measured while locked |
| Brightness | One drive level for every segment | Per-segment and colon equalisation, so a `1` is not brighter than an `8` |
| USB serial | GPS sentences | The same, plus a timing sentence on each GPS pulse, and opt-in diagnostics |
| A stalled clock | Frozen until power-cycled | Restarts itself |
| Removing a line from `config.txt` | Takes effect at the next power-on | Takes effect when the file is saved |

<details>
<summary><b>Display modes</b>: the 12 new keys</summary>

Enable each in `config.txt` like the stock modes, or from the menu.

| Key | Shows |
|---|---|
| `MODE_SUN` | Sunrise, sunset and solar noon |
| `MODE_SUN_AZEL` | The sun's azimuth and elevation now |
| `MODE_MOON` | Moon phase and illuminated percentage |
| `MODE_DARK` | Countdown to astronomical darkness, and the twilight times |
| `MODE_GRID` | Maidenhead grid locator |
| `MODE_LATLON` | Latitude and longitude |
| `MODE_LST` | Local sidereal time, ticking on the time row |
| `MODE_SOLAR` | Apparent solar time (sundial time), ticking on the time row |
| `MODE_ZONE2` | A second timezone, ticking on the time row |
| `MODE_STAR` | Countdown to the next bright stars crossing the meridian |
| `MODE_ADEV` | Allan deviation of the clock's own oscillator, measured against GPS |
| `MODE_TEMPCOMP` | What the temperature compensation has learned |

</details>

<details>
<summary><b>When GPS is lost</b>: digits that fade honestly, and a held frequency</summary>

All on by default.

- **`significance_fade`** replaces the fixed `Tolerance_time_*` times. The clock computes a bound on its own time error from what it measured while locked, and each sub-second digit fades out as the bound passes that digit's place value. A digit that is lit is still right.
- **`freq_hold`** steers out the oscillator's small fixed offset, measured against GPS while locked. Replayed over 24 days of recordings from one clock, the typical error after an hour without GPS fell from 386 µs to 7 µs.
- **`tc_learn`, `tc_apply`, `tc_persist`**: the clock learns how its two crystals drift with temperature, applies that during an outage, and keeps the model across power-off.

</details>

<details>
<summary><b>Display</b>: brightness equalisation, colons, chimes, paging</summary>

- **`seg_balance`, `colon_balance`** (on by default) equalise brightness between digits that light different numbers of segments, and dim the colons with the rest of the display.
- **`colon_alt_mode`** gives the sidereal, solar and second-timezone modes their own colon animation, so they cannot be mistaken for civil time.
- **`cuckoo`** adds a small flourish each quarter-hour and a longer one on the hour.
- **`page_ms`** sets how long each page of a multi-page mode stays up.

</details>

<details>
<summary><b>The settings menu, and <code>config.txt</code></b></summary>

Hold both buttons to open the menu on the date row; the time row keeps the time. It covers brightness, colons, the display modes, the options above and a factory reset. It needs this firmware on **both** boards (`fwt.bin` and `fwd.bin`), because the date board reads the buttons.

Menu choices are saved in `SETTINGS.BIN` on the clock's drive and survive power-off.

When `config.txt` and the menu disagree, the rule works one setting at a time:

- `config.txt` does not mention the setting: the menu's choice applies.
- The line is unchanged since the clock last read the file: the menu's choice still applies.
- The line is new or changed: the file wins, and the menu's choice is cleared. Remove that line later and the setting goes to its built-in default.

So a brightness set at the clock survives an edit to the timezone line, and a line you change in the file always takes effect, whether the clock was running at the time or unplugged straight after.

The clock also sends its menu choices over USB as `config.txt` lines (`$PMCFG`). With PCC's `pccd` running, they are written into `config.txt`, so the file shows what the clock is doing. With `config_write = on`, a clock on a charger writes them into the file itself.

</details>

<details>
<summary><b>USB serial output</b>: timing and diagnostic sentences</summary>

`$PMTXTS` is on by default (`pps`); the rest are off until asked for.

| Sentence | Carries |
|---|---|
| `$PMTXTS` | One line per GPS pulse: phase against GPS, oscillator calibration, temperature, and a USB start-of-frame tail that lets a computer place the pulse on its own clock to about a microsecond despite USB jitter |
| `$PMBRIT` | The automatic dimmer's operating point |
| `$PMADEV`, `$PMHDEV` | The measured stability curve |
| `$PMSTAR` | The upcoming star transits |
| `$PMCFG` | The menu's saved choices, as `config.txt` lines |

One-line reports are also available for the temperature model (`tc_dump`), the settings store (`menu_dump`) and the watchdog (`wdt_dump`).

</details>

<details>
<summary><b>Fixes</b>: always on</summary>

Seen on a real clock:

- **The millisecond count ran one ahead of GPS.** A timer tick left pending across the GPS pulse was counted as the first millisecond of the new second: 98.8% of locked seconds in recordings from one clock. Found through `$PMTXTS`. The stock pulse handler restarts the counter the same way, so stock very likely does the same; it has no telemetry to show it.
- **The timezone lookup re-ran on every GPS fix** although the clock had not moved, stalling the main loop for about 300 ms each second.

Found in the code or the emulator, and fixed before they were seen on a clock:

- A cut-short `TZRULES.BIN`, such as an interrupted copy, made the timezone loader run off the end of memory at every power-on. It is now refused, and the loaded zone is kept.
- A `config.txt` saved with no file timestamp was never loaded.
- An unguarded pointer in the USB serial path when no computer is attached.
- Out-of-range writes in the text mode, and in the date board's display and text handling.
- Work inside the USB interrupt that could collide with the main loop: applying a serial setting, checking for an update on eject, and allocating memory.

Changed for everyone: a line removed from `config.txt` returns that setting to its default when the file is saved, not at the next power-on.

On by default: the watchdog (`watchdog`) restarts a clock whose main loop has stalled for 8 seconds. It is never armed during an update, and stands down after three restarts in a row.

</details>

<details>
<summary><b>Files on the drive</b>, and what is not changed</summary>

| File | For |
|---|---|
| `SETTINGS.BIN` | Menu settings and the learned temperature model |
| `STARS.BIN` | The bright-star catalogue for `MODE_STAR` |

Not changed: the hardware, the bootloader, the update method, and every stock mode and `config.txt` key. The bootloader checks an image before it erases anything.

</details>

## Install

Download the files from the **[latest release](https://github.com/peterlewis/clock4/releases/latest)**.

1. Plug the clock in. It appears as a drive.
2. If the drive has no `SETTINGS.BIN`, copy `settings.bin` to it. Never copy it over an existing one: that file holds your settings.
3. Copy `stars.bin` if you want `MODE_STAR`.
4. Copy `fwt.bin` (time board) and `fwd.bin` (date board).
5. Eject the drive. The clock updates itself and restarts.

Your `config.txt` keeps working as it is, and the defaults above apply to any key it doesn't mention.

> [!TIP]
> The release's `config.txt` is a sample with a line for every key this firmware knows, each at its built-in value. Copy lines from it into yours; don't copy it over your own unless you mean to lose your settings.

To return to stock, copy mitxela's `fwt.bin` and `fwd.bin` the same way. [qspi/qspi.md](qspi/qspi.md) covers the details, including formatting the drive from scratch.

## Companion app

[Precision Clock Companion](https://github.com/peterlewis/pcc) (PCC) runs this firmware in the browser, so every mode, the menu and the `config.txt` parser can be tried before flashing. It charts the telemetry, edits `config.txt` through the firmware's own parser, and its background program, `pccd`, records what the clock reports and keeps `config.txt` in step with the menu. The holdover figures above were measured from those recordings.

**[What PCC does with this firmware →](https://github.com/peterlewis/pcc#pcc-and-the-extended-firmware)**

<details>
<summary><b>How it is tested</b></summary>

- **Emulator suites** in the PCC repository ([`web/emu/phase1`](https://github.com/peterlewis/pcc/tree/main/web/emu/phase1)): 37 suites that drive the real firmware code, compiled to WebAssembly. A change here comes with a test there.
- **Conformance:** 4,511 display checks against independently computed calendar and astronomy results, and 912 timezone checks against the host's timezone database.
- **A native build** of the same source, compared byte for byte with the WebAssembly build's output.
- **A replay** of a capture from a real clock.
- **A power cut at every flash operation** of the `config.txt` writer, in the emulator; and the code that finds the file's sectors run with the real file-system code against disk images (`mk4-time/test/config_locate_test.py`).

</details>

<details>
<summary><b>The source</b></summary>

Three software projects, compiled with STM32CubeIDE:

- **mk4-time**: the main clock, on the STM32L476
- **mk4-date**: the secondary display, on the STM32L010
- **mk4-bootloader**: firmware updates, on the STM32L476

The QSPI folder has the scripts that build firmware images with a CRC the clock recognises, the tzrules, and a valid disk image; see [these notes](qspi/qspi.md).

`mk4-time/Core/Src/main.c` is split into sections under `main_parts/`; [the notes there](mk4-time/Core/Src/main_parts/README.md) explain the layout.

</details>

## Credits

The Precision Clock Mk IV, its hardware and the firmware this builds on are by [mitxela](https://mitxela.com).

Timezone detection is ported from [ZoneDetect](https://github.com/BertoldVdb/ZoneDetect) by Bertold Van den Bergh and uses shapefile data from [Timezone Boundary Builder](https://github.com/evansiroky/timezone-boundary-builder) by Evan Siroky.

The star catalogue is derived from the [HYG database](https://github.com/astronexus/HYG-Database).
