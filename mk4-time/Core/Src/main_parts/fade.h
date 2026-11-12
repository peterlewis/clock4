// main_parts/fade.h — the significance fade: the bound on the display's time error in a holdover.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

float holdover_u_us = 0.0f;                 // last computed 3σ time-interval-error bound U(τ), µs

// SIGNIFICANCE FADE: the bound U(t) on the display's time error t seconds into a holdover. A
// sub-second digit fades as U approaches half its place value and is dark once U passes it.
//
// The first bound was a guess: 0.02 ppm * sqrt(age of the last calibration), three-sigma. Against 24
// days of 1 Hz records from a production Mk IV (pccd's flight recorder; 46,000 replayed GPS losses)
// it was 6 to 350 times larger than the real error. The ms digit went dark about 6 minutes into a
// holdover, when it was in fact right for about 50 minutes, and for 2.5 hours with freq_hold.
//
// What the data shows, and what this bound is now built from:
//  - THE OFFSET. Unsteered, the error is almost exactly (the oscillator's offset) * t: 99.7% of
//    losses stayed under 0.17 ppm * t. The clock measures that offset (fh_estimate), so it is a
//    known term, not a margin: charged in full when it is left in, and not at all when freq_hold is
//    steering it out.
//  - THE WANDER. With the offset removed, what remains grows as t^1.5, a random-walking frequency:
//    err = a * t^1.5, with a = 1.5e-10 per root-second on that clock from minutes out to hours.
//    Each clock learns its own a (fw_var, in freq_hold.h). The learned value is never trusted below
//    FADE_A_FLOOR, that clock's long-run level: with no floor a quiet day talked the bound down and
//    the next thermal event broke it in 0.7% of cases. Before 2.3 h of lock, twice that level.
//  - THE FACTOR. FADE_K = 6.5, not 3: the wander is bursty, not Gaussian. With these constants the
//    real error exceeded the bound in 0.001% of 102,000 replayed cases, steered and unsteered,
//    with and without tc_apply, from 5 minutes to 24 hours.
//  - Beyond a few hours the real wander levels off (it follows the day) while t^1.5 keeps growing,
//    so the bound is 3 to 6 times loose at 12 to 24 h. That errs toward dark, the honest side.
// The temperature model's own residual still counts, as before, but only while tc_apply is actually
// steering from it. One clock supplied the numbers; a noisier one widens its bound itself through
// the learned wander, and only a quieter one is held to the floor.
#define FADE_K           6.5f
#define FADE_C0_US       1.0f                 // PPS edge jitter and scheduling at the moment of loss
#define FADE_SE          0.25f                // ticks/s: how well an 80 s estimate knows the frequency
#define FADE_A_FLOOR     0.1475f              // (ticks/s)^2 at 1024 s  ==  a = 1.5e-10 per root-second at 80 MHz
#define FADE_A_DEFAULT   0.59f                // twice that level, until the clock has learned its own
#define FADE_B_UNKNOWN   10.0f                // ppm: an oscillator never yet measured against GPS
static float fade_wander_var(void){
  if (fw_count < FW_MIN) return FADE_A_DEFAULT;
  return fw_var > FADE_A_FLOOR ? fw_var : FADE_A_FLOOR;
}

static void computeHoldoverFade(void){
  uint32_t age = (uint32_t)currentTime - last_pps_time;              // holdover seconds
  float tau = (float)age;
  float tpp = (float)tc_tpp(); if (tpp <= 0.0f) tpp = 80.0f;         // ticks/s per ppm

  // The oscillator's offset that is NOT being steered out, in ticks per second.
  int32_t e_fp = 0;
  _Bool measured = fh_estimate(&e_fp);
  float y = measured ? fabsf((float)e_fp / (float)FH_FP) : FADE_B_UNKNOWN * tpp;
  float left_in, start_us;
  if (tc_hold_active) {                      // freq_hold has it: only the seconds before steering engaged count
    left_in  = 0.0f;
    start_us = y / tpp * (float)(age < tc_engage_s ? age : tc_engage_s);
  } else {
    left_in  = y;
    start_us = 0.0f;
  }

  // The wander: K * sqrt(estimate error^2 + a^2 * t), in ticks per second averaged over the holdover.
  float wander = FADE_K * sqrtf(FADE_SE * FADE_SE + fade_wander_var() / FW_WINDOW_S * tau);

  // The temperature model's own error, while it is steering (and a penalty beyond what it learned).
  float model = 0.0f;
  if (tc_temp_active) {
    model = 3.0f * tc_hse_resid;                                    // HSE residual, ticks/s
    int t = die_temp_c;
    int over = t < tc_hse_tmin ? tc_hse_tmin - t : t > tc_hse_tmax ? t - tc_hse_tmax : 0;
    if (over > 0) model += 3.0f * 0.3f * (float)over * tpp;         // unvalidated beyond coverage
  }

  float U_us = FADE_C0_US + start_us + (left_in + wander + model) / tpp * tau;   // ppm * s = us
  holdover_u_us = U_us;                                             // publish for read-back / display

  // Half place values (µs): 0.1 s, 0.01 s, 0.001 s. Fade band β. b_k = (h−U)/(β·h), clamped 0..1.
  static const float h_us[3] = { 50000.0f, 5000.0f, 500.0f };
  const float beta = 0.4f;
  for (int k = 0; k < 3; k++){
    float b = (h_us[k] - U_us) / (beta * h_us[k]);
    if (b < 0.0f) b = 0.0f; else if (b > 1.0f) b = 1.0f;
    digit_bright[k] = (uint8_t)(b * (float)FADE_MAX + 0.5f);
  }
  digit_bright[3] = digit_bright[0];   // the decimal point dies with the 0.1 s digit
}
