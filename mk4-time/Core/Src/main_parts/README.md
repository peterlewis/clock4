# main_parts: the sections of main.c

`main.c` used to be one file of about 7,500 lines. These files are that same text, cut along the
boundaries it already had and included from `main.c` once each, in the same order.

They are sections, not headers. Each one defines code and state, may use anything `main.c` declares
above the point where it is included, and must not be included anywhere else.

## Why one translation unit

A split into separately compiled modules would change what the compiler produces. Much of this
firmware depends on functions being inlined across what would become module boundaries: the 1 ms
tick handlers, the date-row wrapper, the scan refresh. Compiled apart, the image grows and the timing
of the interrupt paths changes, and neither can be checked without a clock on a bench.

Kept as one translation unit, the split can be proven to change nothing. When it was made, the
compiled `main.o` and the linked `mk4-time.bin` were byte-for-byte identical before and after.

The emulator in the PCC repository also relies on this: it includes `main.c` whole so that its tests
can reach the firmware's internal state.

## Rules

- Order matters. A section can only use what is defined above it. The order is the list of
  `#include` lines in `main.c`.
- A new subsystem gets a new file here and one `#include` line in `main.c`, placed after everything
  it uses.
- State shared by several sections lives at the top of `main.c`, as before.
- Moving code between sections is safe when the order of definitions does not change. To check a
  move, build before and after with the same `build_id.h` and compare `mk4-time.bin`:
  `make main-build` in `Release/` relinks without stamping a new build id.

## The sections, in order

| File | What it holds |
|---|---|
| `astro_rows.h` | Sun, moon, grid and star-transit rows; ZONE 2 offsets |
| `date_row.h` | `sendDate`: every display mode formatted for the date board |
| `time_row.h` | The big digits: next second, countdown, sidereal / solar / second zone, the RTC write |
| `gps.h` | The NMEA sentences the clock reads |
| `scan.h` | The display scan and per-segment brightness balance |
| `cuckoo.h` | Scheduled flourishes on the time row |
| `diag.h` | `$PMLOOP` marks and `$PMBRIT` state |
| `matrix_and_colons.h` | The scan rate; colon animations and their brightness |
| `config.h` | `config.txt` and serial config: parser, defaults table, file loader |
| `rtc_cal.h` | Calibrating the RTC crystal against GPS |
| `adev.h` | The Allan and Hadamard deviation engine |
| `pps.h` | The PPS edge and the `$PMTXTS` / `$PMBRIT` sentences |
| `tempcomp.h` | Learning the oscillators against temperature |
| `freq_hold.h` | The oscillator's measured offset and learned wander |
| `governor.h` | Holdover steering and the RTC trim, once a second |
| `dumps.h` | The serial reports |
| `watchdog.h` | The independent watchdog and its boot-loop guard |
| `holdover.h` | The significance fade's bound; the main loop's housekeeping |
| `ticks.h` | The 1 ms SysTick handlers |
| `sensors.h` | USB power, backup cell voltage, die temperature |
| `zones.h` | Timezone rules from `TZRULES.BIN` |
| `modes.h` | The precision ladder, changing display mode, the buttons |
| `store.h` | `SETTINGS.BIN`: the menu store and the tempcomp store |
| `menu.h` | The two-button menu |
