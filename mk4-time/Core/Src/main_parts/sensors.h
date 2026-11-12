// main_parts/sensors.h — USB power, the backup cell voltage, the die temperature.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

// We cannot use hardware vbus monitoring since the pin is occupied by USART1 TX
// We can't use EXTI on PA8 as it's in the same group as PPS
void monitor_vbus(void){
  static _Bool vbus_state = 1; // power-on state is initialised, even if not connected

  _Bool vbus = (GPIOA->IDR & GPIO_PIN_8);

  if (vbus_state && !vbus) { // disconnected

    MX_USB_Stop();

  } else if (vbus && !vbus_state) { // connected

    MX_USB_DEVICE_Init();

  }
  vbus_state = vbus;
}

void measure_vbat(void){
  ADC123_COMMON->CCR |= ADC_CCR_VBATEN;
  HAL_Delay(5);
  HAL_ADC_Start(&hadc3);
  HAL_ADC_PollForConversion(&hadc3, 10);
  uint16_t adc = HAL_ADC_GetValue(&hadc3);
  ADC123_COMMON->CCR &= ~ADC_CCR_VBATEN;
  vbat = (float)adc *0.0024102564102564104;//3*3.29/4095.0;
}

// Read the STM32 internal die-temperature sensor on hadc3 (shared with VBAT) into die_temp_c.
// The die sits slightly above ambient on this low-power board, but it tracks the crystal well
// enough to characterise the oscillator's temperature dependence.
void measure_temp(void){
  ADC_ChannelConfTypeDef s = {0};
  s.Rank = ADC_REGULAR_RANK_1;
  s.SamplingTime = ADC_SAMPLETIME_640CYCLES_5;   // temp sensor needs a long sampling time
  s.SingleDiff = ADC_SINGLE_ENDED;
  s.OffsetNumber = ADC_OFFSET_NONE;
  s.Offset = 0;

  s.Channel = ADC_CHANNEL_TEMPSENSOR;
  HAL_ADC_ConfigChannel(&hadc3, &s);
  ADC123_COMMON->CCR |= ADC_CCR_TSEN;
  HAL_Delay(1);                                  // tSTART for the temperature sensor (~120 us)
  HAL_ADC_Start(&hadc3);
  HAL_ADC_PollForConversion(&hadc3, 10);
  uint16_t raw = HAL_ADC_GetValue(&hadc3);
  ADC123_COMMON->CCR &= ~ADC_CCR_TSEN;

  // Factory-calibrated conversion (TS_CAL1/TS_CAL2 in flash). VREF taken as 3300 mV; absolute
  // accuracy isn't critical — the curve is fitted against GPS-measured ppm, not trusted raw.
  die_temp_c = (int16_t)__HAL_ADC_CALC_TEMPERATURE(3300, raw, ADC_RESOLUTION_12B);
#ifndef __EMSCRIPTEN__                           // the calibration words live in system flash
  die_temp_filter(die_temp_convert(raw, (int32_t)*TEMPSENSOR_CAL1_ADDR, (int32_t)*TEMPSENSOR_CAL2_ADDR, 3300.0f));
#endif

  s.Channel = ADC_CHANNEL_VBAT;                  // restore so measure_vbat() keeps working
  HAL_ADC_ConfigChannel(&hadc3, &s);
}
