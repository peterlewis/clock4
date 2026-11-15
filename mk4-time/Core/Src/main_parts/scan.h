// main_parts/scan.h — the display scan: its length, the per-digit dimming inputs, and the per-segment balance maths (seg_balance).
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

uint16_t display_scan_len = 5;   // last DMA transfer count set here — segbal_poll steers 5 <-> 80

void setDisplayPWM(uint32_t bright){
  display_scan_len = (uint16_t)bright;
  HAL_DMA_Abort(&hdma_tim1_up);
  HAL_DMA_Abort(&hdma_tim7_up);
  HAL_DMA_Start(&hdma_tim1_up, (uint32_t)buffer_b, (uint32_t)&GPIOB->ODR, bright);
  HAL_DMA_Start(&hdma_tim7_up, (uint32_t)buffer_c, (uint32_t)&GPIOC->ODR, bright);
}

// Holdover fade (opt-in). A sub-second digit whose accuracy can no longer be held — its time
// uncertainty during GPS-loss holdover has grown past that digit's place value — FADES to black by
// its remaining significance instead of dashing, overriding the fixed Tolerance_time_* ladder.
// digit_bright[] holds per-digit intensity 0..FADE_MAX for the [deciseconds, centiseconds,
// milliseconds, decimal-point] positions (FADE_MAX = fully lit, i.e. certainly significant).
// Default 0 = NOT significant: at power-on nothing is disciplined yet (had_pps=0, holdover age is
// huge), so with significance_fade the sub-second digits must start DASHED and only light once the
// first computeHoldoverFade() proves significance — else setPrecision() at boot reads a stale FADE_MAX
// and flashes ticking numbers for ~1 s before the first per-second recompute dashes them.
volatile _Bool significance_fade = 0;
#define FADE_MAX 16
uint8_t digit_bright[4] = { 0, 0, 0, 0 };

// --- Per-segment brightness balance (seg_balance) --------------------------------------------
// The display is voltage-driven (buffer chips + 10R per segment; the DAC sets the rail), so all
// lit segments of a digit share the drop across that digit's common return path: FEWER lit
// segments leaves more voltage per LED, so '-', '1' and '7' glow visibly brighter than '8' —
// worst at low brightness where the rail sits just above the LED forward voltage. There is no
// per-digit analog knob, but there is headroom in the scan: buffer_b/buffer_c are sized [80] and
// the DMA is circular, so the 5-step scan can be replayed as 16 cycles of 5 slots. Slots 0..4
// stay the live "master" slots every existing writer already targets; segbal_poll() (main loop,
// at most 1 kHz) mirrors them into slots 5..79 with each digit lit in s of its 16 cycles,
//     s = 16 - strength * (16 - 2 * popcount(segments)) / 100
// so at full strength s = 2N and every lit segment averages the same duty-per-segment product:
// (1/N) * (2N/16) = 1/8, uniform across the display (an 8-segment '8.' keeps 16/16 — sparse
// digits dim DOWN to its level, dense digits are untouched). The cycle mask picks each digit's s
// lit cycles as a NESTED rank window: rank = bit-reversed k rotated by a fixed per-digit phase
// (cycle 0 — the master slot — pinned at rank 0, so masters are never rewritten and count toward
// the duty exactly). Two properties are load-bearing, and each was a hardware-visible artefact
// when missing:
//  - NESTED (s+1's lit set = s's plus exactly one cycle): s changes whenever a digit's segment
//    count does — every second boundary at least — and a spread that RESHUFFLES on s±1 (e.g.
//    (k*s) % 16 < s, the first ship) re-phases the whole column's light at that instant: a
//    once-per-second step in the ghost bleed of unlit segments + a beat on the sub-second digits.
//  - DECORRELATED (distinct phase per digit): un-rotated nested ranks light every digit on the
//    SAME low-rank cycles, so the shared rail sees a sawtooth (all-on cycles vs sparse cycles,
//    per-cycle load variance ~14 vs ~1.4 rotated in sim) whose shape steps with the time content
//    — the whole DATE row blipped at 1 Hz and per-digit brightness left calibration (the K curve
//    was eyeballed against decorrelated patterns).
// Positions stay near-evenly spread at every s (a rotated rank window bit-reverses to a van-der-
// Corput run; worst case s = 1 is the master alone, unchanged from stock 5-slot timing).
//
// Above 100 the map turns into a power law, s = 16*(N/8)^(strength/100) — continuous at 100
// (gamma 1 = the linear map). Near the LED forward-voltage knee (low rail) segment current is
// exponential in voltage, so the real bloom ratio between a '-' and an '8.' can exceed what any
// LINEAR duty map can counter; hardware A/B showed 100 under-correcting at low brightness. The
// exponent steepens the curve: at 200, a 1-segment digit runs 1/16 duty against the 8-segment
// digit's 16/16. Tune live over serial against the actual display.
// Off (0, the default) leaves the stock 5-slot scan and all timing untouched.
//
// The bloom is rail-dependent, and NOT monotonically: hardware calibration (eyeball-matched at
// four rail levels) found a valley — strong compensation needed at the dim end (the LED knee,
// where current is exponential in voltage), moderate again at full rail (maximum-current IR drop
// in the shared return), mild in between. "seg_balance = on" applies that measured curve,
// interpolated by the live rail (dac_target, 4095 = dimmest); it is the whole intended interface.
// A numeric value (2..300) instead applies a fixed manual strength for experiments.
// seg_balance = 0/off (the default) keeps the stock scan and stock timing untouched.
volatile uint16_t seg_balance = 0;     // 0 = off (stock) · 1/on = AUTO (calibrated curve) · 2..300 = fixed manual strength

#define SEGBAL_BSEG_MASK 0x01FCu       // GPIOB word: bits 2..8 are segments; everything else
                                       // (bCat column selects etc.) passes through unmasked

// rank(k) for D=16 (bit-reversed k). For D=8/4 shift right by 1/2 — dropping the low bits of the
// reversal is exactly the 3-/2-bit reversal, so one table serves every depth.
static const uint8_t SEGBAL_REV16[16] = {0,8,4,12,2,10,6,14,1,9,5,13,3,11,7,15};
// Per-digit phases for the mirror ranks (col x bank; DP rides its digit's phase so its cycles
// stay a subset of the digit's). Rank 0 = the master slot is pinned; the phase rotates ranks
// 1..D-1 among themselves (mod D-1), so every digit still lights the master and its mirrors sit
// in a distinct rank window. Values are spread over the 15-ring (idx*4 mod 15, all distinct).
static const uint8_t SEGBAL_PH_B[5] = {0, 4, 8, 12, 1};
static const uint8_t SEGBAL_PH_C[5] = {5, 9, 13, 2, 6};

// Mirror freshness across main-loop stalls. The sub-second MASTERS are written from the SysTick
// ISR (SysTick_CountUp_*), so the stock 5-slot scan never lags the main loop — but the mirrors
// carry (D-1)/D of the light when balancing, and a refill that lives only in the main loop goes
// stale for the length of any long pass (the once-per-second PendSV display prep, housekeeping,
// a flash commit): hardware showed the sub-second digits freezing once per second, present since
// the first segbal ship and independent of the dither order. segbal_poll() therefore EXPORTS the
// per-column lit-cycle bitmaps, and every SysTick tick re-copies the live master values through
// them (segbal_isr_refresh), pinning mirror VALUE freshness to the same 1 ms the masters get.
// The bitmaps may lag a main-loop pass behind a glyph change — a duty lag of one pass,
// imperceptible where a frozen digit was not.
static volatile uint16_t segbal_lit_b[5], segbal_lit_c[5], segbal_lit_dp[5];  // bit k = cycle k lit
static volatile uint8_t  segbal_mirror_live = 0;   // ISR refresh armed (D-cycle scan up, bitmaps valid)

// Lit cycles (of 16) for a digit with n lit segments at effective strength `eff` (0..300).
// n <= 8 always (7 segments + DP). Returns 16 (always lit) .. 1 (floor for any lit digit).
static uint32_t segbal_duty(uint32_t n, uint32_t eff){
  if (n == 0) return 0;
  if (eff <= 100)                                          // linear blend: 100 -> s = 2n exactly
    return 16u - eff * (16u - 2u*n) / 100u;
  float s = 16.0f * powf((float)n / 8.0f, (float)eff / 100.0f);   // power law, gamma > 1
  uint32_t si = (uint32_t)(s + 0.5f);
  return si < 1u ? 1u : (si > 16u ? 16u : si);
}

// Effective strength for THIS refill. AUTO (seg_balance = 1/on) follows the hardware calibration:
// a full eyeballed sweep across the whole rail on a production Mk IV (2026-07-11) landed the even
// point on a clean EXPONENTIAL — the round-number rails fell on a x3-per-half-brightness geometric
// progression (rail brightest -> 10, mid -> 30, dimmest -> 90), i.e. K = 10 * 9^(dac/4095). That is
// exactly the LED knee: near the bottom of the rail, segment current is exponential in forward
// voltage, so a sparse digit pulls away exponentially fast and needs exponentially more duty haircut.
// 4095 is the 12-bit DAC full-scale code (dac_target maxes there, matching the original firmware); the
// intermediate breakpoints below stay on clean powers of two and K evaluates to the measured anchors.
// Sampled to a 9-point LUT (cheaper than a per-refill powf, and exponentials interpolate linearly to
// well under 1 K). A numeric value (2..300) instead applies a fixed manual strength for experiments.
// seg_balance = 0/off (the default) keeps the stock scan and stock timing untouched.
static const uint16_t SEGBAL_AUTO_DAC[9] = { 0, 512, 1024, 1536, 2048, 2560, 3072, 3584, 4095 };
static const uint16_t SEGBAL_AUTO_K[9]   = { 10,  13,   17,   23,   30,   39,   52,   68,   90 };  // 10*9^(dac/4095)

static uint32_t segbal_strength(void){
  if (seg_balance != 1) return seg_balance;                // manual fixed strength, or 0 = off
  int32_t d = (int32_t)dac_target;
  if (d <= SEGBAL_AUTO_DAC[0]) return SEGBAL_AUTO_K[0];
  for (uint32_t i = 1; i < 9; i++) {
    if (d <= (int32_t)SEGBAL_AUTO_DAC[i]) {
      int32_t d0 = SEGBAL_AUTO_DAC[i-1], d1 = SEGBAL_AUTO_DAC[i];
      int32_t k0 = SEGBAL_AUTO_K[i-1],   k1 = SEGBAL_AUTO_K[i];
      return (uint32_t)(k0 + (k1 - k0) * (d - d0) / (d1 - d0));
    }
  }
  return SEGBAL_AUTO_K[8];
}

// Dither depth (cycles per column) for the CURRENT scan rate. display_frequency is a user config
// (1..100 kHz) that retunes TIM1, and a fixed 16-cycle dither at a slow scan would visibly strobe:
// the dither repeats at step/(5*D) Hz, so pick the deepest D of {16,8,4} that stays >= ~200 Hz,
// and return 0 (balancing unavailable) below ~4 kHz steps. Stock rate (~62 kHz steps) gives D=16.
static uint32_t segbal_depth(void){
  uint32_t step = 16000000u / (TIM1->ARR + 1u);            // TIM1 clock 16 MHz (see setDisplayFreq)
  if (step >= 16000u) return 16u;
  if (step >=  8000u) return 8u;
  if (step >=  4000u) return 4u;
  return 0u;
}
