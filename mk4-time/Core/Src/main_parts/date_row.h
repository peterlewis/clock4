// main_parts/date_row.h — sendDate: every display mode formatted for the date board, in its own C-library context.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

// 12 bytes at 115200 8E1 is 1.14ms, 32 bytes would be 3.06ms
// ---- sendDate's private C-library context ---------------------------------------------------
// newlib keeps the scratch state of float conversion (dtoa's result buffer and its free lists) in
// ONE structure, reached through the global _impure_ptr. sendDate formats floats (the Julian, VBAT,
// TEMPCOMP and ADEV rows) and runs from the SysTick, PendSV, DAC-DMA and GPS handlers, while the
// main loop formats floats of its own: the tc_dump lines, $PMTXTC, $PMADEV, $PMHDEV. When a handler's
// sendDate lands inside one of those prints, its conversion frees and reuses the buffer the main
// loop is still reading, and the main loop's line goes out with WRONG DIGITS - no fault, a valid
// checksum, a plausible number. Reproduced on this exact libc_nano under QEMU with a real SysTick:
// 824 of 1912 interrupted prints were wrong, e.g. 18.68123 printed as 28.68123. A tc_dump line is
// meant to be pasted back into config.txt.
//
// sendDate therefore swaps in a context of its own for its whole extent and restores the caller's on
// the way out (saved in a local, so nested handlers unwind correctly). Nothing else changes: the same
// library calls, the same bytes. With it, 0 of 1912 were wrong. It does not make sendDate itself
// reentrant - two overlapping sendDate calls still share one context and one transmit buffer, which
// is what the decisec != 9 guards on the main-loop callers are for.
#ifdef __NEWLIB__
#include <reent.h>
static struct _reent sd_reent = _REENT_INIT(sd_reent);
#define SD_CONTEXT_ENTER()  struct _reent *sd_saved_ = _impure_ptr; _impure_ptr = &sd_reent
#define SD_CONTEXT_LEAVE()  _impure_ptr = sd_saved_
#else                                   // the emulator and the native test twin do not run newlib
#define SD_CONTEXT_ENTER()  do {} while (0)
#define SD_CONTEXT_LEAVE()  do {} while (0)
#endif

static void sendDate_body( _Bool now ){
  if (waitingForLatch) {
    if (countMode==COUNT_HIDDEN) {
      // if we've entered count_hidden while waiting for latch, it will never happen
      sendLatch()
      waitingForLatch=0;
    } else {
      resendDate=1;
      return;
    }
  }

  uint8_t i = 10;
  HAL_UART_AbortTransmit(&huart2);
  uart2_tx_buffer[0] = CMD_LOAD_TEXT;

  if (menu_text[0]) {                     // the on-device menu owns the date row (TIME row untouched)
    uint8_t n = 0;
    while (n < 10 && menu_text[n]) { uart2_tx_buffer[1+n] = (uint8_t)menu_text[n]; n++; }
    while (n < 10) uart2_tx_buffer[1 + n++] = ' ';   // pad so no stale digits linger
    i = 10;
    goto menu_send_term;
  }

  switch (displayMode) {
  default:
  case MODE_LST:       // alt-timebase modes keep the civil date on the date row —
  case MODE_SOLAR:   // the bottom row stays an unambiguous civil anchor
  case MODE_ZONE2:
  case MODE_ISO8601_STD:
    uart2_tx_buffer[1] ='2';
    uart2_tx_buffer[2] ='0';
    uart2_tx_buffer[3] ='0'+nextBcd.tenYears;
    uart2_tx_buffer[4] ='0'+nextBcd.years;
    uart2_tx_buffer[5] ='-';
    uart2_tx_buffer[6] ='0'+nextBcd.tenMonths;
    uart2_tx_buffer[7] ='0'+nextBcd.months;
    uart2_tx_buffer[8] ='-';
    uart2_tx_buffer[9] ='0'+nextBcd.tenDays;
    uart2_tx_buffer[10]='0'+nextBcd.days;
    break;
#ifdef NONCOMPLIANT_DATE_MODES
  case MODE_DDMMYYYY:
    uart2_tx_buffer[1] ='0'+nextBcd.tenDays;
    uart2_tx_buffer[2] ='0'+nextBcd.days;
    uart2_tx_buffer[3] ='-';
    uart2_tx_buffer[4] ='0'+nextBcd.tenMonths;
    uart2_tx_buffer[5] ='0'+nextBcd.months;
    uart2_tx_buffer[6] ='-';
    uart2_tx_buffer[7] ='2';
    uart2_tx_buffer[8] ='0';
    uart2_tx_buffer[9] ='0'+nextBcd.tenYears;
    uart2_tx_buffer[10]='0'+nextBcd.years;
    break;
#endif
  case MODE_ISO_ORDINAL:
    uart2_tx_buffer[1] ='2' ;//-2+nextBcd.seconds;
    uart2_tx_buffer[2] ='0';
    uart2_tx_buffer[3] ='0'+nextBcd.tenYears;
    uart2_tx_buffer[4] ='0'+nextBcd.years;
    uart2_tx_buffer[5] ='-';
    i = 5 + sprintf((char*)&uart2_tx_buffer[6], "%d", tm_yday+1);
    break;
  case MODE_ISO_WEEK:
    i = sprintf((char*)&uart2_tx_buffer[1], "%d-W%d-%d", iso_year, iso_week, iso_wday+1);
    break;
  case MODE_UNIX:
    i = sprintf((char*)&uart2_tx_buffer[1], "%010ld", (uint32_t)currentTime);
    break;
  case MODE_JULIAN_DATE:
    i = sprintf((char*)&uart2_tx_buffer[1], "%10f", (double)currentTime/86400.0 + 2440587.5 );
    break;
  case MODE_MODIFIED_JD:
    i = sprintf((char*)&uart2_tx_buffer[1], "%10f", (double)currentTime/86400.0 + 40587);
    break;
  case MODE_SHOW_OFFSET:
    // This probably isn't the best place to do it, but the data is static anyway

    if (currentOffset<0){
      buffer_b[0]=bCat0 | 0b0000000000;
      buffer_b[1]=bCat1 | 0b0100000000;
    } else {
      buffer_b[0]=bCat0 | 0b0100011000;
      buffer_b[1]=bCat1 | 0b0111000000;
    }
    int minutes = ((abs(currentOffset)/60) %60);
    int hours = (abs(currentOffset)/3600);

    buffer_b[2]=bCat2 | bLut[ hours/10 ];
    buffer_b[3]=bCat3 | bLut[ hours%10 ];
    buffer_b[4]=bCat4 | bLut[ minutes/10 ];

    buffer_c[0].low= cLut[ minutes%10 ];
    buffer_c[0].high=0b11001110;
    buffer_c[1].low=0;
    buffer_c[2].low=0;
    buffer_c[3].low=0;

    uart2_tx_buffer[1] ='u';
    uart2_tx_buffer[2] ='t';
    uart2_tx_buffer[3] ='c';
    uart2_tx_buffer[4] =' ';
    uart2_tx_buffer[5] ='o';
    uart2_tx_buffer[6] ='f';
    uart2_tx_buffer[7] ='f';
    uart2_tx_buffer[8] ='s';
    uart2_tx_buffer[9] ='e';
    uart2_tx_buffer[10]='t';
    break;
  case MODE_SHOW_TZ_NAME:
    if (loadedRulesString[0]) {
      char * zo = loadedRulesString;
      while (*zo && *zo != '/') zo++;
      if (currentTime%4 <2) {
        zo++;
        i = snprintf((char*)&uart2_tx_buffer[1], 11,"%s", zo);
      } else {
        i = zo-loadedRulesString;
        if (i>10) i=10;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf((char*)&uart2_tx_buffer[1], i+1,"%s", loadedRulesString);
#pragma GCC diagnostic pop
      }
    } else {
      uart2_tx_buffer[1]='-';
      i=1;
    }
    break;
  case MODE_WEEKDAY:
    i = sprintf((char*)&uart2_tx_buffer[1], "%s", wday_str[tm_wday]);
    break;
  case MODE_WEEKDA_DD:
    sprintf((char*)&uart2_tx_buffer[1], "%-7.7s ", wday_str[tm_wday]);
    uart2_tx_buffer[9] ='0'+nextBcd.tenDays;
    uart2_tx_buffer[10]='0'+nextBcd.days;
    break;
  case MODE_WDY_MM_DD:
    sprintf((char*)&uart2_tx_buffer[1], "%.4s ", wday_str[tm_wday]);
    uart2_tx_buffer[6] ='0'+nextBcd.tenMonths;
    uart2_tx_buffer[7] ='0'+nextBcd.months;
    uart2_tx_buffer[8] ='-';
    uart2_tx_buffer[9] ='0'+nextBcd.tenDays;
    uart2_tx_buffer[10]='0'+nextBcd.days;
    break;
  case MODE_SATVIEW:
    if (satview[SV_GPS_L1]==255 && satview[SV_GPS_UNKNOWN]==255) {
      i = sprintf((char*)&uart2_tx_buffer[1], "GPS -");
    } else {
      uint8_t GPS_sv = 0, GLONASS_sv = 0, GALILEO_sv = 0, BEIDOU_sv = 0;
      if (satview[SV_GPS_L1]!=255) GPS_sv += satview[SV_GPS_L1];
      if (satview[SV_GPS_UNKNOWN]!=255) GPS_sv += satview[SV_GPS_UNKNOWN];
      if (satview[SV_GLONASS_L1]!=255) GLONASS_sv += satview[SV_GLONASS_L1];
      if (satview[SV_GLONASS_UNKNOWN]!=255) GLONASS_sv += satview[SV_GLONASS_UNKNOWN];
      if (satview[SV_GALILEO_E1]!=255) GALILEO_sv += satview[SV_GALILEO_E1];
      if (satview[SV_GALILEO_UNKNOWN]!=255) GALILEO_sv += satview[SV_GALILEO_UNKNOWN];
      if (satview[SV_BEIDOU_B1]!=255) BEIDOU_sv += satview[SV_BEIDOU_B1];
      if (satview[SV_BEIDOU_UNKNOWN]!=255)  BEIDOU_sv += satview[SV_BEIDOU_UNKNOWN];

      if (GLONASS_sv>0 && GLONASS_sv>=GALILEO_sv && GLONASS_sv>=BEIDOU_sv) {
        i = sprintf((char*)&uart2_tx_buffer[1], "GPS %d L%d", GPS_sv, GLONASS_sv);
      } else if (GALILEO_sv>0 && GALILEO_sv>=GLONASS_sv && GALILEO_sv>=BEIDOU_sv){
        i = sprintf((char*)&uart2_tx_buffer[1], "GPS %d A%d", GPS_sv, GALILEO_sv);
      } else if (BEIDOU_sv>0 && BEIDOU_sv>=GLONASS_sv && BEIDOU_sv>=GALILEO_sv){
        i = sprintf((char*)&uart2_tx_buffer[1], "GPS %d b%d", GPS_sv, BEIDOU_sv);
      } else {
        i = sprintf((char*)&uart2_tx_buffer[1], "GPS %d -", GPS_sv);
      }
    }
    break;
  case MODE_TEMPCOMP: {
    // Pages: die temp -> HSE model -> LSE model -> samples+state, page_ms dwell each.
    // Values are the governor's display cache (clamped so the row never overflows). Layout is
    // the RISE/SET style: label, separator space, a sign slot (space when positive), then the
    // digits — numbers align whether signed or not, and short values keep clear space at the
    // row's end beside the time row: "tC  32C" / "HSE -0.25" / "rtC  18.68" / "n 159 L".
    int tcp = (int)((uwTick / page_ms()) % 4);
    char num[12];
    if (tcp == 0) {
      int t2 = (int)die_temp_c;
      i = sprintf((char*)&uart2_tx_buffer[1], "tC %c%dC", t2 < 0 ? '-' : ' ', t2 < 0 ? -t2 : t2);
    } else if (tcp == 1 || tcp == 2) {
      _Bool ok = (tcp == 1) ? tc_disp_hse_ok : tc_disp_lse_ok;
      float v = (tcp == 1) ? tc_disp_hse : tc_disp_lse;
      if (!ok) i = sprintf((char*)&uart2_tx_buffer[1], "%s ----", (tcp == 1) ? "HSE" : "rtC");
      else {
        sprintf(num, "%.2f", (double)(v < 0 ? -v : v));
        i = sprintf((char*)&uart2_tx_buffer[1], "%s %c%s", (tcp == 1) ? "HSE" : "rtC", v < 0 ? '-' : ' ', num);
      }
    } else {
      unsigned long ns = tc_n_hse > 999999UL ? 999999UL : tc_n_hse;
      i = sprintf((char*)&uart2_tx_buffer[1], "n%6lu %c", ns, tc_disp_state);
    }
    break;
  }
  case MODE_ADEV: {
    // sigma_y(tau) paged one octave per page_ms dwell; tau = 2^page seconds. Until the ring has
    // enough contiguous 1 s samples for even tau=1 (needs 3), show a filling marker. The layout is
    // a compact scientific that always fits the 10-char row: "<tau>s <sigma>", mantissa dropped as
    // the tau label grows ("1s 3.2e-11" / "64s 3e-11" / "1024s3e-11"). Fractional-frequency values.
    uint8_t noct = adev_disp_noct();
    if (noct == 0) {
      i = sprintf((char*)&uart2_tx_buffer[1], "Adev ----");   // filling, or PPS not locked yet
    } else if ((uint32_t)(uwTick - adev_last_ms) > 2500u) {
      i = sprintf((char*)&uart2_tx_buffer[1], "Adev HOLd");   // holdover: no fresh samples — the curve would be a frozen replay
    } else {
      uint32_t k   = (uwTick / page_ms()) % noct;
      uint32_t tau = 1u << k;
      double   s   = (double)adev_disp_sigma((uint8_t)k);
      char sig[12];
      sprintf(sig, (tau < 10) ? "%.1e" : "%.0e", s);          // room for a mantissa digit only when tau is short
      i = sprintf((char*)&uart2_tx_buffer[1], "%lu %s",       // NO unit-s ('s' == the '5' glyph: "1024s3e-11" read as 10245...) and ALWAYS a separator
                  (unsigned long)tau, sig);
    }
    break;
  }
  case MODE_STAR: {
    // Soonest bright-star meridian transits, paged one per page_ms dwell: "<name> <h:mm>" counting
    // down to culmination. The countdown ticks every second (recomputed here from the cached transit
    // epoch); star_update() re-sorts the list in the main loop. "STAr ----" with no GPS fix.
    if (star_ncache == 0) { i = sprintf((char*)&uart2_tx_buffer[1], "STAr ----"); break; }
    // The payoff moment: when the soonest star reaches culmination, latch its name and hold a "NOW"
    // tell for 8 s — otherwise the next star_update() re-sort rolls it off the list the second it
    // happens and the event is invisible.
    static char     star_now_nm[4];
    static uint32_t star_now_until;
    if ((long)star_cache[0].epoch - (long)currentTime <= 1L){
      memcpy(star_now_nm, star_cache[0].nm, 4);
      star_now_until = (uint32_t)currentTime + 8u;
    }
    if (star_now_until && (uint32_t)currentTime < star_now_until){
      i = sprintf((char*)&uart2_tx_buffer[1], "%-4.4s  NOW", star_now_nm);
      break;
    }
    uint32_t p = (uwTick / page_ms()) % star_ncache;
    long rem = (long)star_cache[p].epoch - (long)currentTime;   // seconds to transit
    if (rem < 0) rem = 0;
    // §6: countdown as bare space-separated values (no dash). Under an hour it reads minutes seconds
    // ("<name>  1 35"); an hour or more switches to hours-'h'-minutes ("<name>  2h15") so a far
    // transit can't masquerade as 2 min 15 s and the field still fits the row.
    if (rem < 3600)
      i = sprintf((char*)&uart2_tx_buffer[1], "%-4.4s %2ld %02ld", star_cache[p].nm, rem / 60, rem % 60);
    else
      i = sprintf((char*)&uart2_tx_buffer[1], "%-4.4s %2ldh%02ld", star_cache[p].nm, rem / 3600, (rem / 60) % 60);
    break;
  }
  case MODE_STANDBY:
     return;
  case MODE_COUNTDOWN:
    i = sprintf((char*)&uart2_tx_buffer[1], "t-%7ldd", countdown_days);
    break;
  case MODE_DEBUG_BRIGHTNESS:
    i = sprintf((char*)&uart2_tx_buffer[1], "%04d %04d", (int)ADC1->DR, 4095-(int)dac_target);
    break;
  case MODE_DEBUG_RTC:
    i = sprintf((char*)&uart2_tx_buffer[1], "rtc %d", debug_rtc_val);
    break;
  case MODE_TEXT:
    if (textDisplay[0]) {
      i = snprintf((char*)&uart2_tx_buffer[1], 30,"%s", textDisplay);
      // snprintf returns the length it WOULD have written (newlib-nano follows C99),
      // not the truncated count; a >29-char TEXT= would otherwise push the ++i below
      // past uart2_tx_buffer[31]. Clamp to the bytes actually written.
      if (i > 29) i = 29;
    } else {
      uart2_tx_buffer[1]='-';
      i=1;
    }
    break;
  case MODE_VBAT:
    if (vbat == 0.0) {
      i = sprintf((char*)&uart2_tx_buffer[1], "bat -");
    } else {
      i = sprintf((char*)&uart2_tx_buffer[1], "bat %.4f", vbat);
    }
    break;
  case MODE_TTFF:
    // Our assumption is that uwTick is zero at power on
    if (!had_pps) time_till_first_fix = (int)(uwTick/1000);
    i = sprintf((char*)&uart2_tx_buffer[1], "ttff %3d.%02d", (int)(time_till_first_fix/60), (int)(time_till_first_fix%60));
    break;
  case MODE_DISPLAYTEST:
    int nn = currentTime%10;

    TIM2->CCR1 = 0;
    TIM2->CCR2 = 0;
    buffer_c[0].high &= ~cSegDP;
    buffer_c[1].high &= ~cSegDP;
    buffer_c[2].high &= ~cSegDP;
    buffer_c[3].high &= ~cSegDP;

    if ((currentTime%20)<10) {
      uart2_tx_buffer[1] =
      uart2_tx_buffer[2] =
      uart2_tx_buffer[3] =
      uart2_tx_buffer[4] =
      uart2_tx_buffer[5] =
      uart2_tx_buffer[6] =
      uart2_tx_buffer[7] =
      uart2_tx_buffer[8] =
      uart2_tx_buffer[9] =
      uart2_tx_buffer[10]= '0'+ nn;

      buffer_b[0]=bCat0 | bLut[ nn ];
      buffer_b[1]=bCat1 | bLut[ nn ];
      buffer_b[2]=bCat2 | bLut[ nn ];
      buffer_b[3]=bCat3 | bLut[ nn ];
      buffer_b[4]=bCat4 | bLut[ nn ];

      buffer_c[0].low= cLut[ nn ];
      buffer_c[1].low=cLut[ nn ];
      buffer_c[2].low=cLut[ nn ];
      buffer_c[3].low=cLut[ nn ];

      if ((currentTime%2) ==0) {
        TIM2->CCR2 = 300;
      } else {
        TIM2->CCR1 = 300;
      }
    } else {

      buffer_b[0]=bCat0 | (nn==0?bLut[8]:0);
      buffer_b[1]=bCat1 | (nn==1?bLut[8]:0);
      buffer_b[2]=bCat2 | (nn==2?bLut[8]:0);
      buffer_b[3]=bCat3 | (nn==3?bLut[8]:0);
      buffer_b[4]=bCat4 | (nn==4?bLut[8]:0);
      buffer_c[0].low=(nn==5?cLut[8]:0);
      buffer_c[1].low=(nn==6?cLut[8]:0);
      buffer_c[2].low=(nn==7?cLut[8]:0);
      buffer_c[3].low=(nn==8?cLut[8]:0);

      if (nn>=5) buffer_c[nn-5].high |= cSegDP;

      i = sprintf((char*)&uart2_tx_buffer[1], "%*s8.", nn, "");
    }

    break;
  case MODE_FIRMWARE_CRC_T:
  {
    extern uint32_t _app_crc[];
    uint32_t fwt = byteswap32(_app_crc[0]);
    i = sprintf((char*)&uart2_tx_buffer[1], "t %08lx", fwt);
  }
    break;
  case MODE_FIRMWARE_CRC_D:
    uart2_tx_buffer[0]=CMD_SHOW_CRC;
    break;

  // --- Astro pack: format the main-loop-computed cache onto the date row only,
  //     leaving the time row as the running clock (SATVIEW-style). --------------
  case MODE_SUN: {
    if (!astro.have_pos || !astro.epoch) { i = sprintf((char*)&uart2_tx_buffer[1], "RISE  ----"); break; }
    int page = (uwTick / page_ms()) % 3;              // rise -> set -> solar noon, page_ms each
    // labels padded to 4 chars in the literal ("SET "/"SOL ") so the time digits
    // line up under RISE without relying on the nano printf honouring "%-4s"
    const char *lbl = page == 0 ? "RISE" : page == 1 ? "SET " : "SOL ";
    int m           = page == 0 ? astro.rise_min : page == 1 ? astro.set_min : astro.noon_min;
    if (!astro.sun_up_today && page != 2) {            // sun never rises/sets today
      i = sprintf((char*)&uart2_tx_buffer[1], "%s ----", lbl);
    } else {
      i = sprintf((char*)&uart2_tx_buffer[1], "%s %02d.%02d", lbl, m / 60, m % 60);
    }
    break;
  }
  case MODE_SUN_AZEL:
    if (!astro.have_pos || !astro.epoch) { i = sprintf((char*)&uart2_tx_buffer[1], "AZ -- EL--"); }
    else if (astro.el < 0) i = sprintf((char*)&uart2_tx_buffer[1], "AZ%03dEL-%02d", astro.az, -astro.el);
    else                   i = sprintf((char*)&uart2_tx_buffer[1], "AZ%03dEL%02d",  astro.az,  astro.el);
    break;
  case MODE_MOON:                                      // UTC only; no fix needed
    if (!astro.epoch) i = sprintf((char*)&uart2_tx_buffer[1], "MOON -");
    else              i = sprintf((char*)&uart2_tx_buffer[1], "MOON %d %3d", astro.moon_idx, astro.moon_pct);
    break;
  case MODE_GRID:
    i = sprintf((char*)&uart2_tx_buffer[1], "%s", astro.epoch ? astro.grid : "----");
    break;
  case MODE_LATLON:
    // RISE/SET-style layout: label, separator space, a sign slot (space when positive), then
    // the digits — numbers align whether signed or not, and short values keep clear space at
    // the row's end: "LAT  51.48" / "LAT -51.48". A 3-digit longitude can't fit both the
    // separator and the sign slot in 10 chars, so the separator is dropped just for that case
    // ("LON 179.99" / "LON-179.99").
    if (!astro.have_pos || !astro.epoch) { i = sprintf((char*)&uart2_tx_buffer[1], "LAT  ----"); }
    else {
      _Bool lat = (uwTick / page_ms()) % 2 == 0;      // page latitude / longitude, page_ms each
      double v = lat ? astro.lat_show : astro.lon_show;
      long h = (long)(v * 100.0 + (v < 0 ? -0.5 : 0.5));  // hundredths, rounded
      long a2 = h < 0 ? -h : h;
      i = sprintf((char*)&uart2_tx_buffer[1], (a2 >= 10000) ? "%s%c%ld.%02ld" : "%s %c%ld.%02ld",
                  lat ? "LAT" : "LON", h < 0 ? '-' : ' ', a2 / 100, a2 % 100);
    }
    break;
  case MODE_DARK: {
    // The observing-session twilight ladder, paged: headline countdown to astronomical darkness, then
    // civil / nautical / astronomical dusk times and the astronomical dawn (dark ends). Honest at the
    // edges: no fix -> dashes; a white night that never reaches -18 -> "NO DARK"; polar night -> "DARK NOW".
    if (!astro.have_pos || !astro.epoch) { i = sprintf((char*)&uart2_tx_buffer[1], "%-4.4s ----", "DARK"); break; }
    int now_min = (int)((((long)currentTime + currentOffset) % 86400L + 86400L) % 86400L) / 60;
    // Every page uses MODE_SUN's RISE/SET layout: a 4-wide label + " HH.MM", so the digits sit in the
    // SAME columns as the ladder pages -> the numbers stay put as the mode pages. Countdown hours are
    // 2-digit too (05.57, not 5.57) so they line up with the times; no hyphen.
    switch ((int)((uwTick / page_ms()) % 5)) {
    case 0:                                              // headline: live countdown / status
      if (astro.always_dark)        i = sprintf((char*)&uart2_tx_buffer[1], "DARK  NOW");
      else if (!astro.dark_tonight) i = sprintf((char*)&uart2_tx_buffer[1], "NO DARK");
      else {
        int dusk = astro.ast_dusk_min, dawn = astro.ast_dawn_min;
        _Bool in_dark = (now_min >= dusk) || (now_min < dawn);   // dark window wraps midnight
        int togo = in_dark ? ((dawn - now_min + 1440) % 1440) : (dusk - now_min);   // minutes to the next edge
        // DAWN = counting to the end of dark (observing time left); DARK = counting down to its start.
        i = sprintf((char*)&uart2_tx_buffer[1], "%-4.4s %02d.%02d", in_dark ? "DAWN" : "DARK", togo / 60, togo % 60);
      }
      break;
    case 1:                                              // civil dusk (-6)
      i = astro.civ_dusk_min < 0 ? sprintf((char*)&uart2_tx_buffer[1], "%-4.4s ----", "CIV")
        : sprintf((char*)&uart2_tx_buffer[1], "%-4.4s %02d.%02d", "CIV", astro.civ_dusk_min/60, astro.civ_dusk_min%60);
      break;
    case 2:                                              // nautical dusk (-12)
      i = astro.nau_dusk_min < 0 ? sprintf((char*)&uart2_tx_buffer[1], "%-4.4s ----", "NAU")
        : sprintf((char*)&uart2_tx_buffer[1], "%-4.4s %02d.%02d", "NAU", astro.nau_dusk_min/60, astro.nau_dusk_min%60);
      break;
    case 3:                                              // astronomical dusk (-18): dark begins
      i = astro.ast_dusk_min < 0 ? sprintf((char*)&uart2_tx_buffer[1], "%-4.4s ----", "AST")
        : sprintf((char*)&uart2_tx_buffer[1], "%-4.4s %02d.%02d", "AST", astro.ast_dusk_min/60, astro.ast_dusk_min%60);
      break;
    default:                                             // astronomical dawn: dark ends
      i = astro.ast_dawn_min < 0 ? sprintf((char*)&uart2_tx_buffer[1], "%-4.4s ----", "End")
        : sprintf((char*)&uart2_tx_buffer[1], "%-4.4s %02d.%02d", "End", astro.ast_dawn_min/60, astro.ast_dawn_min%60);
      break;
    }
    break;
  }
  }
menu_send_term:
  if (now) {
    uart2_tx_buffer[++i]= CMD_RELOAD_TEXT;
  } else {
    uart2_tx_buffer[++i]= '\n';
    waitingForLatch=1;
  }
  HAL_UART_Transmit_DMA(&huart2, uart2_tx_buffer, i+1);

}

// The public entry point: every caller, in every context, formats inside sendDate's own context.
void sendDate( _Bool now ){
  SD_CONTEXT_ENTER();
  sendDate_body(now);
  SD_CONTEXT_LEAVE();
}
