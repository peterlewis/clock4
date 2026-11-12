// main_parts/dumps.h — the serial reports: tc_dump, $PMADEV, $PMHDEV, $PMSTAR.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

// "tc_dump = on" over serial: emit the learned model as ready-to-paste config.txt lines plus
// two checksummed $PMTXTC sentences (H and L — split so each fits NMEA_BUF_SIZE). One line per
// main-loop pass; each line is FORMATTED ONCE and only the CDC submit is retried on BUSY (float
// snprintf must not re-run thousands of times against the ISR's own float sprintf — newlib-nano
// shares one _reent). A stuck host aborts the dump after a bounded number of BUSY passes.
// HSE coefficients are printed in ppm/°C (per-degree slope b and curvature c, converted from
// the tick-domain model); the HSE 'a' term has an arbitrary instrument origin and is neither
// printed nor needed — steering uses temperature DIFFERENCES only (see tc_hse_delta).
static void tc_dump_step(void){
  static uint8_t  idx = 0;
  static int      dn = -1;                    // formatted length; -1 = line not built yet
  static uint16_t busy_ct = 0;
  static char     dline[NMEA_BUF_SIZE];
  if (!tc_dump_pending) return;
  if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) { tc_dump_pending = 0; idx = 0; dn = -1; return; }

  if (dn < 0) {                               // build the current line exactly once
    float tpp = (float)tc_tpp();
    int n = 0;
    switch (idx) {
      case 0:
        n = snprintf(dline, sizeof dline, "# tempcomp: hse n=%lu lse n=%lu, die %d..%d C, state %c\r\n",
                     (unsigned long)tc_n_hse, (unsigned long)tc_n_lse,
                     (int)tc_hse_tmin, (int)tc_hse_tmax, tc_disp_state);
        break;
      case 1: n = snprintf(dline, sizeof dline, "tc_t0 = %d\r\n", (int)tc_t0); break;
      case 2:                                 // HSE slope, ppm/degC (origin-free)
        if (tc_hse_valid) n = snprintf(dline, sizeof dline, "tc_hse_b = %.5f\r\n", (double)(tc_hse_m[1] / tpp));
        else              n = snprintf(dline, sizeof dline, "# tc_hse_b = ----\r\n");
        break;
      case 3:                                 // HSE curvature, ppm/degC^2
        if (tc_hse_valid) n = snprintf(dline, sizeof dline, "tc_hse_c = %.6f\r\n", (double)(tc_hse_m[2] / tpp));
        else              n = snprintf(dline, sizeof dline, "# tc_hse_c = ----\r\n");
        break;
      case 4: case 5: case 6: {               // LSE a/b/c, absolute ppm at tc_t0
        static const char nm[3] = {'a','b','c'};
        static const char *fm[3] = {"tc_lse_%c = %.4f\r\n", "tc_lse_%c = %.5f\r\n", "tc_lse_%c = %.6f\r\n"};
        int k = idx - 4;
        if (tc_lse_valid) n = snprintf(dline, sizeof dline, fm[k], nm[k], (double)tc_lse_m[k]);
        else              n = snprintf(dline, sizeof dline, "# tc_lse_%c = ----\r\n", nm[k]);
        break;
      }
      case 7: case 8: {                       // machine-parsable pair for the web app
        char body[72];
        int nb;
        if (idx == 7)
          nb = snprintf(body, sizeof body, "PMTXTC,H,%lu,%d,%d,%.5f,%.6f,%c",
                        (unsigned long)tc_n_hse, (int)tc_hse_tmin, (int)tc_hse_tmax,
                        (double)(tc_hse_valid ? tc_hse_m[1] / tpp : 0),
                        (double)(tc_hse_valid ? tc_hse_m[2] / tpp : 0), tc_hse_valid ? 'V' : '-');
        else
          nb = snprintf(body, sizeof body, "PMTXTC,L,%lu,%.4f,%.5f,%.6f,%c",
                        (unsigned long)tc_n_lse,
                        (double)(tc_lse_valid ? tc_lse_m[0] : 0), (double)(tc_lse_valid ? tc_lse_m[1] : 0),
                        (double)(tc_lse_valid ? tc_lse_m[2] : 0), tc_lse_valid ? 'V' : '-');
        if (nb < 0 || nb >= (int)sizeof body) { tc_dump_pending = 0; idx = 0; return; }
        uint8_t cks = 0;
        for (int i2 = 0; i2 < nb; i2++) cks ^= (uint8_t)body[i2];
        n = snprintf(dline, sizeof dline, "$%s*%02X\r\n", body, (unsigned)cks);
        break;
      }
      case 9: {                               // seed coverage — with "tc_seed = on" the paste warm-starts
        int lo = tc_hse_valid ? tc_hse_tmin : tc_lse_tmin;   // (and keeps evolving) instead of freezing
        int hi = tc_hse_valid ? tc_hse_tmax : tc_lse_tmax;
        if (tc_lse_valid) { if (tc_lse_tmin < lo) lo = tc_lse_tmin; if (tc_lse_tmax > hi) hi = tc_lse_tmax; }
        n = snprintf(dline, sizeof dline, "tc_seed_lo = %d\r\ntc_seed_hi = %d\r\n", lo, hi);
        break;
      }
    }
    if (idx == 10) {                          // freq_hold: the measured oscillator offset (a comment line: nothing to paste)
      int32_t e_fp = 0;
      if (!freq_hold)                 n = snprintf(dline, sizeof dline, "# freq_hold: off\r\n");
      else if (!fh_estimate(&e_fp))   n = snprintf(dline, sizeof dline, "# freq_hold: ---- (needs 48 s of GPS lock)\r\n");
      else {                                  // ppm to 4 places, in integers (no float printf here)
        int32_t fp1 = tc_tpp() * FH_FP;       // 1/16 ticks per ppm
        long q = fp1 > 0 ? ((long)(e_fp < 0 ? -e_fp : e_fp) * 10000L + fp1 / 2) / fp1 : 0;
        n = snprintf(dline, sizeof dline, "# freq_hold: %c%ld.%04ld ppm over %u s of lock\r\n",
                     e_fp < 0 ? '-' : '+', q / 10000L, q % 10000L, (unsigned)(fh_count * FH_BLOCK_S));
      }
    }
    if (idx == 11) {                          // the significance-fade bound: what it rests on, and where it stands at an hour
      float a_var = fade_wander_var();
      float tpp_f = (float)tc_tpp(); if (tpp_f <= 0.0f) tpp_f = 80.0f;
      // wander level a in units of 1e-12 per root-second, and the hour's wander term in us; integers only
      long a12 = (long)(sqrtf(a_var / FW_WINDOW_S) / (tpp_f * 1e6f) * 1e12f + 0.5f);
      long u1h = (long)(FADE_K * sqrtf(FADE_SE * FADE_SE + a_var / FW_WINDOW_S * 3600.0f) / tpp_f * 3600.0f + 0.5f);
      n = snprintf(dline, sizeof dline, "# fade: wander %ld e-12/rt-s (%s, %u windows), +-%ld us at 1 h plus any offset left in\r\n",
                   a12, fw_count < FW_MIN ? "default" : fw_var > FADE_A_FLOOR ? "learned" : "floor", (unsigned)fw_count, u1h);
    }
    if (n <= 0 || n >= (int)sizeof dline) { tc_dump_pending = 0; idx = 0; dn = -1; return; }
    dn = n;
    busy_ct = 0;
  }

  __disable_irq();                            // serialise against the ISR NMEA passthrough
  uint8_t r = CDC_Copy_Transmit((uint8_t*)dline, (uint16_t)dn);
  __enable_irq();
  if (r == USBD_BUSY) {                       // retry the SUBMIT only; the line stays built
    if (++busy_ct > 5000) { tc_dump_pending = 0; idx = 0; dn = -1; }  // host stopped reading
    return;
  }
  dn = -1;
  if (++idx > 11) { idx = 0; tc_dump_pending = 0; }
}

// "adev_dump = on" over serial: emit the whole live Allan-deviation curve as ONE checksummed
// sentence — $PMADEV,<valid>,<noct>,<sigma_0>,..,<sigma_{noct-1}>*CC — with sigma_k = sigma_y(tau)
// at tau = 2^k s (fractional frequency, %.2e). valid = contiguous phase samples behind the estimate
// (confidence). Fresh cache computed here (thread context) so the dump works in any display mode.
// Serviced from the main loop; retries only the CDC submit on USBD_BUSY, like tc_dump_step/emitPPS.
static void adev_dump_step(void){
  static int      dn = -1;                    // formatted length; -1 = not built yet
  static uint16_t busy_ct = 0;
  static char     dline[160];                 // epoch+tau0 header + 11 octaves outgrow NMEA_BUF_SIZE
  if (!adev_dump_pending) return;
  if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) { adev_dump_pending = 0; dn = -1; return; }

  if (dn < 0) {
    adev_reduce();                            // fresh octave cache (never in an ISR)
    uint8_t  noct  = adev_noct;
    uint16_t valid = adev_valid;
    char body[128];
    // Self-describing for machine consumers: epoch (unix s) + tau0 (s) lead the sentence, so tau_k =
    // tau0 * 2^k needs no out-of-band spec and stale sentences are detectable.
    int nb = snprintf(body, sizeof body, "PMADEV,%lu,1,%u,%u",
                      (unsigned long)(uint32_t)currentTime, (unsigned)valid, (unsigned)noct);
    if (nb < 0 || nb >= (int)sizeof body) { adev_dump_pending = 0; return; }
    for (uint8_t k = 0; k < noct; k++) {
      int t = snprintf(body + nb, sizeof body - nb, ",%.2e", (double)adev_sigma_cache[k]);
      if (t < 0 || t >= (int)(sizeof body - nb)) { adev_dump_pending = 0; return; }
      nb += t;
    }
    uint8_t cks = 0;
    for (int i = 0; i < nb; i++) cks ^= (uint8_t)body[i];
    int n = snprintf(dline, sizeof dline, "$%s*%02X\r\n", body, (unsigned)cks);
    if (n <= 0 || n >= (int)sizeof dline) { adev_dump_pending = 0; return; }
    dn = n; busy_ct = 0;
  }

  __disable_irq();                            // serialise against the ISR NMEA passthrough
  uint8_t r = CDC_Copy_Transmit((uint8_t*)dline, (uint16_t)dn);
  __enable_irq();
  if (r == USBD_BUSY) {                        // retry the SUBMIT only; the line stays built
    if (++busy_ct > 5000) { adev_dump_pending = 0; dn = -1; }
    return;
  }
  dn = -1; adev_dump_pending = 0;
}

// "hdev_dump = on": one $PMHDEV sentence — the Hadamard twin of $PMADEV (same shape: epoch, tau0,
// valid, noct, sigmas), computed on demand from the shared phase ring. Same CDC/BUSY-retry contract.
static void hdev_dump_step(void){
  static int      dn = -1;
  static uint16_t busy_ct = 0;
  static char     dline[160];
  if (!hdev_dump_pending) return;
  if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) { hdev_dump_pending = 0; dn = -1; return; }

  if (dn < 0) {
    uint8_t  noct  = hdev_noct();
    uint16_t valid = adev_valid;
    char body[128];
    int nb = snprintf(body, sizeof body, "PMHDEV,%lu,1,%u,%u",
                      (unsigned long)(uint32_t)currentTime, (unsigned)valid, (unsigned)noct);
    if (nb < 0 || nb >= (int)sizeof body) { hdev_dump_pending = 0; return; }
    for (uint8_t k = 0; k < noct; k++) {
      int t = snprintf(body + nb, sizeof body - nb, ",%.2e", (double)hdev_sigma_for_m(1u<<k));
      if (t < 0 || t >= (int)(sizeof body - nb)) { hdev_dump_pending = 0; return; }
      nb += t;
    }
    uint8_t cks = 0;
    for (int i = 0; i < nb; i++) cks ^= (uint8_t)body[i];
    int n = snprintf(dline, sizeof dline, "$%s*%02X\r\n", body, (unsigned)cks);
    if (n <= 0 || n >= (int)sizeof dline) { hdev_dump_pending = 0; return; }
    dn = n; busy_ct = 0;
  }

  __disable_irq();
  uint8_t r = CDC_Copy_Transmit((uint8_t*)dline, (uint16_t)dn);
  __enable_irq();
  if (r == USBD_BUSY) {
    if (++busy_ct > 5000) { hdev_dump_pending = 0; dn = -1; }
    return;
  }
  dn = -1; hdev_dump_pending = 0;
}

// "star_dump = on" over serial: the current soonest-transit list as ONE checksummed sentence —
// $PMSTAR,<n>,<name>,<sec_to_transit>,<transit_alt_deg>,..*CC. Fresh list computed here so it works
// in any display mode. Same CDC/BUSY-retry contract as adev_dump_step/emitPPSTimestamp.
static void star_dump_step(void){
  static int      dn = -1;
  static uint16_t busy_ct = 0;
  static char     dline[192];   // 8 entries x ",NAME,SSSSS,AA,D" + header outgrow NMEA_BUF_SIZE
  if (!star_dump_pending) return;
  if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) { star_dump_pending = 0; dn = -1; return; }

  if (dn < 0) {
    star_update();                             // fresh transit list (never in an ISR)
    uint8_t n = star_ncache;
    char body[176];
    int nb = snprintf(body, sizeof body, "PMSTAR,%u", (unsigned)n);
    if (nb < 0 || nb >= (int)sizeof body) { star_dump_pending = 0; return; }
    for (uint8_t k = 0; k < n; k++) {
      long rem = (long)star_cache[k].epoch - (long)currentTime; if (rem < 0) rem = 0;
      int t = snprintf(body + nb, sizeof body - nb, ",%.4s,%ld,%d,%c",
                       star_cache[k].nm, rem, (int)star_cache[k].alt, star_cache[k].dir);
      if (t < 0 || t >= (int)(sizeof body - nb)) { star_dump_pending = 0; return; }
      nb += t;
    }
    uint8_t cks = 0;
    for (int i = 0; i < nb; i++) cks ^= (uint8_t)body[i];
    int nn = snprintf(dline, sizeof dline, "$%s*%02X\r\n", body, (unsigned)cks);
    if (nn <= 0 || nn >= (int)sizeof dline) { star_dump_pending = 0; return; }
    dn = nn; busy_ct = 0;
  }

  __disable_irq();
  uint8_t r = CDC_Copy_Transmit((uint8_t*)dline, (uint16_t)dn);
  __enable_irq();
  if (r == USBD_BUSY) { if (++busy_ct > 5000) { star_dump_pending = 0; dn = -1; } return; }
  dn = -1; star_dump_pending = 0;
}
