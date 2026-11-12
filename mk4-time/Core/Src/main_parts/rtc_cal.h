// main_parts/rtc_cal.h — calibrating the 32 kHz RTC crystal against GPS.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

void calibrateRTC(void){
  // Called by PPS EXTI

  static uint32_t calibStart =0;
  // LPTIM period is 2 seconds
  // If calibrated well the overflow interrupt should collide with this one
  // Pick an odd number of seconds to calibrate against
#define CAL_PERIOD 63

  // No clear documentation on this but experimentally it appears to be 3 LSE cycles
#define LPTIM_START_DELAY 3

  if (currentTime - calibStart > CAL_PERIOD)  {

    LPTIM1_high=0;
    LL_LPTIM_StartCounter(LPTIM1, LL_LPTIM_OPERATING_MODE_CONTINUOUS);
    calibStart = currentTime;

  } else if ((uint32_t)currentTime - calibStart == CAL_PERIOD) {
    volatile uint16_t x = LPTIM1->CNT;
    volatile uint16_t y = LPTIM1->CNT;
    if (x!=y) goto skipRtcCal;

    int32_t error = ((LPTIM1_high<<16) + x) - 32768*CAL_PERIOD + LPTIM_START_DELAY;
    float e = (float)error * 32.0 / CAL_PERIOD;

    debug_rtc_val = error;//0x100 + round(e);

    if (e>255.0 || e< -255.0) goto skipRtcCal;

    __HAL_RTC_WRITEPROTECTION_DISABLE(&hrtc);
    RTC->CALR = 0x100 + (int)round(e);
    __HAL_RTC_WRITEPROTECTION_ENABLE(&hrtc);
    rtc_last_calibration = (uint32_t)currentTime;

skipRtcCal:
    // Prepare the counter for the next calibration
    // LPTIM1->CNT is read only, the only way to zero it is to disable and re-enable the timer.
    // There is a further delay associated with this, better to put it here than right at the moment we want to start the timer.
    LPTIM1->CR &= ~LPTIM_CR_ENABLE;
    LPTIM1->CR |= LPTIM_CR_ENABLE;
    LL_LPTIM_SetAutoReload(LPTIM1, 0xFFFF);
    LL_LPTIM_ClearFLAG_ARRM(LPTIM1); // just in case there's one pending
  }
}

void EXTI9_5_IRQHandler(void){__HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_7);}
