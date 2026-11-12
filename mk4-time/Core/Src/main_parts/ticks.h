// main_parts/ticks.h — the 1 ms SysTick handlers: the sub-second digits and the .900 preparation of the next second.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

// tc_steer(): holdover rate steering (see tc_governor). Sets the length of the NEXT 1 ms
// period: LOAD writes take effect at the following reload, so distributing tc_rem longer
// periods per 1000 gives an average of base + rem/1000 extra ticks per ms — fractional-ppm
// rate control with three int32 ops. tc_steer_on is 0 unless tc_apply engaged in holdover,
// so the stock cost is one predicted-untaken branch per ms.
#define tc_steer() \
    if (tc_steer_on) { \
      tc_acc += tc_rem; \
      if (tc_acc >= 1000) { tc_acc -= 1000; SysTick->LOAD = (uint32_t)(tc_load_base + 1); } \
      else                { SysTick->LOAD = (uint32_t)tc_load_base; } \
    }

#define timetick() \
    tc_steer(); \
    millisec++; \
    if (millisec>=10) { \
      millisec=0; \
      centisec++; \
      if (centisec>=10) { \
        centisec=0; \
        decisec++; \
        if (decisec>=10) { \
          decisec=0; \
          loadNextTimestamp(); \
        } \
      } \
    }

void SysTick_CountUp_P3(void)
{
  timetick()

  buffer_c[3].low=cLut[millisec];
  buffer_c[2].low=cLut[centisec];
  buffer_c[1].low=cLut[decisec];

  segbal_isr_refresh();   // mirrors track the masters at ISR freshness (seg_balance)

  HAL_IncTick();

  // At the 0.900 mark, we calculate what the display should read at the next pulse
  if (decisec==9 && centisec==0 && millisec==0){
    // Calculating the next display from the unix timestamp takes about 32uS with -O2, -O3 or -Os
    // takes about 70uS on -O0 so I think it's fine to do this within systick
    // If needed, we should move this to a lower priority software-triggered interrupt
    currentTime++;
    setNextTimestamp( currentTime );
    sendDate(0);
  }
}

void SysTick_CountUp_P2(void) {
  timetick()

  buffer_c[2].low=cLut[centisec];
  buffer_c[1].low=cLut[decisec];

  segbal_isr_refresh();

  HAL_IncTick();

  if (decisec==9 && centisec==0 && millisec==0){
    currentTime++;
    setNextTimestamp( currentTime );
    sendDate(0);
  }
}
void SysTick_CountUp_P1(void) {

  timetick()

  buffer_c[1].low=cLut[decisec];

  segbal_isr_refresh();

  HAL_IncTick();

  if (decisec==9 && centisec==0 && millisec==0){
    currentTime++;
    setNextTimestamp( currentTime );
    sendDate(0);
  }
}

void SysTick_CountUp_P0(void) {

  timetick()

  segbal_isr_refresh();

  HAL_IncTick();

  if (decisec==9 && centisec==0 && millisec==0){
    currentTime++;
    setNextTimestamp( currentTime );
    sendDate(0);
  }
}

void SysTick_CountUp_NoUpdate(void) {
  tc_steer();                         // this handler inlines its own cascade: hook it too
  millisec++;
  if (millisec>=10) {
    millisec=0;
    centisec++;
    if (centisec>=10) {
      centisec=0;
      decisec++;
      if (decisec>=10) {
        decisec=0;
        // write_rtc still needs to happen
        triggerPendSV();
      }
    }
  }

  segbal_isr_refresh();   // masters are main-loop-drawn here (TEXT etc.) — keep mirrors no staler

  HAL_IncTick();

  if (decisec==9 && centisec==0 && millisec==0){
    currentTime++;
    setNextTimestamp( currentTime );
    //sendDate(0);
  }
}


void SysTick_CountDown_P3(void)
{
  timetick()

  buffer_c[3].low=cLut[9-millisec];
  buffer_c[2].low=cLut[9-centisec];
  buffer_c[1].low=cLut[9-decisec];

  segbal_isr_refresh();

  HAL_IncTick();

  if (decisec==9 && centisec==0 && millisec==0){
    currentTime++;
    setNextCountdown( currentTime );
    sendDate(0);
  }
}

void SysTick_CountDown_P2(void)
{
  timetick()

  //buffer_c[3].low=cLut[9-millisec];
  buffer_c[2].low=cLut[9-centisec];
  buffer_c[1].low=cLut[9-decisec];

  segbal_isr_refresh();

  HAL_IncTick();

  if (decisec==9 && centisec==0 && millisec==0){
    currentTime++;
    setNextCountdown( currentTime );
    sendDate(0);
  }
}

void SysTick_CountDown_P1(void)
{
  timetick()

  //buffer_c[3].low=cLut[9-millisec];
  //buffer_c[2].low=cLut[9-centisec];
  buffer_c[1].low=cLut[9-decisec];

  segbal_isr_refresh();

  HAL_IncTick();

  if (decisec==9 && centisec==0 && millisec==0){
    currentTime++;
    setNextCountdown( currentTime );
    sendDate(0);
  }
}

// A no precision countdown is going to be really ambiguous, as it will hit zero a second before the target
// Then again it will only be used in situations where the tolerance is worse than a second
void SysTick_CountDown_P0(void)
{
  timetick()

  //buffer_c[3].low=cLut[9-millisec];
  //buffer_c[2].low=cLut[9-centisec];
  //buffer_c[1].low=cLut[9-decisec];

  segbal_isr_refresh();

  HAL_IncTick();

  if (decisec==9 && centisec==0 && millisec==0){
    currentTime++;
    setNextCountdown( currentTime );
    sendDate(0);
  }
}

// Alternate-timebase handlers (MODE_LST / MODE_SOLAR / MODE_ZONE2): identical to the CountUp family —
// same cascade, same sub-second painting, same precision ladder — except the .900 prep
// overlays the staged alternate HH:MM:SS onto next7seg (see alt_prep_next).
void SysTick_Alt_P3(void)
{
  timetick()

  buffer_c[3].low=cLut[millisec];
  buffer_c[2].low=cLut[centisec];
  buffer_c[1].low=cLut[decisec];

  segbal_isr_refresh();

  HAL_IncTick();

  if (decisec==9 && centisec==0 && millisec==0){
    alt_prep_next();
  }
}

void SysTick_Alt_P2(void) {
  timetick()

  buffer_c[2].low=cLut[centisec];
  buffer_c[1].low=cLut[decisec];

  segbal_isr_refresh();

  HAL_IncTick();

  if (decisec==9 && centisec==0 && millisec==0){
    alt_prep_next();
  }
}

void SysTick_Alt_P1(void) {
  timetick()

  buffer_c[1].low=cLut[decisec];

  segbal_isr_refresh();

  HAL_IncTick();

  if (decisec==9 && centisec==0 && millisec==0){
    alt_prep_next();
  }
}

void SysTick_Alt_P0(void) {
  timetick()

  segbal_isr_refresh();

  HAL_IncTick();

  if (decisec==9 && centisec==0 && millisec==0){
    alt_prep_next();
  }
}

void SysTick_Dummy(void){
  HAL_IncTick();
}
