# main_parts: the sections of main.c

`main.c` used to be one file of about 7,500 lines. These files are that text, one subsystem to a
file, included from `main.c` once each.

They are sections, not headers. Each one defines code and state, may use anything declared above the
point where it is included, and must not be included anywhere else.

## Why one translation unit

A split into separately compiled modules would change what the compiler produces. Much of this
firmware depends on functions being inlined across what would become module boundaries: the 1 ms
tick handlers, the date-row wrapper, the scan refresh. Compiled apart, the image grows and the timing
of the interrupt paths changes, and neither can be checked without a clock on a bench.

Kept as one translation unit, a rearrangement can be proven to change no code. Twice so far:

- The first cut kept every line in its old order. The compiled `main.o` and the linked
  `mk4-time.bin` were byte-for-byte identical before and after.
- The sections were then put in dependency order and each given its own state. That moves functions
  and variables to new addresses, so the image differs, but every function and every variable in
  `main.o` compiled to the same bytes with the same references (`test/objeq.py`, below).

The emulator in the PCC repository also relies on this: it includes `main.c` whole so that its tests
can reach the firmware's internal state.

## Rules

- Order matters. A section can only use what is defined above it. The order is the list of
  `#include` lines in `main.c`, and it runs from what everything needs (the watchdog marks, the
  sensors, the timezone rules) to what needs everything (the date row, the store, the menu, the
  config parser, the main loop's housekeeping).
- A section owns its state: its settings, its working variables, the flags that ask it to do
  something. They are declared at the top of its file.
- State that a section needs from a section BELOW it cannot live with its owner. It is declared in
  `main.c`, above the list: the time, the position, the display buffers, the `config` structure, the
  display mode, and the two things the menu shows the rest of the firmware.
- A call that goes up the list needs a prototype. They are in `main.h` (`sendDate`, `setPrecision`,
  the PPS handlers) and in the `PFP` block of `main.c`. Do not declare another section's function
  inside a section to get round the order, and do not define a variable twice: move the code instead.
- A new subsystem gets a new file here and one `#include` line in `main.c`, placed after everything
  it uses.

## Speed and size

The project is compiled `-O3`, which the interrupt handlers and the display path want. Most of the
code is neither: it runs once at start-up, or when a file changes, or when a button is pressed. Built
`-O3` it was about 12 KB bigger than it needs to be, in an app region of 192 KB.

So a function that runs rarely, and never on a timing path, is marked `SMALL_CODE` (defined in
`main.c`: the compiler's `cold` attribute, and never inlined). The compiler then builds it for size.
Mark new code of that kind. Do not mark anything an interrupt handler runs (the SysTick, PPS, PendSV,
UART, DMA and DAC handlers), `sendDate`, the brightness and colon polling, the cuckoo, or the
astronomy. The USB interrupt is the exception: it has a lower priority than those handlers, and the
config parser it runs for a serial line is marked.

## Checking a rearrangement

Moving code must not change it. Keep the old `main.o`, rebuild, and compare:

    cp Release/Core/Src/main.o /tmp/main_before.o
    (cd Release && rm Core/Src/main.o && make Core/Src/main.o)
    python3 test/objeq.py /tmp/main_before.o Release/Core/Src/main.o

`objeq.py` compares the two objects function by function and variable by variable, whatever order
they are in: the same bytes, and the same references by name. It prints `EQUIVALENT`, or the
functions that differ. If nothing moved to a new place, the stronger check also applies: build with
the same `build_id.h` (`make main-build` in `Release/` relinks without stamping a new one) and
compare `mk4-time.bin`.

Two variables share the named section `.ram2`: the watchdog's record and the Allan-deviation ring.
Their order in it follows the order of the sections here.

## The sections, in order

| File | What it holds |
|---|---|
| `watchdog.h` | The independent watchdog and its boot-loop guard |
| `diag.h` | The $PMLOOP loop-latency marks and the $PMBRIT report state |
| `sensors.h` | USB power, the backup cell voltage, the die temperature |
| `rtc_cal.h` | Calibrating the 32 kHz RTC crystal against GPS |
| `zones.h` | Timezone rules from TZRULES.BIN, and the second zone's offset |
| `astro_rows.h` | The sun, moon, grid and star-transit rows: what the main loop computes for them |
| `time_row.h` | The big digits: the next second, countdown, the sidereal / solar / second-zone timebases, the RTC write |
| `gps.h` | The NMEA sentences the clock reads: RMC for time and position, GSV for satellites in view |
| `scan.h` | The display scan: its length, the per-digit dimming inputs, and the per-segment balance maths (seg_balance) |
| `cuckoo.h` | Scheduled flourishes on the time row |
| `scan_compose.h` | The display on and off, and the balance, the fade and the cuckoo composed into the scan |
| `matrix_and_colons.h` | The scan rate, and the colon animations and their brightness |
| `adev.h` | The Allan and Hadamard deviation engine |
| `pps.h` | The PPS edge: capture, the three edge handlers, and the $PMTXTS and $PMBRIT sentences |
| `tempcomp.h` | Learning the oscillators against temperature: samples, fits, seeds, the models |
| `freq_hold.h` | The oscillator's measured offset and learned wander |
| `governor.h` | Once a second: steer the timebase in holdover, trim the RTC |
| `fade.h` | The significance fade: the bound on the display's time error in a holdover |
| `ticks.h` | The 1 ms SysTick handlers: the sub-second digits and the .900 preparation of the next second |
| `modes.h` | Which digits may be shown (the precision ladder), changing display mode, the buttons |
| `date_row.h` | `sendDate`: every display mode formatted for the date board, in its own C-library context |
| `dumps.h` | The serial reports: tc_dump, $PMADEV, $PMHDEV, $PMSTAR |
| `store.h` | SETTINGS.BIN: the menu settings store, the tempcomp store, and the journal for a safe sector rewrite |
| `menu.h` | The two-button menu |
| `config.h` | `config.txt` and serial config lines: the parser, the defaults table, the file loader |
| `config_write.h` | The clock writing its menu choices into `config.txt` itself (opt-in) |
| `housekeeping.h` | The timing subsystems' once-per-pass work in the main loop |
