# Precision Clock Mk IV: extended firmware

This is a fork of mitxela's firmware for the [Precision Clock Mk IV](https://mitxela.com/projects/precision_clock_mk_iv). The `rollup` branch adds display modes, a settings menu on the clock itself, better behaviour when GPS is lost, and timing telemetry. It also fixes a number of faults found along the way.

> Independent work. It is not affiliated with mitxela and is not part of his releases. The official firmware is [mitxela/clock4](https://github.com/mitxela/clock4), and the official documentation is [here](https://mitxela.com/projects/precision_clock_mk_iv/docs).

- **The manual for everything added:** [docs/firmware-additions.md](docs/firmware-additions.md)
- **The companion app:** [Precision Clock Companion](https://github.com/peterlewis/pcc) (PCC) runs this firmware in a browser and reads what it reports. See [How PCC helps](#how-pcc-helps).

## How this firmware differs from stock

"Stock" means mitxela's released firmware: 0.0.4 on the time board and 0.0.1 on the date board. This branch reports `0.0.5+<build>` and `0.0.2+<build>`, where `<build>` is the build time, so every image is distinct and the bootloader never skips an update as "already installed".

New behaviour is opt-in. With none of the new `config.txt` keys set, and the stock date-board firmware, the clock shows what a stock clock shows. The exceptions are listed under [Fixes](#fixes-always-on).

| | Stock | This firmware |
|---|---|---|
| Display modes | Calendar, timezone, countdown, text, satellite and diagnostic modes | The same, plus sun, moon, twilight, grid locator, position, sidereal time, solar time, a second timezone, star transits and oscillator stability |
| Changing a setting | Edit `config.txt` on a computer | Also a two-button menu on the clock, saved across power-off |
| GPS lost | Sub-second digits are dashed after fixed times | Optional: each digit fades as the measured uncertainty reaches it, and the clock holds the oscillator frequency it measured while locked |
| Brightness | One drive level for every segment | Optional per-segment and colon equalisation, so a `1` is not brighter than an `8` |
| USB serial | GPS sentences | The same, plus opt-in timing and diagnostic sentences |
| Removing a line from `config.txt` | Takes effect at the next power-on | Takes effect when the file is saved |

### New display modes

Each is enabled from `config.txt`, like the stock modes, or from the menu.

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

### When GPS is lost

All of these are off by default.

- **`significance_fade`**: replaces the fixed `Tolerance_time_*` times. The clock computes a bound on its own time error from what it measured while locked. Each sub-second digit fades out as the bound passes that digit's place value, so a digit that is lit is a digit that is still right.
- **`freq_hold`**: the oscillator has a small fixed offset from its nominal frequency. The clock measures it against GPS while locked and steers it out when GPS goes away. Replayed over 24 days of recordings from one clock, the typical error after an hour without GPS fell from 386 µs to 7 µs.
- **`tc_learn`, `tc_apply`, `tc_persist`**: the clock learns how its two crystals drift with temperature and applies that during an outage. The learned model is kept across power-off.

### Display

- **`seg_balance`, `colon_balance`**: equalise brightness between digits that light different numbers of segments, and dim the colons with the rest of the display.
- **`colon_alt_mode`**: a separate colon animation for the sidereal, solar and second-timezone modes, so they cannot be mistaken for civil time.
- **`cuckoo`**: a small flourish on each quarter-hour and a longer one on the hour.
- **`page_ms`**: how long each page of a multi-page mode stays up.

### The menu, and `config.txt`

Hold both buttons to open a settings menu on the date row. The time row keeps showing the time. The menu covers brightness, colons, the display modes, the options above and a factory reset. It needs this branch's date-board firmware (`fwd.bin`) as well as the time-board firmware, because the date board reads the buttons.

Menu changes are saved in a file on the clock's drive, `SETTINGS.BIN`, and survive power-off.

`config.txt` and the menu can disagree about a setting. The rule works one setting at a time:

- `config.txt` does not mention the setting: the menu's choice applies.
- `config.txt` mentions it, and that line has not changed since the clock last read the file: the menu's choice still applies.
- That line is new or has changed: the file wins, and the menu's choice is forgotten.

So a brightness set at the clock survives an edit to the timezone line, and a line you change in the file always takes effect. This works the same whether the clock was running when you edited the file or was unplugged straight afterwards.

The clock also sends its menu choices over USB as `config.txt` lines. With PCC's `pccd` running, they are written into `config.txt`, so the file shows what the clock is doing. With `config_write = on`, a clock on a charger writes them into the file itself.

### USB serial output

Each of these is off until asked for.

| Sentence | Carries |
|---|---|
| `$PMTXTS` | One line per GPS pulse: the clock's phase against GPS, oscillator calibration, temperature |
| `$PMBRIT` | The automatic dimmer's operating point |
| `$PMADEV`, `$PMHDEV` | The measured stability curve |
| `$PMSTAR` | The upcoming star transits |
| `$PMCFG` | The menu's saved choices, as `config.txt` lines |

There are also one-line reports for the temperature model (`tc_dump`), the settings store (`menu_dump`) and the watchdog (`wdt_dump`).

### Fixes (always on)

Seen on a real clock:

- **The millisecond count ran one ahead of GPS.** A timer tick left pending across the GPS pulse was counted as the first millisecond of the new second. Recordings from one clock show it in 98.8% of locked seconds. It was found on this branch through `$PMTXTS`. The stock pulse handler restarts the counter the same way, so stock very likely does the same, but stock has no telemetry to show it.
- **The timezone lookup ran again on every GPS fix**, although the clock had not moved, and stalled the main loop for about 300 ms in every second.

Found by reading the code or in the emulator, and fixed before they were seen on a clock:

- **A cut-short `TZRULES.BIN`**, such as an interrupted copy, made the timezone loader run off the end of memory at every power-on. It is now refused, and the loaded zone is left as it was.
- **A `config.txt` saved with no file timestamp** was never loaded.
- **An unguarded pointer in the USB serial path** when no computer is attached.
- **Out-of-range writes** in the text mode and in the date board's display and text handling.
- **Work done inside the USB interrupt** that could collide with the main loop: applying a serial setting, checking for an update on eject, and allocating memory.

Also changed for every user:

- **A line removed from `config.txt`** returns that setting to its default when the file is saved, not at the next power-on.

There is also an opt-in watchdog (`watchdog = on`) that restarts a clock whose main loop has stopped.

### New files on the clock's drive

| File | For |
|---|---|
| `SETTINGS.BIN` | Menu settings and the learned temperature model |
| `STARS.BIN` | The bright-star catalogue for `MODE_STAR` |

The release folder also has a sample `config.txt`. It is the same text PCC uses as its template.

### What is not changed

The hardware, the bootloader, the update method, and every stock mode and `config.txt` key. The bootloader checks an image before it erases anything, so the stock firmware can be put back the same way this one was installed.

## Installing

Ready-built images are in [qspi/output](qspi/output).

1. Plug the clock into a computer. It appears as a drive.
2. If the drive has no `SETTINGS.BIN`, copy `settings.bin` to it first. Do not copy it over an existing one: that file holds your settings.
3. Copy `stars.bin` if you want `MODE_STAR`.
4. Copy `fwt.bin` (time board) and `fwd.bin` (date board).
5. Eject the drive. The clock updates itself and restarts.

Your existing `config.txt` keeps working as it is. `config.txt` in the same folder is a sample with a line for every key this firmware knows, the new ones included, each switched off or at its built-in value. Copy lines from it into yours, or start from it. Do not copy it over your own file unless you mean to lose your settings.

To return to stock, copy mitxela's `fwt.bin` and `fwd.bin` the same way. [qspi/qspi.md](qspi/qspi.md) has the details, including how to format the drive from scratch.

## Status

This is draft firmware. I run it on my own clock. Every change is tested in an emulator before an image is built, and the hardware testing is that one clock. Use it at your own risk.

## How PCC helps

[Precision Clock Companion](https://github.com/peterlewis/pcc) is a browser app and a small background program for the clock. It is the other half of this work.

- **Try the firmware without flashing it.** PCC compiles this source to WebAssembly and runs it as the clock on screen. Every mode, the menu and the `config.txt` parser are the real code. Open [pcc.ptrl.ws](https://pcc.ptrl.ws/) to use it.
- **It is how the firmware is tested.** The emulator test suites live in the PCC repository. A change here comes with a test there.
- **It reads the telemetry.** PCC charts `$PMTXTS` phase and drift, the stability curve and the star transits, and fits the temperature model.
- **It edits `config.txt`.** The editor runs each line through the firmware's own parser, so what it accepts is what the clock accepts. Its template is this repository's sample `config.txt`, and it can lay an existing file's settings onto that template.
- **It keeps a record.** The background program, `pccd`, records everything the clock reports. Those recordings are what the holdover figures above were measured from.
- **It keeps `config.txt` true.** When you change a setting with the clock's buttons, `pccd` writes that line into `config.txt` on the clock's drive.
- **It makes the clock a time source.** `pccd` can feed the clock's GPS pulse to chrony, so the computer keeps time from it.

## How it is tested

- **Emulator suites** in the PCC repository (`web/emu/phase1`): 37 suites that drive the real firmware code, compiled to WebAssembly.
- **Conformance:** 4,511 display checks against independently computed calendar and astronomy results, and 912 timezone checks against the host's timezone database.
- **A native build** of the same source, whose output is compared byte for byte with the WebAssembly build's.
- **A replay** of a capture from a real clock.
- **A power cut at every flash operation** of the `config.txt` writer, in the emulator; and the code that finds the file's sectors run with the real file-system code against disk images (`mk4-time/test/config_locate_test.py`).

## The source

There are three software projects, compiled with STM32CubeIDE:

- mk4-time is the main clock source code running on the STM32L476
- mk4-date is the secondary display running on the STM32L010
- mk4-bootloader runs on the STM32L476 to do firmware updates

In the QSPI folder, there are scripts to create firmware images with CRC that the clock will recognise, along with the scripts to create the tzrules and create a valid disk image, see [these notes](qspi/qspi.md) for more info.

`mk4-time/Core/Src/main.c` is split into sections under `main_parts/`; [the notes there](mk4-time/Core/Src/main_parts/README.md) explain the layout.

## Credits

The Precision Clock Mk IV, its hardware and the firmware this builds on are by [mitxela](https://mitxela.com).

Timezone detection is ported from [ZoneDetect](https://github.com/BertoldVdb/ZoneDetect) by Bertold Van den Bergh and uses shapefile data from [Timezone Boundary Builder](https://github.com/evansiroky/timezone-boundary-builder) by Evan Siroky.

The star catalogue is derived from the [HYG database](https://github.com/astronexus/HYG-Database).
