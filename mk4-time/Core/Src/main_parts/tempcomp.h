// main_parts/tempcomp.h — learning the oscillators against temperature: samples, fits, seeds, the models.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

// --- Temperature compensation (opt-in) ------------------------------------------------------
// Learns ppm-vs-die-temperature for both oscillators while GPS-locked (tc_learn), then during
// GPS-loss holdover steers the SysTick timebase from the HSE model (tc_apply) and optionally
// trims RTC->CALR from the LSE model (tc_rtc) so the battery RTC hands over better time across
// a power loss. "tc_dump = on" over serial prints the learned coefficients as ready-to-paste
// config lines; non-NAN tc_hse_a/tc_lse_a in config freeze the model (config overrides learning).
// All defaults off: with none of the keys set, behaviour is identical to stock.
// Config-key scalars are written from the USB OTG ISR (parseConfigString) and read by the
// main loop: volatile, matching the pps_ts_enabled precedent.
volatile _Bool tc_learn = 0, tc_apply = 0, tc_rtc = 0;
volatile int16_t  tc_t0 = 40;              // model centre temperature (°C)
// Frozen coefficients (ppm units at tc_t0). Elements are single-word (atomic) reads/writes;
// consumers snapshot each element once. HSE has NO 'a': its learned origin is arbitrary and
// steering uses temperature differences only, so freezing needs just b (and optionally c).
float tc_cfg_hse[3] = {NAN, NAN, NAN};     // [0] unused, [1] ppm/°C, [2] ppm/°C²
float tc_cfg_lse[3] = {NAN, NAN, NAN};     // absolute: ppm, ppm/°C, ppm/°C²
volatile _Bool tc_reset_pending = 0;       // "tc_reset = on" over serial -> forget everything learned (serviced in the main loop)

// Learned state (main-loop only). 2 °C bins spanning die temp -8..71 °C; sums are bounded by
// the halving-at-32768 aging rule (max |sum| ~ 6400*32768 < 2^31), so int32 cannot overflow.
struct tc_bin { int32_t hse_sum, lse_sum; uint16_t hse_n, lse_n; };
struct tc_bin tc_bins[40];
float tc_hse_m[3], tc_lse_m[3];            // learned models (ppm at powers of T - tc_t0)
_Bool tc_hse_valid = 0, tc_lse_valid = 0;
int16_t tc_hse_tmin = 0, tc_hse_tmax = 0;  // learned coverage: model is clamped to this range
int16_t tc_lse_tmin = 0, tc_lse_tmax = 0;
uint32_t tc_n_hse = 0, tc_n_lse = 0;       // lifetime sample counts (display + dump)
// Weighted-RMS residual of each learned fit (model vs bin means): the model's OWN error, in its
// fit units (HSE: SysTick ticks/s; LSE: ppm). This is the holdover-fade uncertainty's σ_temp source
// — how far the temperature model actually is from the measured data, not a guess.
float tc_hse_resid = 0, tc_lse_resid = 0;

// Warm-start (seed-and-evolve). A previously-learned model — the last tc_dump, written back into
// config.txt by the host (the firmware never writes the filesystem; the QSPI drive is host-owned) —
// is reloaded at boot as an evolving PRIOR rather than a hard freeze. The clock is temperature-
// compensated from the first second and keeps refining: tc_hse_prior/tc_lse_prior hold the model
// ORDER (1 const, 2 linear, 3 quadratic) currently carried by the seed, and tc_fit keeps that model
// until real learning supports a fit at least as rich, then hands over. tc_seed = off leaves the
// existing freeze path (tc_hse_b/… as asserted constants) untouched.
volatile _Bool   tc_seed = 0;                 // config: warm-start from the seeded coefficients + evolve
volatile int16_t tc_seed_lo = 0, tc_seed_hi = 0;   // seed coverage (die °C): bounds the prior — never extrapolated
uint8_t tc_hse_prior = 0, tc_lse_prior = 0;   // seed model order still held (0 = handed over to real data)
_Bool tc_seed_done = 0;                        // one-shot: seed once per power-on (BSS-cleared at reset)
// Serial "tc_seed = on" arms this (ISR-side single-word write); tc_housekeeping consumes it in the
// MAIN LOOP, so the learned-state contract ("main-loop only") holds and a paste seeds exactly once,
// after all its coefficient lines have parsed (send "tc_seed = on" last — the order tc_dump prints).
volatile _Bool tc_seed_pending = 0;

// Everything below runs in the MAIN LOOP only (float allowed, calibrateRTC precedent). The tick
// ISRs see just three precomputed int32s via the tc_steer_* handoff in the timetick() hook.

static uint32_t tc_nom_load = 0;    // SysTick->LOAD captured before any steering (80 MHz: 79999)

// Ticks per ppm, derived from the captured nominal period so no core-clock assumption is baked
// in: one second is (LOAD+1)*1000 ticks, so 1 ppm = (LOAD+1)/1000 ticks (80 at 80 MHz).
// Verified against the live unit: $PMTXTS reports load=79999 (10 MHz TCXO -> PLL -> 80 MHz).
static int32_t tc_tpp(void){ return (int32_t)((tc_nom_load + 1) / 1000); }

static int tc_bin_i(int t){ int i = (t + 8) / 2; return i < 0 ? 0 : (i > 39 ? 39 : i); }

// One HSE sample per GPS-locked second. The PPS ISRs re-zero the ms cascade at every edge
// (SysTick->VAL reload + counter reset), so each capture is already a SELF-CONTAINED one-second
// accumulation: pos = const + tpp·ppm(T), where const is a fixed capture/reload offset and
// tpp = ticks per ppm. Measured on the live unit: pos = 79925.8 ± 0.67 ticks (~8 ns RMS) — the
// constant dominates and is unknowable from lock data alone, so the model is learned in ticks
// with an ARBITRARY ORIGIN, rebased to the first accepted sample to keep bin sums small. Its
// differences over temperature are exact, and holdover steering only ever applies
// model(T_now) − model(T_at_loss), from which the origin cancels. (An earlier draft differenced
// consecutive captures — but the per-edge cascade reset makes that identically ~0; verified on
// hardware: dpos = 0.05 ± 1.1 ticks.)
static int32_t tc_e0 = 0;                     // origin rebase: first accepted sample
static _Bool   tc_e0_set = 0;
static int32_t tc_ema = 0;                    // slow tracker for the glitch gate
static void tc_hse_learn(void){
  static uint32_t last_seq = 0;
  static uint8_t  warm = 0;

  __disable_irq();                            // tear-free copy (emitPPSTimestamp pattern)
  uint32_t seq   = pps_cap.seq;
  uint16_t subms = pps_cap.subms;
  uint32_t st    = pps_cap.systick;
  int16_t  temp  = pps_cap.temp;
  uint8_t  flags = pps_cap.flags;
  __enable_irq();

  if (seq == last_seq) return;                // no new edge since last pass
  _Bool contiguous = (seq == last_seq + 1);
  last_seq = seq;

  if ((flags & 0x3) != 0x3 || subms > 999) { warm = 0; return; }
  if (!contiguous) { warm = 0; return; }      // edges were missed: settle again
  if (warm < 10) { warm++; return; }          // settle after (re)acquisition

  int32_t half = (int32_t)(tc_nom_load + 1) * 500;   // half a second in ticks
  int32_t e = (int32_t)subms * (int32_t)(tc_nom_load + 1)
            + (int32_t)tc_nom_load - (int32_t)st;    // this second's accumulation (+ const)
  if (e >  half) e -= 2 * half;               // fold the origin into ±half a second
  if (e < -half) e += 2 * half;

  int32_t tpp = tc_tpp();                     // ticks per ppm (80 at 80 MHz)
  if (!tc_e0_set) { tc_e0 = e; tc_ema = 0; tc_e0_set = 1; }
  e -= tc_e0;                                 // arbitrary-origin rebase (keeps sums int32-safe)
  if (e - tc_ema > 100 * tpp || e - tc_ema < -100 * tpp) return; // >100 ppm step: glitch
  if (e > 30000 || e < -30000) return;        // hard cap so 32768·|e| can never overflow int32
  tc_ema += (e - tc_ema) / 16;

  struct tc_bin *b = &tc_bins[tc_bin_i(temp)];
  if (b->hse_n >= 8) {                        // per-bin outlier gate: 10 ppm off the mean
    int32_t d = e - b->hse_sum / (int32_t)b->hse_n;
    if (d > 10 * tpp || d < -10 * tpp) return;
  }
  if (b->hse_n >= 32768) { b->hse_sum /= 2; b->hse_n /= 2; }   // overflow-proof aging
  b->hse_sum += e; b->hse_n++;
  tc_n_hse++;
}

// One LSE sample per successful RTC calibration: calibrateRTC only advances the BKP31R stamp
// on an in-range 63 s measurement, so watching the stamp inherits its validity gate for free.
static void tc_lse_learn(void){
  static uint32_t seen = 0;
  uint32_t cal = rtc_last_calibration;
  if (cal == seen) return;
  _Bool first = (seen == 0);
  seen = cal;
  if (first) return;                          // boot-time stamp, not a fresh measurement
  int32_t v = debug_rtc_val;                  // raw LSE cycle error over CAL_PERIOD (63 s)
  if (v > 1000 || v < -1000) return;
  struct tc_bin *b = &tc_bins[tc_bin_i(die_temp_c)];
  if (b->lse_n >= 32768) { b->lse_sum /= 2; b->lse_n /= 2; }
  b->lse_sum += v; b->lse_n++;
  tc_n_lse++;
}

// Solve A·x = y for a 3x3 symmetric system by Gaussian elimination with partial pivoting.
static _Bool tc_gauss3(float A[3][3], float y[3], float x[3]){
  int p[3] = {0, 1, 2};
  for (int c = 0; c < 3; c++){
    int best = c;
    for (int r = c + 1; r < 3; r++)
      if (fabsf(A[p[r]][c]) > fabsf(A[p[best]][c])) best = r;
    int t = p[c]; p[c] = p[best]; p[best] = t;
    if (fabsf(A[p[c]][c]) < 1e-9f) return 0;
    for (int r = c + 1; r < 3; r++){
      float f = A[p[r]][c] / A[p[c]][c];
      for (int k = c; k < 3; k++) A[p[r]][k] -= f * A[p[c]][k];
      y[p[r]] -= f * y[p[c]];
    }
  }
  for (int c = 2; c >= 0; c--){
    float s = y[p[c]];
    for (int k = c + 1; k < 3; k++) s -= A[p[c]][k] * x[k];
    x[c] = s / A[p[c]][c];
  }
  return 1;
}

// Weighted least-squares fit of y(T) = a + b·x + c·x², x = T - tc_t0, over bin means.
// Falls back quadratic -> linear -> constant as temperature coverage thins. `scale` converts
// bin units (HSE: 1.0 — model stays in ticks, arbitrary origin; LSE: raw 63 s cal cycles → ppm).
// A fit is only accepted if every coefficient is finite: a near-singular system can pass the
// pivot threshold yet overflow to Inf/NaN, and NaN must never reach the steering or display.
static _Bool tc_fin3(const float m[3]){ return isfinite(m[0]) && isfinite(m[1]) && isfinite(m[2]); }

// Returns the ACHIEVED model order: 3 quadratic, 2 linear, 1 constant, 0 no fit. (The order lets the
// warm-start prior be preserved until real data supports a fit at least as rich — see tc_fit.)
static int tc_fit_one(_Bool lse, float scale, uint16_t n_quad, float m[3],
                        int16_t *tmin_out, int16_t *tmax_out){
  float S[5] = {0,0,0,0,0}, T[3] = {0,0,0};
  float S0a = 0, T0a = 0;                     // all-samples weighted mean (constant fallback)
  int nb = 0, tmin = 127, tmax = -128;
  int tmin_a = 127, tmax_a = -128;

  for (int i = 0; i < 40; i++){
    uint16_t n  = lse ? tc_bins[i].lse_n   : tc_bins[i].hse_n;
    if (!n) continue;
    int32_t sum = lse ? tc_bins[i].lse_sum : tc_bins[i].hse_sum;
    int   t = i * 2 - 8;                      // bin low edge; bin holds {t, t+1}
    float y = ((float)sum / (float)n) * scale;
    float w = (float)n;
    S0a += w; T0a += w * y;
    if (t < tmin_a) tmin_a = t;
    if (t > tmax_a) tmax_a = t;
    if (n < n_quad) continue;                 // curve terms only from well-filled bins
    float x = ((float)t + 0.5f) - (float)tc_t0;   // true bin centre: t + 0.5
    nb++;
    if (t < tmin) tmin = t;
    if (t > tmax) tmax = t;
    S[0] += w;         S[1] += w*x;       S[2] += w*x*x;
    S[3] += w*x*x*x;   S[4] += w*x*x*x*x;
    T[0] += w*y;       T[1] += w*x*y;     T[2] += w*x*x*y;
  }

  if (nb >= 3 && (tmax - tmin) >= 6) {        // quadratic
    float A[3][3] = {{S[0],S[1],S[2]},{S[1],S[2],S[3]},{S[2],S[3],S[4]}};
    float yv[3]   = {T[0],T[1],T[2]};
    if (tc_gauss3(A, yv, m) && tc_fin3(m)) { *tmin_out = tmin; *tmax_out = tmax; return 3; }
  }
  if (nb >= 2 && (tmax - tmin) >= 4) {        // linear
    float det = S[0]*S[2] - S[1]*S[1];
    if (fabsf(det) > 1e-9f){
      m[0] = (T[0]*S[2] - T[1]*S[1]) / det;
      m[1] = (S[0]*T[1] - S[1]*T[0]) / det;
      m[2] = 0;
      if (tc_fin3(m)) { *tmin_out = tmin; *tmax_out = tmax; return 2; }
    }
  }
  if (S0a >= (lse ? 8.0f : 60.0f)) {          // constant: the dominant fixed offset
    m[0] = T0a / S0a; m[1] = 0; m[2] = 0;
    if (tc_fin3(m)) { *tmin_out = tmin_a; *tmax_out = tmax_a; return 1; }
  }
  return 0;
}

static float tc_poly(const float m[3], float x){ return m[0] + m[1]*x + m[2]*x*x; }

// Weighted-RMS residual of a fitted model over every populated bin (not only the curve-eligible
// ones): sqrt( Σ n·(bin_mean − model(T))² / Σ n ), in the fit's units. Runs as a second pass, so
// it never perturbs the fit itself; 40 bins, at most once per 5 min alongside tc_fit().
// `center` removes the weighted-mean of the residuals before the RMS — i.e. an ORIGIN-INVARIANT error.
// Needed for HSE: its model origin (m[0]) is arbitrary, so while a warm-start seed is held (m[0]=0) the
// real bins carry a different DC constant (the tc_e0 rebase); the raw offset would swamp the RMS and
// corrupt the holdover-fade σ_temp. Mean-centring measures only how well the SLOPE/CURVATURE match,
// which is all HSE cares about. LSE (absolute ppm, real m[0]) passes center=0 — its offset is real error.
static float tc_fit_resid(_Bool lse, float scale, const float m[3], _Bool center){
  float sw = 0, swr = 0, swr2 = 0;
  for (int i = 0; i < 40; i++){
    uint16_t n = lse ? tc_bins[i].lse_n : tc_bins[i].hse_n;
    if (!n) continue;
    int32_t sum = lse ? tc_bins[i].lse_sum : tc_bins[i].hse_sum;
    float y = ((float)sum / (float)n) * scale;
    float x = ((float)(i * 2 - 8) + 0.5f) - (float)tc_t0;   // bin centre − model origin
    float r = y - tc_poly(m, x);
    sw += (float)n; swr += (float)n * r; swr2 += (float)n * r * r;
  }
  if (!(sw > 0) || !isfinite(swr2)) return 0.0f;
  if (center) swr2 -= swr * swr / sw;                        // Σn(r−r̄)² = Σn·r² − (Σn·r)²/Σn
  return (swr2 > 0 && isfinite(swr2)) ? sqrtf(swr2 / sw) : 0.0f;
}

// Refit both models from the bins. A warm-start prior (tc_*_prior != 0) is PRESERVED until real data
// supports a fit at least as rich as the seed's order — then real data takes over (prior cleared).
// With no prior held (the normal cold-learn case) this is the original behaviour: fit -> adopt/invalidate.
// The residual is always re-measured against whatever model is held; while a seed is still held with no
// real samples yet, tc_fit_resid returns 0 (no data) and the seed's CARRIED residual is kept.
static void tc_fit(void){
  int16_t tmn, tmx; float m[3];

  int oh = tc_fit_one(0, 1.0f, 64, m, &tmn, &tmx);
  if (oh >= tc_hse_prior) {                    // real data at least as rich as the prior -> adopt it
    tc_hse_valid = (oh > 0);
    if (oh) { tc_hse_m[0]=m[0]; tc_hse_m[1]=m[1]; tc_hse_m[2]=m[2]; tc_hse_tmin=tmn; tc_hse_tmax=tmx; }
    tc_hse_prior = 0;
  }
  if (tc_hse_valid) { float r = tc_fit_resid(0, 1.0f, tc_hse_m, 1); if (r > 0 || !tc_hse_prior) tc_hse_resid = r; }
  else tc_hse_resid = 0;

  const float lse_scale = 1e6f/(32768.0f*63.0f);
  int ol = tc_fit_one(1, lse_scale, 4, m, &tmn, &tmx);
  if (ol >= tc_lse_prior) {
    tc_lse_valid = (ol > 0);
    if (ol) { tc_lse_m[0]=m[0]; tc_lse_m[1]=m[1]; tc_lse_m[2]=m[2]; tc_lse_tmin=tmn; tc_lse_tmax=tmx; }
    tc_lse_prior = 0;
  }
  if (tc_lse_valid) { float r = tc_fit_resid(1, lse_scale, tc_lse_m, 0); if (r > 0 || !tc_lse_prior) tc_lse_resid = r; }
  else tc_lse_resid = 0;
}

// Warm-start the tempco model from the seeded coefficients (tc_seed = on). Loads tc_hse_b/c and
// tc_lse_a/b/c — the last tc_dump, persisted in config.txt by the host — as the LIVE model so the
// clock is temperature-compensated from the first second, records the seed's order (kept by tc_fit
// until real data is at least as rich), and seeds a conservative residual so holdover-fade stays
// honest before real samples arrive. Clearing the frozen slots is what turns a freeze into an
// evolving prior. Runs once per boot; on a later live config reload it only re-clears the reparsed
// coefficients so the freeze path can't silently re-activate over the evolving model.
static void tc_seed_apply(void){
  if (!tc_seed) return;
  if (tc_seed_done) {                         // live reload: keep evolving — don't re-freeze or re-seed
    tc_cfg_hse[0]=tc_cfg_hse[1]=tc_cfg_hse[2]=NAN;
    tc_cfg_lse[0]=tc_cfg_lse[1]=tc_cfg_lse[2]=NAN;
    return;
  }
  _Bool have_hse = !isnan(tc_cfg_hse[1]) || !isnan(tc_cfg_hse[2]);
  _Bool have_lse = !isnan(tc_cfg_lse[0]) || !isnan(tc_cfg_lse[1]) || !isnan(tc_cfg_lse[2]);
  if (!have_hse && !have_lse) return;         // seed enabled but no coefficients yet — wait for a reload
  tc_seed_done = 1;

  int16_t lo = tc_seed_lo, hi = tc_seed_hi;
  if (hi - lo < 4) { lo = (int16_t)(tc_t0 - 6); hi = (int16_t)(tc_t0 + 6); }   // sane span if none given
  // readConfigFile (hence this seed) runs at boot BEFORE the first tc_housekeeping, so tc_nom_load is
  // still 0 and tc_tpp() would return 0 — which would silently zero the tick-domain HSE model. Capture
  // the nominal SysTick period here (SystemClock_Config set it before the config load) — the same
  // capture tc_housekeeping() also performs.
  if (!tc_nom_load) tc_nom_load = SysTick->LOAD;
  float tpp = (float)tc_tpp();

  if (have_hse) {
    tc_hse_m[0] = 0.0f;                        // arbitrary origin — steering uses temperature differences
    tc_hse_m[1] = isnan(tc_cfg_hse[1]) ? 0.0f : tc_cfg_hse[1] * tpp;
    tc_hse_m[2] = isnan(tc_cfg_hse[2]) ? 0.0f : tc_cfg_hse[2] * tpp;
    tc_hse_tmin = lo; tc_hse_tmax = hi; tc_hse_valid = 1;
    tc_hse_prior = (!isnan(tc_cfg_hse[2]) && tc_cfg_hse[2] != 0.0f) ? 3 : 2;
    tc_hse_resid = 2.0f * tpp;                 // conservative (~2 ppm) until real data measures it
    tc_cfg_hse[0] = tc_cfg_hse[1] = tc_cfg_hse[2] = NAN;   // seed replaces freeze -> free to evolve
  }
  if (have_lse) {
    tc_lse_m[0] = isnan(tc_cfg_lse[0]) ? 0.0f : tc_cfg_lse[0];
    tc_lse_m[1] = isnan(tc_cfg_lse[1]) ? 0.0f : tc_cfg_lse[1];
    tc_lse_m[2] = isnan(tc_cfg_lse[2]) ? 0.0f : tc_cfg_lse[2];
    tc_lse_tmin = lo; tc_lse_tmax = hi; tc_lse_valid = 1;
    tc_lse_prior = (!isnan(tc_cfg_lse[2]) && tc_cfg_lse[2] != 0.0f) ? 3
                 : (!isnan(tc_cfg_lse[1]) && tc_cfg_lse[1] != 0.0f) ? 2 : 1;
    tc_lse_resid = 2.0f;
    tc_cfg_lse[0] = tc_cfg_lse[1] = tc_cfg_lse[2] = NAN;
  }
}

// LSE model (absolute ppm): non-NAN config a freezes it (the user asserted the values);
// otherwise the learned fit, clamped to its observed temperature range (no extrapolation).
// Config values are USB-ISR-written; snapshot each element once (single-word reads are atomic).
static _Bool tc_model_lse(int t, float *ppm){
  float a = tc_cfg_lse[0], b = tc_cfg_lse[1], c = tc_cfg_lse[2];
  if (!isnan(a)) {
    if (isnan(b)) b = 0;
    if (isnan(c)) c = 0;
    float x = (float)t - (float)tc_t0;
    *ppm = a + b*x + c*x*x;
    return isfinite(*ppm);
  }
  if (!tc_lse_valid) return 0;
  if (t < tc_lse_tmin) t = tc_lse_tmin;
  if (t > tc_lse_tmax) t = tc_lse_tmax;
  *ppm = tc_poly(tc_lse_m, (float)t - (float)tc_t0);
  return 1;
}

// HSE steering delta in TICKS between two temperatures. The learned model's origin is
// arbitrary (see tc_hse_learn), so only differences are meaningful — which is exactly what
// holdover needs: at GPS loss the display is phase-true, and the error that then accrues is
// the temperature-driven CHANGE of the oscillator, model(T_now) − model(T_loss). Frozen config
// coefficients are in ppm; a cancels in the difference, so only tc_hse_b/c are required.
static _Bool tc_hse_delta(float t_now, float t_ref, float *dticks){   // temperatures in degC, fractions welcome; result in ticks/s
  float b = tc_cfg_hse[1], c = tc_cfg_hse[2];
  if (!isnan(b)) {                            // frozen: b (and optionally c) from config
    if (isnan(c)) c = 0;
    float x1 = t_now - (float)tc_t0, x0 = t_ref - (float)tc_t0;
    float dppm = (b*x1 + c*x1*x1) - (b*x0 + c*x0*x0);
    if (!isfinite(dppm)) return 0;
    *dticks = dppm * (float)tc_tpp();
    return 1;
  }
  if (!tc_hse_valid) return 0;
  if (t_now < (float)tc_hse_tmin) t_now = (float)tc_hse_tmin;   // no extrapolation past learned coverage
  if (t_now > (float)tc_hse_tmax) t_now = (float)tc_hse_tmax;
  if (t_ref < (float)tc_hse_tmin) t_ref = (float)tc_hse_tmin;
  if (t_ref > (float)tc_hse_tmax) t_ref = (float)tc_hse_tmax;
  float d = tc_poly(tc_hse_m, t_now - (float)tc_t0)
          - tc_poly(tc_hse_m, t_ref - (float)tc_t0);
  if (!isfinite(d)) return 0;
  *dticks = d;                                // learned model is already in ticks
  return 1;
}
