// main_parts/config.h — config.txt and serial config lines: the parser, the defaults table, the file loader.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

_Bool delayedPostConfigCleanup = 0;   // a serial config line arrived (in the USB interrupt): the main loop runs postConfigCleanup
// Validated coefficient parse: garbage/'----'/empty leaves the value untouched (a pasted-back
// commented dump line must not freeze 0.0); an explicit "nan" parses and UNFREEZES the slot.
static void tc_parse_coeff(const char *v, float *out){
  char *end;
  float f = strtof(v, &end);
  if (end != v) *out = f;
}

_Bool truthy(char const* str){
  if (strcasecmp(str, "on")==0) return 1;
  if (strcasecmp(str, "enabled")==0) return 1;
  if (strcasecmp(str, "1")==0) return 1;
  return 0;
}

_Bool falsey(char const* str){
  if (strcasecmp(str, "off")==0) return 1;
  if (strcasecmp(str, "disabled")==0) return 1;
  if (strcasecmp(str, "0")==0) return 1;
  if (strcasecmp(str, "none")==0) return 1;
  return 0;
}

// Accept a float between 0.0 and 1.0, or an int from 0 to 4095
float parseBrightness(char *v, _Bool invert){
  if (!v[0]) return -1;
  float b = strtof(v, NULL);
  if (!isfinite(b) || b<0.0) return -1;
  if (b<=1.0 && v[1]=='.')
    return invert? (1.0-b) * 4095 : b*4095;
  if (b<=4095)
    return invert? 4095-b : b;
  return -1;
}

#define set_mode_enabled(mode, value) \
  do { if ((config.modes_enabled[mode] = truthy(value))) requestMode=mode; \
       if (!from_serial) cfg_modes_defined |= (1ull<<(mode)); } while(0)   /* menu-precedence tracking */

static uint8_t parseColonName(const char *value){
  if (strcasecmp(value, "solid") == 0)        return COLON_MODE_SOLID;
  if (strcasecmp(value, "heartbeat") == 0)    return COLON_MODE_HEARTBEAT;
  if (strcasecmp(value, "sawtooth") == 0)     return COLON_MODE_1PPS_SAWTOOTH;
  if (strcasecmp(value, "alt_sawtooth") == 0) return COLON_MODE_ALT_SAWTOOTH;
  if (strcasecmp(value, "toggle") == 0)       return COLON_MODE_TOGGLE;
  return COLON_MODE_SLOWFADE;
}

void parseConfigString(char *key, char *value, _Bool from_serial) {

  if (strcasecmp(key, "text") == 0) {

    strcpy(textDisplay, value);

  } else if (strcasecmp(key, "MATRIX_FREQUENCY") == 0) {

    setDisplayFreq(atoi(value));
    if (!from_serial) cfg_owns(KID_MATRIX_FREQ);   // config.txt keeps priority over the menu

  } else if (strcasecmp(key, "zone_override") == 0) {

    if (!value[0] || delayedLoadRules) return;

    strcpy(preloadRulesString, value);
    delayedLoadRules=1;
    ZDAbort();

  } else if (strcasecmp(key, "zone2") == 0) {

    if (!value[0]) {                                                          // empty -> clear (dashes)
      loadedZone2[0]=0; zone2_fixed=0; delayedLoadZone2=0;
    } else if (zone2_parse_fixed(value, &offset2)) {                          // "UTC" / "+HH:MM" literal: resolve now (no FATFS)
      zone2_fixed = 1; delayedLoadZone2 = 0;
      strncpy(loadedZone2, value, sizeof loadedZone2 - 1);
      loadedZone2[sizeof loadedZone2 - 1] = 0;
    } else if (!delayedLoadZone2) {                                          // IANA name: defer the FATFS load to checkDelayedLoadRules
      strncpy(preloadZone2, value, sizeof preloadZone2 - 1);
      preloadZone2[sizeof preloadZone2 - 1] = 0;
      delayedLoadZone2 = 1; loadedZone2[0] = 0;                               // dashes until resolved
    }

  } else if (strcasecmp(key, "brightness") == 0) {

    config.brightness_override = parseBrightness(value, 1);
    if (!from_serial) cfg_owns(KID_BRIGHTNESS);

  } else if (strcasecmp(key, "countdown_to") == 0) {

    //  support fractional seconds??
    struct tm t = {0};
    if( sscanf(value, "%d-%d-%dT%d:%d:%dZ", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec) >=3) {

      if (t.tm_year > 9999) return; // arbitrary cutoff, ~3e6 days
      t.tm_year -= 1900;
      t.tm_mon -= 1;

      config.countdown_to = mktime(&t) -1;

    }
  } else if (strcasecmp(key, "MODE_ISO8601_STD") == 0) {
    set_mode_enabled(MODE_ISO8601_STD, value);
  } else if (strcasecmp(key, "MODE_ISO_ORDINAL") == 0) {
    set_mode_enabled(MODE_ISO_ORDINAL, value);
  } else if (strcasecmp(key, "MODE_ISO_WEEK") == 0) {
    set_mode_enabled(MODE_ISO_WEEK, value);
  } else if (strcasecmp(key, "MODE_UNIX") == 0) {
    set_mode_enabled(MODE_UNIX, value);
  } else if (strcasecmp(key, "MODE_JULIAN_DATE") == 0) {
    set_mode_enabled(MODE_JULIAN_DATE, value);
  } else if (strcasecmp(key, "MODE_MODIFIED_JD") == 0) {
    set_mode_enabled(MODE_MODIFIED_JD, value);
  } else if (strcasecmp(key, "MODE_SHOW_OFFSET") == 0) {
    set_mode_enabled(MODE_SHOW_OFFSET, value);
  } else if (strcasecmp(key, "MODE_SHOW_TZ_NAME") == 0) {
    set_mode_enabled(MODE_SHOW_TZ_NAME, value);
  } else if (strcasecmp(key, "MODE_WEEKDAY") == 0) {
    set_mode_enabled(MODE_WEEKDAY, value);
  } else if (strcasecmp(key, "MODE_WEEKDA_DD") == 0) {
    set_mode_enabled(MODE_WEEKDA_DD, value);
  } else if (strcasecmp(key, "MODE_WDY_MM_DD") == 0) {
    set_mode_enabled(MODE_WDY_MM_DD, value);
  } else if (strcasecmp(key, "MODE_STANDBY") == 0) {
    set_mode_enabled(MODE_STANDBY, value);
  } else if (strcasecmp(key, "MODE_COUNTDOWN") == 0) {
    set_mode_enabled(MODE_COUNTDOWN, value);
  } else if (strcasecmp(key, "MODE_SATVIEW") == 0) {
    set_mode_enabled(MODE_SATVIEW, value);
  } else if (strcasecmp(key, "MODE_DEBUG_BRIGHTNESS") == 0) {
    set_mode_enabled(MODE_DEBUG_BRIGHTNESS, value);
  } else if (strcasecmp(key, "MODE_DEBUG_RTC") == 0) {
    set_mode_enabled(MODE_DEBUG_RTC, value);
  } else if (strcasecmp(key, "MODE_TEXT") == 0) {
    set_mode_enabled(MODE_TEXT, value);
  } else if (strcasecmp(key, "MODE_VBAT") == 0) {
    set_mode_enabled(MODE_VBAT, value);
  } else if (strcasecmp(key, "MODE_DISPLAYTEST") == 0) {
    set_mode_enabled(MODE_DISPLAYTEST, value);
  } else if (strcasecmp(key, "MODE_TTFF") == 0) {
    set_mode_enabled(MODE_TTFF, value);
#ifdef NONCOMPLIANT_DATE_MODES
  } else if (strcasecmp(key, "MODE_DDMMYYYY") == 0) {
    set_mode_enabled(MODE_DDMMYYYY, value);
#endif
  } else if (strcasecmp(key, "MODE_FIRMWARE_CRC") == 0) {
    set_mode_enabled(MODE_FIRMWARE_CRC_D, value);
    set_mode_enabled(MODE_FIRMWARE_CRC_T, value);
  } else if (strcasecmp(key, "MODE_SUN") == 0) {
    set_mode_enabled(MODE_SUN, value);
  } else if (strcasecmp(key, "MODE_SUN_AZEL") == 0) {
    set_mode_enabled(MODE_SUN_AZEL, value);
  } else if (strcasecmp(key, "MODE_MOON") == 0) {
    set_mode_enabled(MODE_MOON, value);
  } else if (strcasecmp(key, "MODE_GRID") == 0) {
    set_mode_enabled(MODE_GRID, value);
  } else if (strcasecmp(key, "MODE_LATLON") == 0) {
    set_mode_enabled(MODE_LATLON, value);
  } else if (strcasecmp(key, "page_ms") == 0) {
    int v = atoi(value);
    config.page_ms = v < 0 ? 0 : (v > 65535 ? 65535 : v);   // fits uint16; 0 -> default
    if (!from_serial) cfg_owns(KID_PAGE_MS);
  } else if (strcasecmp(key, "Tolerance_time_1ms") == 0) {
    config.tolerance_1ms = atoi(value);
  } else if (strcasecmp(key, "Tolerance_time_10ms") == 0) {
    config.tolerance_10ms = atoi(value);
  } else if (strcasecmp(key, "Tolerance_time_100ms") == 0) {
    config.tolerance_100ms = atoi(value);
  } else if (strcasecmp(key, "fake_longitude") == 0) {
    config.fake_long = atof(value);
  } else if (strcasecmp(key, "fake_latitude") == 0) {
    config.fake_lat = atof(value);
  } else if (strcasecmp(key, "colon_mode") == 0) {

    colonModeCivil = parseColonName(value);
    if (!from_serial) cfg_owns(KID_COLON);

  } else if (strcasecmp(key, "colon_alt_mode") == 0) {

    colonModeAlt = parseColonName(value);   // shared by the alternate-timebase modes (ACOLON in the menu)
    colonAltExplicit = 1;
    if (!from_serial) cfg_owns(KID_COLON_ALT);

  } else if (strcasecmp(key, "nmea") == 0) {

    if (falsey(value)) {
      nmea_cdc_level = NMEA_NONE;
    } else if (strcasecmp(value, "rmc") == 0) {
      nmea_cdc_level = NMEA_RMC;
    } else nmea_cdc_level = NMEA_ALL;
    if (!from_serial) cfg_owns(KID_NMEA);

  } else if (strcasecmp(key, "pps") == 0) {

    pps_ts_enabled = truthy(value);   // emit a $PMTXTS timing sentence on each PPS edge
    if (!from_serial) cfg_owns(KID_PPS);

  } else if (strcasecmp(key, "cuckoo") == 0) {

    // One key, enable-and-select fused (CUCKOO_SPEC.md v2): the value names the hourly piece;
    // the quarter nod comes with it. off (default) disables both tiers. Unknown values fall
    // to off — a misspelt piece must not leave a stale one performing.
    uint8_t prev = cuckoo;
    if      (strcasecmp(value, "trust") == 0)        cuckoo = CK_TRUST;
    else                                             cuckoo = CK_OFF;   // off, and every parked piece name, and typos
    if (cuckoo != prev) cuckoo_abort();   // a change mid-piece ends it cleanly on the plain face
    if (!from_serial) cfg_owns(KID_CUCKOO);

  } else if (strcasecmp(key, "cuckoo_preview") == 0) {

    // Serial-only bench tool: play a piece ONCE, immediately, without touching the `cuckoo`
    // config. "on" previews the configured piece; a piece name previews that piece; "nod"
    // previews the quarter gesture. Never valid in config.txt (a file must not perform).
    if (from_serial) {
      if      (strcasecmp(value, "nod") == 0)       ck_preview(CK_NOD);
      else if (strcasecmp(value, "trust") == 0)     ck_preview(CK_TRUST);
      else if (truthy(value))                       ck_preview(cuckoo);
    }

  } else if (strcasecmp(key, "MODE_TEMPCOMP") == 0) {
    set_mode_enabled(MODE_TEMPCOMP, value);
  } else if (strcasecmp(key, "MODE_LST") == 0) {
    set_mode_enabled(MODE_LST, value);
  } else if (strcasecmp(key, "MODE_SOLAR") == 0) {
    set_mode_enabled(MODE_SOLAR, value);
  } else if (strcasecmp(key, "MODE_ADEV") == 0) {
    set_mode_enabled(MODE_ADEV, value);
  } else if (strcasecmp(key, "MODE_STAR") == 0) {
    set_mode_enabled(MODE_STAR, value);
  } else if (strcasecmp(key, "MODE_ZONE2") == 0) {
    set_mode_enabled(MODE_ZONE2, value);
  } else if (strcasecmp(key, "MODE_DARK") == 0) {
    set_mode_enabled(MODE_DARK, value);
  } else if (strcasecmp(key, "tc_learn") == 0) {
    tc_learn = truthy(value);         // accumulate (die temp, ppm) samples while GPS-locked
    if (!from_serial) cfg_owns(KID_TEMPCOMP);   // menu TEMPCOMP toggle bundles learn+apply
  } else if (strcasecmp(key, "tc_apply") == 0) {
    tc_apply = truthy(value);         // steer the SysTick timebase during GPS-loss holdover
    if (!from_serial) cfg_owns(KID_TEMPCOMP);
  } else if (strcasecmp(key, "significance_fade") == 0) {
    significance_fade = truthy(value);      // fade sub-second digits by significance in holdover, not dash
    if (!from_serial) cfg_owns(KID_SIG_FADE);
  } else if (strcasecmp(key, "seg_balance") == 0) {
    // Equalise per-segment brightness by duty (see segbal_poll). "on" (or 1) = AUTO, the
    // calibrated strength-vs-rail curve — the intended setting. A numeric 2..300 applies a
    // fixed manual strength (<=100 linear blend, >100 power-law) for tuning experiments.
    int v = truthy(value) ? 1 : atoi(value);
    seg_balance = (uint16_t)(v < 0 ? 0 : (v > 300 ? 300 : v));
    if (!from_serial) cfg_owns(KID_BALANCE);   // menu BALANCE toggle bundles seg+colon
  } else if (strcasecmp(key, "colon_balance") == 0) {
    // Dim the colons with the rail (see colon_balance_poll). "on"/1 = AUTO curve; a numeric 2..256
    // pins a fixed scale (of 256) for eyeball calibration. The main-loop poll re-applies it.
    int v = truthy(value) ? 1 : (falsey(value) ? 0 : atoi(value));
    colon_balance = (uint16_t)(v < 0 ? 0 : (v > 256 ? 256 : v));
    colonForce = 1;                   // land an explicit sweep step even if within the hysteresis band
    if (!from_serial) cfg_owns(KID_BALANCE);
  } else if (strcasecmp(key, "colon_full_at") == 0) {
    // Per-hardware anchor: the dac (0..4095) at/below which AUTO colons run at full scale. Overrides
    // the baked default. Kept below full-scale so 4095-colon_full_at stays a positive span.
    int v = atoi(value); colon_full_at = v < 0 ? 0 : (v > 4000 ? 4000 : v);
    colonForce = 1;
  } else if (strcasecmp(key, "colon_floor") == 0) {
    // Per-hardware anchor: the AUTO colon scale (of 256) at the dimmest rail. Overrides the baked default.
    int v = atoi(value); colon_floor = v < 0 ? 0 : (v > 256 ? 256 : v);
    colonForce = 1;
  } else if (strcasecmp(key, "freq_hold") == 0) {
    freq_hold = truthy(value);        // in holdover, run at the frequency measured while locked
    if (!from_serial) cfg_owns(KID_FREQ_HOLD);
  } else if (strcasecmp(key, "tc_rtc") == 0) {
    tc_rtc = truthy(value);           // additionally trim RTC->CALR while GPS is absent
  } else if (strcasecmp(key, "tc_t0") == 0) {
    int v = atoi(value); tc_t0 = v < -30 ? -30 : (v > 80 ? 80 : v);
    if (!from_serial) cfg_tc_defined |= CFG_TC_T0;   // config.txt pins the centre -> a stale flash seed must not override
  } else if (strcasecmp(key, "tc_engage_s") == 0) {
    // Floor of 2: currentTime pre-increments at the modelled .900 mark, so "fresh" reads 1
    // for the last 100 ms of every LOCKED second — a floor of 1 would engage during lock.
    int v = atoi(value); tc_engage_s = v < 2 ? 2 : (v > 3600 ? 3600 : v);
  } else if (strcasecmp(key, "tc_max_ppm") == 0) {
    int v = atoi(value); tc_max_ppm = v < 1 ? 1 : (v > 200 ? 200 : v);
  } else if (strcasecmp(key, "tc_hse_b") == 0) { tc_parse_coeff(value, &tc_cfg_hse[1]);
  } else if (strcasecmp(key, "tc_hse_c") == 0) { tc_parse_coeff(value, &tc_cfg_hse[2]);
  } else if (strcasecmp(key, "tc_lse_a") == 0) { tc_parse_coeff(value, &tc_cfg_lse[0]);
  } else if (strcasecmp(key, "tc_lse_b") == 0) { tc_parse_coeff(value, &tc_cfg_lse[1]);
  } else if (strcasecmp(key, "tc_lse_c") == 0) { tc_parse_coeff(value, &tc_cfg_lse[2]);
  } else if (strcasecmp(key, "tc_seed") == 0) {
    tc_seed = truthy(value);          // load the coefficients above as an EVOLVING prior, not a freeze
    if (from_serial && tc_seed) tc_seed_pending = 1;   // serial trigger: apply from the main loop
    if (!from_serial) cfg_tc_defined |= CFG_TC_SEED;   // config.txt supplies its own seed -> flash seed stands down
  } else if (strcasecmp(key, "tc_seed_lo") == 0) {
    tc_seed_lo = (int16_t)atoi(value);   // seed coverage low edge (die °C) — the prior is not extrapolated
    if (!from_serial) cfg_tc_defined |= CFG_TC_LO;
  } else if (strcasecmp(key, "tc_seed_hi") == 0) {
    tc_seed_hi = (int16_t)atoi(value);
    if (!from_serial) cfg_tc_defined |= CFG_TC_HI;
  } else if (strcasecmp(key, "tc_persist") == 0) {
    tc_persist = truthy(value);       // opt-in: auto-save the learned model to its retained flash store
    if (!from_serial) cfg_owns(KID_TEMPCOMP);   // menu TEMPCOMP bundles this too — a file that defines it wins
  } else if (strcasecmp(key, "tc_forget") == 0) {
    if (from_serial && truthy(value)) tc_forget_pending = 1;   // serial-only: erase the retained model (keeps live learning)
  } else if (strcasecmp(key, "tc_dump") == 0) {
    // Serial-only trigger: print the learned model as paste-ready config lines. A stray
    // tc_dump left in config.txt must not fire on every (re)load, hence the origin guard.
    if (from_serial && truthy(value)) tc_dump_pending = 1;
  } else if (strcasecmp(key, "adev_dump") == 0) {
    if (from_serial && truthy(value)) adev_dump_pending = 1;  // serial-only: emit one $PMADEV sentence
  } else if (strcasecmp(key, "hdev_dump") == 0) {
    if (from_serial && truthy(value)) hdev_dump_pending = 1;  // serial-only: emit one $PMHDEV sentence (drift-immune Hadamard)
  } else if (strcasecmp(key, "star_dump") == 0) {
    if (from_serial && truthy(value)) star_dump_pending = 1;  // serial-only: emit one $PMSTAR sentence
  } else if (strcasecmp(key, "star_max_mag") == 0) {
    float smm = (float)atof(value);      // trim the transit catalogue to stars brighter than this (applied at boot in loadStars)
    if (isfinite(smm)) star_max_mag = smm;   // atof("nan") is a real NaN -> the float->int16 magcut cast would be UB
  } else if (strcasecmp(key, "loop_diag") == 0) {
    loop_diag = truthy(value) ? 1 : 0;   // 1 Hz $PMLOOP main-loop latency diagnostic
  } else if (strcasecmp(key, "brightness_report") == 0) {
    brightness_report = truthy(value) ? 1 : 0;   // 1 Hz $PMBRIT auto-dimmer operating point
    if (!from_serial) cfg_owns(KID_BRIT);
  } else if (strcasecmp(key, "watchdog") == 0) {
    // Opt-in. Arms on the next main-loop pass. It cannot be stopped once armed (the hardware has no
    // off switch), so "off" takes effect at the next reset.
    watchdog = truthy(value) ? 1 : 0;
    if (!from_serial) cfg_owns(KID_WATCHDOG);
  } else if (strcasecmp(key, "wdt_dump") == 0) {
    if (from_serial && truthy(value)) wdt_dump_pending = 1;   // serial-only: report the watchdog's state
  } else if (strcasecmp(key, "menu_dump") == 0) {
    if (from_serial && truthy(value)) menu_dump_pending = 1;  // serial-only: report whether the store is flash-backed or RAM-only
  } else if (strcasecmp(key, "factory_reset") == 0) {
    if (from_serial && truthy(value)) factory_reset_pending = 1; // serial-only: wipe stored menu overrides
  } else if (strcasecmp(key, "tc_reset") == 0) {
    if (from_serial && truthy(value)) tc_reset_pending = 1;   // serial-only, same guard

  } else if (key[0]=='B' && key[1]=='S' && key[3]==0) { //BS1, BS2, etc
    if (!key[2] || key[2]<'1' || key[2]>'0'+sizeof(brightnessCurve)/sizeof(brightnessCurve[0])) return;

    char *c = &value[0];
    while (*c++) if(*c==',') break;
    if (*c==0) return;
    *c=0; c++;

    float in  = parseBrightness(value,0);
    float out = parseBrightness(c,1);
    if (in<0 || out<0) return;

    brightnessCurve[key[2]-'1'].in = in;
    brightnessCurve[key[2]-'1'].out = out;

  }

}

void postConfigCleanup(void){
  // Keep the sidereal colon distinct unless the user EXPLICITLY matched the two.
  if (!colonAltExplicit && colonModeAlt == colonModeCivil) {
    colonModeAlt = (colonModeCivil != COLON_MODE_ALT_SAWTOOTH) ? COLON_MODE_ALT_SAWTOOTH
                 : COLON_MODE_TOGGLE;
  }
  colonMode = 0xFF;             // force applyColonForMode to reload exactly once
  applyColonForMode();

  // check at least one mode is enabled
  uint8_t j = 0;
  for (uint8_t i=0; i<NUM_DISPLAY_MODES; i++)
    j+= config.modes_enabled[i];

  if (!j || (j==1 && config.modes_enabled[MODE_STANDBY])) config.modes_enabled[MODE_ISO8601_STD]=1;
  if (!config.modes_enabled[displayMode] || requestMode!=255) nextMode(0);

  // check tolerances
  if (config.tolerance_1ms == 0)   config.tolerance_1ms   = 0xFFFFFFFF;
  if (config.tolerance_10ms == 0)  config.tolerance_10ms  = 0xFFFFFFFF;
  if (config.tolerance_100ms == 0) config.tolerance_100ms = 0xFFFFFFFF;

  if (displayMode == MODE_COUNTDOWN) {
    setNextCountdown(currentTime);
    setPrecision();
    latchSegments();

    if (config.countdown_to < currentTime || decisec!=9 || centisec!=9 || millisec<7)
      sendDate(1);
  } else if (displayMode == MODE_TEXT) {
    if (decisec!=9 || centisec!=9 || millisec<7)
      sendDate(1);
  }

  if (config.fake_long && config.fake_lat) {
    longitude = config.fake_long;
    latitude = config.fake_lat;
    new_position =1;
  }
}

// ==================== config.txt is the source of truth, on every load ============================
// readConfigFile runs at boot and again whenever the host changes config.txt on the CLOCK drive. It
// used to reset seven keys and then parse the file, so any OTHER key kept its last value when its line
// was deleted or commented out: remove "cuckoo = trust", save, eject - and the cuckoo kept performing
// until the power was cycled. With a config.txt that lists only what differs from the defaults,
// taking a line out is the natural way to turn something off.
//
// The rule now: a key the file SET on the previous load and does NOT set on this one goes back to its
// power-on default. Nothing else is touched, so a value set since by the menu or over USB serial for
// a key the file never mentions survives a reload, exactly as before (PCC Web relies on that for
// brightness_report). A key the host sets over serial stops being the file's to take back.
//
// cfg_defaults is the power-on state written as config lines, so a revert goes through the same
// parser as everything else - no second copy of each key's meaning. Rows are the keys the file can
// own. NOT here, on purpose:
//   - the seven keys readConfigFile resets itself on every load (brightness, colon_mode,
//     colon_alt_mode, the three Tolerance_time_* keys, and the zone_override flag);
//   - serial-only commands (tc_dump, wdt_dump, factory_reset, ...), which a file cannot issue;
//   - MATRIX_FREQUENCY: its power-on state is two different rates on two boards (this one's timer
//     default and the date board's own), which one "frequency" cannot express. Removing that line
//     takes effect at the next power-on. Every supplied config.txt sets it.
// zone_override has a row with no default string: dropping it means "find the zone from the position
// again", which is an action, not a value.
static const struct { const char *key, *def; } cfg_defaults[] = {
  {"text",""}, {"zone_override",NULL}, {"zone2",""}, {"countdown_to","1970-01-01T00:00:01Z"},
  {"MODE_ISO8601_STD","off"}, {"MODE_ISO_ORDINAL","off"}, {"MODE_ISO_WEEK","off"}, {"MODE_UNIX","off"},
  {"MODE_JULIAN_DATE","off"}, {"MODE_MODIFIED_JD","off"}, {"MODE_SHOW_OFFSET","off"},
  {"MODE_SHOW_TZ_NAME","off"}, {"MODE_WEEKDAY","off"}, {"MODE_WEEKDA_DD","off"},
  {"MODE_WDY_MM_DD","off"}, {"MODE_STANDBY","off"}, {"MODE_COUNTDOWN","off"}, {"MODE_SATVIEW","off"},
  {"MODE_DEBUG_BRIGHTNESS","off"}, {"MODE_DEBUG_RTC","off"}, {"MODE_TEXT","off"}, {"MODE_VBAT","off"},
  {"MODE_DISPLAYTEST","off"}, {"MODE_TTFF","off"}, {"MODE_FIRMWARE_CRC","off"}, {"MODE_SUN","off"},
  {"MODE_SUN_AZEL","off"}, {"MODE_MOON","off"}, {"MODE_GRID","off"}, {"MODE_LATLON","off"},
  {"MODE_TEMPCOMP","off"}, {"MODE_LST","off"}, {"MODE_SOLAR","off"}, {"MODE_ADEV","off"},
  {"MODE_STAR","off"}, {"MODE_ZONE2","off"}, {"MODE_DARK","off"}, {"page_ms","0"},
  {"fake_longitude","0"}, {"fake_latitude","0"}, {"nmea","on"}, {"pps","off"}, {"cuckoo","off"},
  {"tc_learn","off"}, {"tc_apply","off"}, {"tc_rtc","off"}, {"tc_persist","off"}, {"freq_hold","off"},
  {"significance_fade","off"}, {"seg_balance","off"}, {"colon_balance","off"},
  {"colon_full_at","2048"}, {"colon_floor","20"}, {"tc_t0","40"}, {"tc_engage_s","2"},
  {"tc_max_ppm","100"}, {"tc_hse_b","nan"}, {"tc_hse_c","nan"}, {"tc_lse_a","nan"}, {"tc_lse_b","nan"},
  {"tc_lse_c","nan"}, {"tc_seed","off"}, {"tc_seed_lo","0"}, {"tc_seed_hi","0"}, {"star_max_mag","6"},
  {"loop_diag","off"}, {"brightness_report","off"}, {"watchdog","off"}, {"BS1","0,0"},
  {"BS2","131,365"}, {"BS3","1076,1422"}, {"BS4","2774,2665"}, {"BS5","3849,4095"},
#ifdef NONCOMPLIANT_DATE_MODES
  {"MODE_DDMMYYYY","off"},
#endif
};
#define CFG_ROWS   (sizeof cfg_defaults / sizeof cfg_defaults[0])
#define CFG_WORDS  ((CFG_ROWS + 31u) / 32u)
static uint32_t cfg_from_file[CFG_WORDS];     // bit per row: the file set this key on the load now in force

static int cfg_row(const char *key){
  for (unsigned i = 0; i < CFG_ROWS; i++) if (strcasecmp(key, cfg_defaults[i].key) == 0) return (int)i;
  return -1;
}
// A key nothing owns any more goes back to its default, unless the file now loaded sets it.
// (menu_reconcile, in the store, calls this when config.txt takes a bundled setting back from the menu.)
static void cfg_release(const char *key){
  int row = cfg_row(key);
  if (row < 0 || !cfg_defaults[row].def || (cfg_from_file[row >> 5] & (1u << (row & 31)))) return;
  char v[32];
  strncpy(v, cfg_defaults[row].def, sizeof v - 1); v[sizeof v - 1] = 0;
  parseConfigString((char*)key, v, 1);        // origin "not the file", as in cfg_revert_missing
}
// After a load: put back to default every key the previous file set and this one (`now`) does not,
// then make `now` the record. An absent file is `now` all zero: everything the file owned goes back.
static void cfg_revert_missing(const uint32_t *now){
  for (unsigned i = 0; i < CFG_ROWS; i++) {
    uint32_t bit = 1u << (i & 31u);
    if (!(cfg_from_file[i >> 5] & bit) || (now[i >> 5] & bit)) continue;
    if (!cfg_defaults[i].def) {               // zone_override dropped: look the zone up from the position again
      zone_lat = 999.0; zone_lon = 999.0;     // (defeats the distance gate, which would otherwise wait for a 0.5 km move)
      new_position = 1;
      continue;
    }
    char v[32];
    strncpy(v, cfg_defaults[i].def, sizeof v - 1); v[sizeof v - 1] = 0;   // the parser edits its value in place (BSn)
    parseConfigString((char*)cfg_defaults[i].key, v, 1);   // origin "not the file": must not count as file-defined
  }
  __disable_irq();                            // the USB ISR clears bits (serial takes a key over)
  for (unsigned w = 0; w < CFG_WORDS; w++) cfg_from_file[w] = now[w];
  __enable_irq();
}

void rxConfigString(char c){
  static char key[32], value[32];
  static uint8_t k=0, v=0, state=0;

  if (c=='\n' || c=='\r') {
    key[k]=0;
    value[v]=0;

    if (strcasecmp(key, "reboot") == 0) {
      MX_USB_Stop();
      NVIC_SystemReset();
    }
    if (k && (v || state>=2)) {
      parseConfigString(key, value, 1);   // serial origin: tc_dump/tc_reset may fire
      int row = cfg_row(key);             // set over serial: no longer the file's to revert on a reload
      if (row >= 0) cfg_from_file[row >> 5] &= ~(1u << (row & 31));
      // rxConfigString runs in the USB OTG ISR; postConfigCleanup() calls nextMode()
      // and sendDate(), which are non-reentrant against the SysTick repaint. Defer it
      // to the main loop so it runs in thread context, like the file-config path does.
      delayedPostConfigCleanup=1;
    }
    k=0;
    v=0;
    state=0;
    return;
  }

  switch (state) {
  case 0: // read key
    if (k) {
     if (c=='=') {state =2; break;}
     if (c==' ' || c=='\t') {state =1; break;}
    }
    key[k++] = c;
    if (k==31) k--;
    break;
  case 1: // whitespace
    if (c=='=') state=2;
    else if (c!=' ' && c!='\t') {state=0; k=0; key[k++]=c;}
    break;
  case 2: //second whitespace
    if (c!=' ' && c!='\t' && c!='=') {state=3; value[v++]=c;}
    break;
  case 3:
    value[v++]=c;
    if (v==31) v--;
  }
}

void readConfigFile(void){

#ifdef CHECK_CONFIG_MTIME
  FILINFO fno;
  if (f_stat(CONFIG_FILENAME, &fno) == FR_OK) {
    // if unchanged, exit early before touching any config
    // if the file doesn't exist, fall through and fail on the f_open
    // A zero FAT timestamp (the volume's RTC was unset when config.txt was written)
    // must not be used as a cache key: config={0} matches it on the very first boot,
    // so config is never loaded, no mode is enabled, and the first MODE button press
    // then spins nextMode() forever. Only short-circuit on a real, non-zero stamp.
    if ((fno.fdate || fno.ftime) && fno.fdate==config.fdate && fno.ftime==config.ftime) return;
    config.fdate=fno.fdate;
    config.ftime=fno.ftime;
  }
#endif

  config.tolerance_1ms   = 1000;
  config.tolerance_10ms  = 10000;
  config.tolerance_100ms = 100000;
  config.zone_override = 0;
  config.brightness_override = -1.0;
  colonModeCivil = 0;
  colonModeAlt = COLON_MODE_ALT_SAWTOOTH;
  colonAltExplicit = 0;
  cfg_simple_defined = 0; cfg_modes_defined = 0; cfg_tc_defined = 0;   // rebuilt below as config.txt keys are parsed
  memset(cfg_sig, 0, sizeof cfg_sig);
  uint32_t cfg_now[CFG_WORDS];           // the keys THIS file sets (cfg_defaults rows)
  for (unsigned w = 0; w < CFG_WORDS; w++) cfg_now[w] = 0;

  FIL file;

   if (f_open(&file, CONFIG_FILENAME, FA_READ) != FR_OK) {
     cfg_revert_missing(cfg_now);   // no config.txt: everything a previous file set goes back to default
     menu_reconcile(0);        // no config.txt: stored menu overrides are the only settings
     postConfigCleanup();
     return;
   }

   char key[32], value[32], s[1];
   unsigned int rc;
   uint16_t col=0;


   while (1) {
     f_read(&file, s, 1, &rc);
     if (rc!=1) break; //EOF

     if (s[0]=='\r' || s[0]=='\n') { col=0; continue; } //EOL

     if (col==0 && (s[0]=='#' || s[0]==';')) { // comments
       while (rc && s[0]!='\n') f_read(&file, s, 1, &rc);
       continue;
     }

     if (s[0]!='=') {
       if (col<sizeof(key)-1 &&s[0]!=' ') key[col++] = s[0];
     } else {

       key[col]=0;

       col=0;
       while (s[0]!='\n') {
         f_read(&file, s, 1, &rc);
         if (rc!=1) break;
         if (col<sizeof(value)-1 &&s[0]!=' ' &&s[0]!='\r' &&s[0]!='\n') value[col++] = s[0];
       }
       value[col]=0;
       col=0;

       int row = cfg_row(key);          // a blank "zone_override =" means "automatic": it does not set the key
       if (row >= 0 && (cfg_defaults[row].def || value[0])) cfg_now[row >> 5] |= 1u << (row & 31);
       cfg_line_hash = 2166136261u;        // FNV-1a of "key=value": how a later load knows this line changed
       for (const char *p = key;   *p; p++) cfg_line_hash = (cfg_line_hash ^ (uint8_t)((*p>='A' && *p<='Z') ? *p+32 : *p)) * 16777619u;   // keys match without regard to case
       cfg_line_hash = (cfg_line_hash ^ (uint8_t)'=') * 16777619u;
       for (const char *p = value; *p; p++) cfg_line_hash = (cfg_line_hash ^ (uint8_t)*p) * 16777619u;
       parseConfigString(key, value, 0);   // file origin: serial-only triggers inert

     }
   }

   cfg_revert_missing(cfg_now);   // keys the previous file set and this one dropped: back to their defaults

   menu_apply_overrides();   // merge stored menu overrides (a changed line beats the menu; see menu_reconcile)

   // if enabled, always boot into ttff
   if (config.modes_enabled[MODE_TTFF]) requestMode=MODE_TTFF;
   else requestMode=255;

   postConfigCleanup();
   fw_restore_from_flash();   // if tc_persist is on, the stored wander level becomes the fade bound's starting point
   tc_seed_from_flash();   // if tc_persist is on, load the retained learned model as this boot's seed (config.txt still wins per-key)
   tc_seed_apply();   // warm-start the tempco model from a persisted seed (tc_seed = on), else no-op
   tc_persist_after_seed();   // cap prior order + restore sample counts; no-op unless a flash model seeded this boot
}
