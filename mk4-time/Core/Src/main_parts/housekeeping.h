// main_parts/housekeeping.h — the timing subsystems' once-per-pass work in the main loop.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

// Main-loop entry point, called every pass. With every tc key at its default this reduces to
// four flag checks — no measurable cost, no behaviour change.
SMALL_CODE void tc_housekeeping(void){
  if (!tc_nom_load) tc_nom_load = SysTick->LOAD;       // capture the nominal period once

  // Serial warm-start (armed by the "tc_seed = on" line) + the evolving seed's freeze guard: with
  // the seed applied, the same call just re-NANs any tc_hse_*/tc_lse_* coefficients a serial line
  // reparsed, so the frozen path can't silently reactivate over the evolving model.
  if (tc_seed_pending) { tc_seed_pending = 0; tc_seed_apply(); }
  else if (tc_seed_done) tc_seed_apply();

  if (tc_reset_pending) {
    memset(tc_bins, 0, sizeof tc_bins);
    tc_hse_valid = tc_lse_valid = 0;
    tc_hse_prior = tc_lse_prior = 0;          // drop any held warm-start prior: reset is a cold restart
    tc_n_hse = tc_n_lse = 0;
    tc_e0_set = 0; tc_ema = 0;                // new origin rebase with the next sample
    fw_break(); fw_var = 0; fw_count = 0;     // the learned wander too: the fade's bound returns to its default
    tc_reset_pending = 0;
    if (tc_persist) tc_forget_pending = 1;    // a reset also wipes the retained model from flash
  }

  if (tc_learn) {
    tc_hse_learn();
    tc_lse_learn();
    static uint32_t last_fit = 0;
    uint32_t now = (uint32_t)currentTime;
    if (now - last_fit >= 300) { last_fit = now; tc_fit(); tc_model_check(); }   // refit at most every 5 min, then check whether to re-persist
  }

  // tc_steer_on in the gate: the governor owns DISENGAGE, so it must stay reachable even if
  // the user turns every tc key off while steering is engaged mid-holdover — otherwise the
  // tick ISR would keep applying a stale frozen correction forever.
  if (freq_hold || significance_fade) fh_learn();   // the oscillator against GPS, one sample per locked second (the fade's bound needs it too)

  if (tc_learn || tc_apply || tc_rtc || freq_hold || tc_steer_on || displayMode == MODE_TEMPCOMP) tc_governor();

  if (significance_fade) {   // recompute the per-digit fade once per second while enabled
    static uint32_t last_fade = 0;
    uint32_t now = (uint32_t)currentTime;
    if (now != last_fade) { last_fade = now; computeHoldoverFade(); }
  }

  tc_dump_step();
  menu_dump_step();
  menu_export_step();  // the menu's choices as config.txt lines, after a save or on request
  cw_step();           // ...and written into config.txt by the clock itself, when no computer is there to do it
}
