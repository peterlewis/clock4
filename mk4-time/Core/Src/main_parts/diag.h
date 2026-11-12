// main_parts/diag.h — the $PMLOOP loop-latency marks and the $PMBRIT report state.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

// ---- $PMLOOP: main-loop latency diagnostic (`loop_diag = on` over serial or config) -----------
// Once per second, emit the WORST gap (ms of uwTick) between consecutive profiler marks and the
// tag of the section that produced it: $PMLOOP,<worst_ms>,<tag>. Marks bracket the main loop's
// sections; PendSV stamps tag 15 when it preempts, so the once-per-second display prep shows up
// under its own name. Emission is lossy on a busy USB endpoint by design (1 Hz diagnostic).
// Tags: 1 menu . 2 balance/colon/brightness-report . 3 tz-lookup . 4 delayed-housekeeping . 5 vbus/temp .
//       6 pps-emit/tempcomp/dumps . 7 vbat/astro . 8 mode-pages/star . 9 alt/loop-tail . 15 PendSV
volatile uint8_t loop_diag = 0;
volatile uint8_t pmloop_lasttag = 0;
static uint32_t  pmloop_last = 0, pmloop_max = 0, pmloop_win = 0;
static uint8_t   pmloop_maxtag = 0;
#define LP_MARK(n) do { uint32_t t_ = uwTick, g_ = t_ - pmloop_last; \
    if (g_ > pmloop_max) { pmloop_max = g_; pmloop_maxtag = pmloop_lasttag; } \
    pmloop_last = t_; pmloop_lasttag = (n); wdt_keep.tag = (n); } while (0)

// ---- $PMBRIT: the auto-dimmer's operating point (`brightness_report = on`, or SYS > BRT MSG) -----
// Once per second: the ambient reading the brightness loop last sampled, the display brightness it
// chose, what chose it, and the two balance values that follow it, so a host can place the clock on
// its BS curve live. Nothing else on USB carries the sensor or the rail: MODE_DEBUG_BRIGHTNESS shows
// the same pair, but on the date row. Off by default, so the stock serial output is unchanged.
//   $PMBRIT,<adc>,<dac>,<src>,<segk>,<colon>*CC
//   adc    ambient light, the raw ADC code 0-4095 (sampled on every pass, whatever drives the rail)
//   dac    display brightness 0-4095 on the BSn scale: 4095 - dac_target, so 0 is dark, 4095 full
//   src    A auto (following the BS curve) . M manual override (brightness key / menu BRIGHT) . S standby
//   segk   effective per-segment balance strength, 0 when off or unavailable at this scan rate
//   colon  applied colon animation scale, of 256 (256 = full, the stock behaviour)
//   In standby the display is off, so segk and colon read 0.
volatile uint8_t brightness_report = 0;
static uint32_t  brit_win = 0;
