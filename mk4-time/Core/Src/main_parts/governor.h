// main_parts/governor.h — once a second: steer the timebase in holdover, trim the RTC.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

volatile uint16_t tc_engage_s = 2;         // seconds of PPS absence before steering engages (min 2)
volatile uint16_t tc_max_ppm = 100;        // hard clamp on the applied correction magnitude

// Steering handoff, governor (main loop) -> tick ISR. base/rem are written together under
// IRQ-off; the ISR Bresenham distributes `rem` one-tick-longer periods per 1000 ms so the
// average period is (tc_load_base+1) + rem/1000 ticks — fractional-ppm rate steering.
volatile uint8_t tc_steer_on = 0;
volatile int32_t tc_load_base = 0;         // SysTick->LOAD for the shorter of the two periods
volatile int32_t tc_rem = 0;               // extra-tick remainder, always in [0,1000)
volatile int32_t tc_acc = 0;               // Bresenham accumulator (ISR-owned)

// Display cache for MODE_TEMPCOMP. Written by the governor (main loop); read by sendDate,
// which ALSO runs from the SysTick ISRs — each field is a single 32-bit (atomic) access, so
// the worst case is a one-repaint-stale value pairing, never a torn read.
float tc_disp_hse = 0, tc_disp_lse = 0;
_Bool tc_disp_hse_ok = 0, tc_disp_lse_ok = 0;
char  tc_disp_state = '-';                 // A applying · F frozen (config) · L learning · - idle

// What the governor is steering right now, for the significance-fade bound: the measured offset
// (freq_hold) and/or the modelled temperature change (tc_apply). Main-loop only.
static _Bool tc_hold_active = 0, tc_temp_active = 0;

// Once-per-second control: evaluate the models at the current die temperature, refresh the
// display cache, engage/disengage SysTick steering, and (optionally) trim RTC->CALR.
static void tc_governor(void){
  static uint32_t last_run = 0;
  static int32_t  applied_fp = 0;             // rate currently steered, 1/16 tick per second
  static int32_t  hold_fp = 0;                // freq_hold's estimate, frozen when steering engaged
  static _Bool    hold_ok = 0;
  static int32_t  dither = 0;                 // carried 1/16-tick remainder, [0,16)
  static _Bool    was_on = 0;
  static float    t_loss = 0;                 // die temp captured when steering engaged (filtered, sub-degree)
  static int32_t  last_steps = 0x7FFF;        // last CALR trim written (sentinel: none)
  static uint32_t last_calr = 0;

  // Snapshot the two ISR-written time variables together: currentTime increments at the
  // modelled .900 mark while last_pps_time updates at the edge, and reading them separately
  // can interleave with both ISRs and yield a wrapped-huge "fresh" that spuriously engages.
  __disable_irq();
  uint32_t now  = (uint32_t)currentTime;
  uint32_t lpps = last_pps_time;
  __enable_irq();
  if (now == last_run) return;
  last_run = now;

  uint32_t fresh = now - lpps;                // seconds since the last PPS edge
  if (fresh > 0x80000000u) fresh = 0;         // interleaved-read underflow: treat as fresh

  int t = die_temp_c;
  float lp = 0;
  _Bool have_l = tc_model_lse(t, &lp);
  int32_t tpp = tc_tpp();

  // The steering works from the filtered sub-degree temperature; t (whole degrees) still serves the
  // RTC trim, whose own steps are coarser than a degree of the LSE model, and the display.
  float tf = die_temp_f_ok ? die_temp_f : (float)t;

  // Would-be steering delta at the current temperatures (also feeds the display)
  float dt_now = 0;
  _Bool have_h = tc_hse_delta(tf, was_on ? t_loss : tf, &dt_now);

  // display cache (clamped so "HSE -99.99" never exceeds the 10-char row).
  // HSE page shows the ACTIVE steering correction in ppm. While locked the PPS discipline owns the
  // phase and nothing is steered: the page then shows 0.00, or with freq_hold on, the offset it is
  // measuring - the rate it will apply at the next GPS loss. LSE page shows the absolute model ppm.
  int32_t live_fp = 0;
  _Bool live_ok = freq_hold && fh_estimate(&live_fp);
  float fp_per_ppm = (float)(tpp * FH_FP);
  float dh = was_on ? (float)applied_fp / fp_per_ppm : live_ok ? (float)live_fp / fp_per_ppm : 0.0f;
  float dl = lp;
  if (dh >  99.99f) dh =  99.99f;
  if (dh < -99.99f) dh = -99.99f;
  if (dl >  99.99f) dl =  99.99f;
  if (dl < -99.99f) dl = -99.99f;
  tc_disp_hse = dh;  tc_disp_hse_ok = have_h || (was_on ? hold_ok : live_ok);
  tc_disp_lse = dl;  tc_disp_lse_ok = have_l;
  tc_disp_state = tc_steer_on ? 'A'
                : (!isnan(tc_cfg_hse[1]) || !isnan(tc_cfg_lse[0])) ? 'F'
                : (tc_hse_prior || tc_lse_prior) ? 'S'          // running on the warm-start seed (evolving)
                : (tc_learn && fresh < 5) ? 'L' : '-';

  // --- HSE steering: engage only in holdover, after first-ever fix, with something to apply.
  // Two corrections add, each optional:
  //  - freq_hold: the offset measured while locked (fh_estimate), frozen at the moment of loss. It is
  //    a step in RATE from the first steered second; the phase was true at the loss, so nothing jumps.
  //  - tc_apply: the temperature-driven CHANGE since loss (origin cancels; see tc_hse_delta). It is 0
  //    at the loss instant by construction and grows only as the die temperature moves.
  // Re-lock needs no unwinding beyond the LOAD restore (the per-edge phase snap owns lock).
  if ((tc_apply || freq_hold) && had_pps && fresh >= tc_engage_s) {
    if (!was_on) {
      t_loss = tf;                            // remember the temperature we lost GPS at
      hold_ok = freq_hold && fh_estimate(&hold_fp);
      if (!hold_ok) hold_fp = 0;
    }
    int32_t lim = (int32_t)tc_max_ppm * tpp * FH_FP;    // 200 ppm * 80 * 16 = 256000: no overflow
    float dtemp_t = 0;                                   // ticks/s, with its fraction
    _Bool have_t = tc_apply && tc_hse_delta(tf, t_loss, &dtemp_t);
    if (have_t || hold_ok) {
      float lim_t = (float)(lim / FH_FP);
      if (dtemp_t >  lim_t) dtemp_t =  lim_t;           // clamp BEFORE scaling
      if (dtemp_t < -lim_t) dtemp_t = -lim_t;
      int32_t target = hold_fp + (have_t ? (int32_t)lroundf(dtemp_t * (float)FH_FP) : 0);   // 1/16 tick: a tenth of a degree counts
      if (target >  lim) target =  lim;
      if (target < -lim) target = -lim;
      if (!was_on) { applied_fp = target; dither = 0; }
      else {                                  // gentle slew (temp-quantisation steps)
        int32_t slew = tpp * FH_FP / 4;       // 0.25 ppm per second
        int32_t d = target - applied_fp;
        if (d >  slew) d =  slew;
        if (d < -slew) d = -slew;
        applied_fp += d;
      }
      // Whole ticks for THIS second: the floor, plus one when the carried 1/16 remainders overflow,
      // so the long-run mean is applied_fp/16 ticks per second exactly.
      int32_t whole = applied_fp >= 0 ? applied_fp / FH_FP : -((-applied_fp + FH_FP - 1) / FH_FP);
      dither += applied_fp - whole * FH_FP;   // remainder, always [0,16)
      if (dither >= FH_FP) { dither -= FH_FP; whole++; }
      int32_t base = whole >= 0 ? whole / 1000 : -((-whole + 999) / 1000);
      int32_t rem  = whole - base * 1000;     // floor-division remainder, always [0,1000)
      __disable_irq();
      tc_load_base = (int32_t)tc_nom_load + base;
      tc_rem = rem;
      tc_steer_on = 1;
      __enable_irq();
      was_on = 1;
      tc_hold_active = hold_ok; tc_temp_active = have_t;
    } else if (was_on) {                      // nothing left to apply mid-holdover
      tc_steer_on = 0;
      SysTick->LOAD = tc_nom_load;
      tc_acc = 0;
      applied_fp = 0; dither = 0; hold_ok = 0;
      was_on = 0;
      tc_hold_active = 0; tc_temp_active = 0;
    }
  } else if (was_on) {
    tc_steer_on = 0;                          // flag first: the ISR stops writing LOAD...
    SysTick->LOAD = tc_nom_load;              // ...then restore the nominal period
    tc_acc = 0;
    applied_fp = 0; dither = 0; hold_ok = 0;
    was_on = 0;
    tc_hold_active = 0; tc_temp_active = 0;
  }

  // --- LSE -> RTC->CALR trim: power-loss insurance only (display time is HSE-driven) ---
  // calibrateRTC() owns CALR while locked (it runs from the PPS ISRs, which are silent now);
  // the first successful calibration after re-lock re-measures and overwrites this trim.
  // While PPS is fresh, forget our last write: calibrateRTC has since replaced CALR, so an
  // equal-valued model trim in the NEXT outage must not be skipped by the != guard.
  if (fresh <= 63) last_steps = 0x7FFF;
  if (tc_rtc && have_l && fresh > 63 && now - last_calr >= 60) {
    int32_t steps = (int32_t)lroundf(lp * (1048576.0f / 1000000.0f));  // ppm -> CALM steps
    if (steps >  255) steps =  255;
    if (steps < -255) steps = -255;
    if (steps != last_steps && !(RTC->ISR & RTC_ISR_RECALPF)) {
      // IRQ-off around the WPR unlock/write/relock triplet: PendSV's write_rtc() (runs each
      // second in holdover) does its own WPR sequence, and a preemption between our key
      // writes and the CALR store would leave the store silently ignored.
      __disable_irq();
      __HAL_RTC_WRITEPROTECTION_DISABLE(&hrtc);
      RTC->CALR = 0x100 + steps;              // same midpoint convention as calibrateRTC
      __HAL_RTC_WRITEPROTECTION_ENABLE(&hrtc);
      __enable_irq();
      last_steps = steps;
      last_calr = now;                        // note: BKP31R deliberately NOT updated
    }
  }
}
