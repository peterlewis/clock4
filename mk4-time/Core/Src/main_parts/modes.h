// main_parts/modes.h — which digits may be shown (the precision ladder), changing display mode, the buttons.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

void setPrecision(void){
  if (significance_fade && countMode == COUNT_NORMAL) {
    // Holdover fade replaces the FIXED Tolerance_time_* dash ladder with a SIGNIFICANCE-driven one:
    // computeHoldoverFade() sets each sub-second digit's intensity in digit_bright[] from the live
    // time-interval-error bound, and a digit goes BLACK once its significance reaches zero — it
    // FADES OUT rather than dashing: digits still significant keep ticking (P3/P2/P1) while the
    // seg-balance duty mirror scales each one's lit cycles by its digit_bright, so the display
    // renders the partial fade of the digit on its way out (the emulator reads the same levels).
    // At lock all are FADE_MAX, so this reduces to a plain P3. digit_bright = [ds, cs, ms, dp];
    // buffer_c[3]/[2]/[1] = ms/cs/ds; the decimal point dies with the 0.1 s digit.
    if (digit_bright[2]) {                                 // ms still significant
      buffer_c[0].high = 0b11001110 | cSegDP;
      SetSysTick( &SysTick_CountUp_P3 );
    } else if (digit_bright[1]) {                          // ms faded out, cs significant
      buffer_c[3].low = 0;
      buffer_c[0].high = 0b11001110 | cSegDP;
      SetSysTick( &SysTick_CountUp_P2 );
    } else if (digit_bright[0]) {                          // ds only
      buffer_c[3].low = 0;
      buffer_c[2].low = 0;
      buffer_c[0].high = 0b11001110 | cSegDP;
      SetSysTick( &SysTick_CountUp_P1 );
    } else {                                               // whole seconds
      buffer_c[3].low = 0;
      buffer_c[2].low = 0;
      buffer_c[1].low = 0;
      buffer_c[0].high = 0b11001110;
      SetSysTick( &SysTick_CountUp_P0 );
    }
    return;
  }
  if (countMode == COUNT_NORMAL) {

    // situations not covered:
    // - short poweroff - not had pps, but RTC calibrated only seconds ago
    // - last pps more than 100000 seconds ago (27 hours)
    if (currentTime - last_pps_time < config.tolerance_1ms){
      buffer_c[0].high= 0b11001110 | cSegDP;
      SetSysTick( &SysTick_CountUp_P3 );
    } else if (currentTime - last_pps_time < config.tolerance_10ms){
      buffer_c[3].low = 0b01000000;
      buffer_c[0].high= 0b11001110 | cSegDP;
      SetSysTick( &SysTick_CountUp_P2 );
    } else if (currentTime - rtc_last_calibration < config.tolerance_100ms){
      buffer_c[3].low = 0b01000000;
      buffer_c[2].low = 0b01000000;
      buffer_c[0].high= 0b11001110 | cSegDP;
      SetSysTick( &SysTick_CountUp_P1 );
    } else {
      buffer_c[3].low = 0b01000000;
      buffer_c[2].low = 0b01000000;
      buffer_c[1].low = 0b01000000;
      buffer_c[0].high= 0b11001110;
      SetSysTick( &SysTick_CountUp_P0 );
    }

  } else if (countMode == COUNT_ALT) {

    if (!alt_have_pos) {
      // No usable position (no fix, no fake_longitude): dashes, digits not ticking —
      // never GMST-as-LST, never a guessed longitude. Re-evaluated every second; the
      // row goes live the moment a position appears. resendDate keeps the civil date
      // row refreshing (PendSV's resend check runs right after this) — without it the
      // date would freeze across midnight while dashed.
      resendDate = 1;
      SetPPS( &PPS_NoUpdate );
      SetSysTick( &SysTick_CountUp_NoUpdate );
      buffer_b[0] = bCat0 | 0b01000000 << 2;
      buffer_b[1] = bCat1 | 0b01000000 << 2;
      buffer_b[2] = bCat2 | 0b01000000 << 2;
      buffer_b[3] = bCat3 | 0b01000000 << 2;
      buffer_b[4] = bCat4 | 0b01000000 << 2;
      buffer_c[0].low = 0b01000000;
      buffer_c[3].low = 0b01000000;
      buffer_c[2].low = 0b01000000;
      buffer_c[1].low = 0b01000000;
      buffer_c[0].high= 0b11001110;
    } else if (currentTime - last_pps_time < config.tolerance_1ms){
      SetPPS( &PPS );
      buffer_c[0].high= 0b11001110 | cSegDP;
      SetSysTick( &SysTick_Alt_P3 );
    } else if (currentTime - last_pps_time < config.tolerance_10ms){
      SetPPS( &PPS );
      buffer_c[3].low = 0b01000000;
      buffer_c[0].high= 0b11001110 | cSegDP;
      SetSysTick( &SysTick_Alt_P2 );
    } else if (currentTime - rtc_last_calibration < config.tolerance_100ms){
      SetPPS( &PPS );
      buffer_c[3].low = 0b01000000;
      buffer_c[2].low = 0b01000000;
      buffer_c[0].high= 0b11001110 | cSegDP;
      SetSysTick( &SysTick_Alt_P1 );
    } else {
      SetPPS( &PPS );
      buffer_c[3].low = 0b01000000;
      buffer_c[2].low = 0b01000000;
      buffer_c[1].low = 0b01000000;
      buffer_c[0].high= 0b11001110;
      SetSysTick( &SysTick_Alt_P0 );
    }

  } else if (displayMode == MODE_COUNTDOWN) {

    if (config.countdown_to >= currentTime) {
      SetPPS( &PPS_Countdown );

      if (currentTime - last_pps_time < config.tolerance_1ms){
        buffer_c[0].high= 0b11001110 | cSegDP;
        SetSysTick( &SysTick_CountDown_P3 );
      } else if (currentTime - last_pps_time < config.tolerance_10ms){
        buffer_c[3].low = 0b01000000;
        buffer_c[0].high= 0b11001110 | cSegDP;
        SetSysTick( &SysTick_CountDown_P2 );
      } else if (currentTime - rtc_last_calibration < config.tolerance_100ms){
        buffer_c[3].low = 0b01000000;
        buffer_c[2].low = 0b01000000;
        buffer_c[0].high= 0b11001110 | cSegDP;
        SetSysTick( &SysTick_CountDown_P1 );
      } else {
        buffer_c[3].low = 0b01000000;
        buffer_c[2].low = 0b01000000;
        buffer_c[1].low = 0b01000000;
        buffer_c[0].high= 0b11001110;
        SetSysTick( &SysTick_CountDown_P0 );
      }

    } else {
      countMode = COUNT_HIDDEN;
      SetSysTick( &SysTick_CountUp_NoUpdate );
      SetPPS( &PPS_NoUpdate );
      buffer_c[0].high= 0b11001110 | cSegDP;
      buffer_c[0].low=cSegDecode0;
      buffer_c[1].low=cSegDecode0;
      buffer_c[2].low=cSegDecode0;
      buffer_c[3].low=cSegDecode0;

      next7seg.b[0] = bCat0 | cLut[0]<<2;
      next7seg.b[1] = bCat1 | cLut[0]<<2;
      next7seg.b[2] = bCat2 | cLut[0]<<2;
      next7seg.b[3] = bCat3 | cLut[0]<<2;
      next7seg.b[4] = bCat4 | cLut[0]<<2;
      next7seg.c = cLut[0];
    }

  }
}

#define justExited(x) ((oldMode==x) && (displayMode != x))
void nextMode(_Bool reverse){

  uint8_t oldMode = displayMode;

  if (requestMode!=255){
    if (!config.modes_enabled[requestMode]) {
      requestMode=255;
      return;
    }
    displayMode=requestMode;
    requestMode=255;
  } else if (reverse) {
    do {
      if (--displayMode >= NUM_DISPLAY_MODES) displayMode=NUM_DISPLAY_MODES-1;
    } while (!config.modes_enabled[displayMode]);
  } else {
    do {
      if (++displayMode >=NUM_DISPLAY_MODES) displayMode=0;
    } while (!config.modes_enabled[displayMode]);
  }

  if (justExited(MODE_VBAT)) vbat = 0.0;
  if (justExited(MODE_STANDBY)) displayOn();
  if (justExited(MODE_DISPLAYTEST)) {
    buffer_c[1].high &= ~cSegDP;
    buffer_c[2].high &= ~cSegDP;
    buffer_c[3].high &= ~cSegDP;
  }
  if ( displayMode == MODE_ISO_WEEK || justExited(MODE_COUNTDOWN)
       || justExited(MODE_LST) || justExited(MODE_SOLAR) || justExited(MODE_ZONE2)) {
    // If we exit countdown/alt mode at .9 seconds
    // it will show the wrong time for .1 seconds
    setNextTimestamp(currentTime);
  }

  if (displayMode == MODE_SHOW_OFFSET || displayMode == MODE_DISPLAYTEST) {
    countMode = COUNT_HIDDEN;
    SetSysTick( &SysTick_CountUp_NoUpdate );
    SetPPS( &PPS_NoUpdate );
    colonAnimationStop()
    TIM2->CCR1 = 0; // specific to show_offset
    TIM2->CCR2 = 300;
  } else if (displayMode == MODE_COUNTDOWN) {

    if (config.countdown_to >= currentTime) {
      countMode = COUNT_DOWN;
      setNextCountdown(currentTime);
    } else {
      countMode = COUNT_HIDDEN;
      countdown_days = 0;
    }
    setPrecision();
    TIM2->CCR1 = 0;
    TIM2->CCR2 = 0;
    latchSegments();

  } else if (is_alt_mode(displayMode)) {

    countMode = COUNT_ALT;
    setNextTimestamp(currentTime);   // stock civil bookkeeping (integer path; countdown-arm cost)
    // NO double math here: nextMode can run in the USART2 button ISR (priority 0, which
    // blocks SysTick and the PPS EXTI), and the LST/solar computation is ~100 µs of
    // soft-double. Invalidate and let the main-loop alt_update() seed within one pass
    // (<100 ms); until then setPrecision shows the dashed state.
    alt_gen++;                       // cancels any in-flight staging for the previous timebase
    alt_stage.for_time = 0;
    alt_have_pos = 0;
    alt_seed_pending = 1;
    setPrecision();
    TIM2->CCR1 = 0;
    TIM2->CCR2 = 0;

  }
  else {
    if (countMode != COUNT_NORMAL) {
      countMode = COUNT_NORMAL;
      setPrecision();
      SetPPS( &PPS );
      TIM2->CCR1 = 0;
      TIM2->CCR2 = 0;
      latchSegments();
    }
  }
  applyColonForMode();   // idempotent: sidereal colon on entry, civil colon on exit
  sendDate(1);
}
void button1pressed(void){
  nextMode(0);
}
void button2pressed(void){
  nextMode(1);
}
void buttonsBothHeld(void){
  HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);
  HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_2);

  HAL_DMA_Abort(&hdma_tim1_up);
  HAL_DMA_Abort(&hdma_tim7_up);
  GPIOB->ODR=0;
  GPIOC->ODR=0;

  NVIC_SystemReset();
}
