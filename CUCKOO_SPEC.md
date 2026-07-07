# CUCKOO — scheduled display animations for the Precision Clock Mk IV

Status: DRAFT v1 · branch `cuckoo` (off master, standalone)

A cuckoo is a short, scheduled display flourish. Five are defined. Each exists to achieve
exactly one thing, statable in one sentence without "and"; any effect that does not serve
that sentence is out. Every animation decorates or modulates the true display — the clock
never shows wrong digits, and every animation's final frame is the plain live face.

This is firmware: the web companion (PCC) inherits the animations through its byte-faithful
WASM build and renders per-segment intensity through an emulator shim export. There is no
web-side animation engine.

## Config vocabulary

```
# which animation manifests (one of the five)
cuckoo_animation = heartbeat

# minutes between manifests, anchored to the hour: the piece ALWAYS plays on the hour,
# and e.g. 15 adds :15/:30/:45. Default 60 (hourly). off / 0 disables.
cuckoo_interval = 60

# a button-cycled display mode that tours the whole catalogue continuously,
# each piece announced by name on the date row
MODE_CUCKOO_SHOWCASE = disabled
```

One rule, no special cases: the arriving minute is due when `minute % interval == 0`. The
schedule plays exactly one chosen piece; the showcase MODE is how you see the whole catalogue.

## The display, as it actually is

Facts that bound every design (verified against main.c):

- The TIME board multiplexes 5 big-digit categories (`bCat0..4`) through `buffer_b[80]` /
  `buffer_c[80]`, circular-DMA'd to GPIOB/GPIOC at the TIM1/TIM7 update rate. The scan
  normally plays 5 slots (one per category, written by `latchSegments()`); global brightness
  (`setDisplayPWM`) dims by lengthening the DMA cycle into the zeroed tail of the buffer
  (5 lit slots + up to 75 dark ones).
- The three sub-second digits + the seconds-units digit live on the `buffer_c` port in the
  same scan. The colons are true PWM (TIM2 CCR via the 200-entry, 10 ms/step DMA tables).
- **The DATE row is a separate board** (mk4-date) fed rendered text over UART with a latch
  byte. The time board cannot address its segments, and today the protocol carries no
  brightness information.

### Primitive 1 — per-segment brightness on the time board (16 levels)

The 80-slot buffer is exactly 5 categories × 16 repeats. During an animation the engine
switches the scan to the full 80-slot interleave `[cat0..cat4] × 16`, where segment *s* of
category *k* is lit in the first `seg_bright[k][s]` of its 16 repeats (0 = dark,
16 = fully lit). This is the same dwell mechanism the significance fade defined
(`digit_bright`, FADE_MAX = 16) taken to per-segment resolution — and it finally gives the
fade's partial levels a physical rendering on hardware, closing the "smooth HW dimming"
gap left by the significance-fade work.

- Scan rate: the category cycle runs 16× longer in this mode; TIM1/TIM7 period is divided
  by 16 for the duration so the digit refresh rate is unchanged (no visible flicker change).
- Global brightness composes multiplicatively in the engine (`level = seg * bright >> 4`),
  since the dark-tail mechanism is unavailable while all 80 slots are in use.
- Entry/exit are atomic: the interleave is staged in the main loop into the unused tail
  half of a double buffer, then the DMA is restarted once (`setDisplayPWM` idiom); exit
  restores the plain 5-slot scan. Both transitions land on a latch boundary.
- Cost: the buffers already exist at [80]; engine RAM is `seg_bright[9][8]` (big + small
  digits + DPs) ≈ 80 B + ~40 B state.

### Primitive 2 — the 10 ms animation tick

A table/integer engine in the colon-DMA idiom: the main loop stages envelopes; a 10 ms
tick (centisecond edge in the SysTick cascade) advances integer countdowns and writes
`seg_bright`. No floats in ISRs; every envelope is a terminating integer countdown, so an
animation cannot fail to end. All timing derives from `currentTime` + the sub-second
counters — never frame or loop counts.

### Primitive 3 — the scheduler

Armed from the existing `.900` staging (which already knows the expiring and next display
values). Triggers are second edges of `currentTime`; the quarter/hour tests are plain
minute/second matches. An animation in flight is never preempted; a scheduled trigger that
collides with one is skipped, not queued.

## The catalogue

### 1. `carry` — make the arithmetic of the rollover visible (1.8 s; 2.4 s at the hour)

Trigger: the rollover into a due minute (minute % interval == 0).
Data: the `.900`-staged expiring/next values — nothing else. Plays in holdover unchanged
(rollovers are true regardless of GPS).

- **Charge** (T−500 ms): the seconds-units digit ramps nominal → 16/16 — visibly overfilling.
- **Pour** (T0+): the staged values give the carry chain length N (1 on a plain minute,
  3 at a quarter, 5 at the hour). Right to left, digit k fires at `T0 + k·beat`: its new
  true glyph snaps in at 16/16 while its right neighbour dips to 6/16 — light handed
  leftward, one digit per beat. `beat` = 60 ms normally, **120 ms at the hour: the chain
  length is the payload, and it must be countable from across the room.**
- **Settle**: fired digits decay linearly 16 → nominal over 300 ms; donors recover in 200 ms.

Exit: all envelopes are integer countdowns terminating at nominal — the animation runs out
onto the plain face; no final blend exists to get wrong.

### 2. `heartbeat` — the machine shows its own pulse (8 s)

Trigger: the due-minute second edge. Data: the live colon DMA table + its live read index
(sampled from the DMA counter in the main loop, never an ISR).

The six big digits + three small digits breathe to the actual colon waveform, radiating
**outward from the colons** by physical distance: element *e* at colon-distance *d* samples
the table at `(live_index − 12·d) mod 200`. Four full 2 s cycles — one cycle reads as
flicker, four read as intent. Brightness floor 4/16: the time stays readable throughout.
If the configured colon mode does not pulse, the colons are lent the canonical heartbeat
table **for the duration only** (theatre needs its heart on stage) and restored bit-exact
at exit. Exit lands on a sampled waveform peak and ramps offsets to zero in 200 ms.

Fully functional with no GPS — the pulse is the machine's own.

### 3. `rain` — show where the time comes from (6 s) — **v2, blocked on the date board**

One drop of light per real tracked satellite falls down its true azimuth column
(10 date-row digits = ten 36° bins, north at digit 0, clockwise), brightness = C/N0,
zenith-highest first, splashing on the DP; then one second of *puddle map* (each wet
column's DP holds its strongest satellite's level) before the date fades back.

Two dependencies, both honest scope:
- **decodeGSV extension**: firmware currently parses only the in-view count; az/el/C-N0
  are in the same sentences. ~32-entry × 3 B RAM table, trivial CPU.
- **mk4-date protocol extension**: the date board renders text and carries no brightness.
  Rain's canvas requires a per-digit/per-segment intensity frame in the UART protocol
  (one command byte + 10 bytes of levels at minimum). Until that exists on both boards,
  rain is not buildable as designed — and it is **deferred, not degraded**: a count-based
  or wrong-canvas rain would fabricate a sky, and a dry sky is the only honest fallback.

No fix → skipped silently. No fake rain, ever.

### 4. `pendulum` — prove the discipline (3 s, display label `CAtCH`)

Trigger: the PPS edge at :57 before a due minute, resolving on its :00 edge.
Data: PPS edges, ms counter, measured drift/jitter (seeds the spread — a well-behaved
oscillator genuinely starts calmer).

Opens with the six big digits swaying **in unison** (0.5 s) — the disorder that follows is
seen to develop, so it reads as choreography, never as a failing multiplexer. Mismatched
integer periods pull the six phase accumulators apart (maximum disorder ≈ 1.5 s), then each
period is chirped along a precomputed integer schedule so all six phases cross zero within
one 10 ms tick of the true :00 PPS edge. The catch lands with one unified full-row breath
(300 ms swell) — the exclamation point — and the plain face at :00 is the final frame.

Holdover → **skipped**. Converging onto an undisciplined internal edge would forge the
signature; no PPS, no performance.

### 5. `trust` — how much the instrument actually knows right now (3 s)

Trigger: the due-minute top-of-second. Data: lock/holdover state; the significance machinery's
U(τ) where present (tempcomp/PR #9 stacking), the stock `Tolerance_time_*` ladder otherwise.

The time row dips to a 2/16 x-ray (300 ms), then a relight wave walks HH → MM → SS → ds →
cs → ms (+DPs), 80 ms per position, each digit relighting to the confidence the firmware
holds for that digit *right now*: full 16/16 while the uncertainty is far below the digit's
place value, rolling off on the significance curve as it approaches. Locked: the wave
overshoots to 16/16 and lands with a single unified row pulse — total confidence has its
own visible signature. Hours into holdover: the wave dies partway through the sub-seconds,
at exactly the honest position. Never locked: it stops dead after the seconds digit, and
the decimals stay dark for the hold — "these digits are unanchored."

Exit: the relit pattern already *is* the live significance-faded face; whole digits ease to
nominal in 300 ms. The animation lands on the tell it dramatises.

v1 walks the time row; the date row joins the sweep (year → month → day first) when the
mk4-date brightness extension (see `rain`) exists.

## MODE_CUCKOO_SHOWCASE

A normal button-cycled display mode. It loops the catalogue: the piece's name renders on
the date row for 1 s (`CArry`, `HEArtbEAt`, `rAIn`, `CAtCH`, `trUSt` — all verified against
the 7-seg alphabet), the piece plays, the plain face rests 2 s, next piece. Pieces whose
data is absent (rain without a fix, pendulum in holdover) are announced and then skipped
with their honest reason shown briefly (`no FIH`, `no PPS`) — the showcase demonstrates the
honesty rules too. The time row stays live throughout.

## What was rejected, and why it stays rejected

- **Three-timescale tour** (civil/sidereal/solar on the big digits): a montage, and six
  seconds of a "wrong" time on a precision clock reads as a fault. The timescales already
  have their own button modes.
- **Moon dial**: the right heritage, the wrong canvas — a 7-seg limb-pair at 15 mm is not
  a moon. Reconsider only with a real pixel canvas.
- **Equation-of-time walk**: elegant, illegible at distance (the payload lived on DPs).
  First substitute if a keeper falls in implementation.
- **Multiplex-scan reveal**: unbuildable at the 10 ms tick, and the "true scan order" it
  claimed to expose was itself a fiction (the scan is 5 category groups, not 19 positions).
- Any count-based rain fallback: fabricated sky positions on an honesty-branded clock.

## Emulator / PCC integration

- `build.sh` probes the branch (`EMU_HAS_CUCKOO`); the shim exports `emu_seg_bright(cell,
  seg)` mirroring `emu_digit_fade`, and the PCC face renders it through the existing
  per-element `setSegField` surface (built 2026-07-07 — the web prototype's lasting
  contribution is exactly this rendering path).
- The web-side prototype engine (`demo7.js`) is retired when this branch reaches the
  rollup; PCC never grows a second animation implementation.
- Conformance: the golden suite must show byte-identical display behaviour with
  `cuckoo_interval = off` (the feature costs nothing when disabled), and a new fixture
  drives one `carry` and asserts the seg_bright envelope terminates at nominal.
