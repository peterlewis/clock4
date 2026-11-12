// main_parts/matrix_and_colons.h — the scan rate, and the colon animations and their brightness.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

static uint32_t matrix_freq_hz = 20000;   // §4 shadow of the display refresh rate (menu MATRIX reads/writes this)
void setDisplayFreq(uint32_t freq){
  if (waitingForLatch) {
    delayedDisplayFreq = freq;
    return;
  }

  if (freq<1000 || freq>100000) {delayedDisplayFreq=0; return;}

  uint8_t tx_buf[4];
  tx_buf[0]= CMD_SET_FREQUENCY;
  tx_buf[1]= (freq>>14) & 0x7F;
  tx_buf[2]= (freq>>7)  & 0x7F;
  tx_buf[3]= (freq)     & 0x7F;
  if (HAL_UART_Transmit(&huart2, tx_buf, 4, 2) == HAL_OK) {
    delayedDisplayFreq = 0;
  }

  uint32_t arr = round(16000000.0 / (float)freq) -1.0;

  TIM1->ARR = arr;
  TIM7->ARR = arr;
  matrix_freq_hz = freq;             // §4 shadow of the applied rate (menu reads this, not the lossy ARR)
}

#define colonAnimationStart() \
  TIM5->CNT=0; \
  HAL_DMA_Start(&hdma_tim5_ch1, (uint32_t)buffer_colons_L, (uint32_t)&TIM2->CCR1, 200); \
  HAL_DMA_Start(&hdma_tim5_ch2, (uint32_t)buffer_colons_R, (uint32_t)&TIM2->CCR2, 200);

#define colonAnimationStop() \
  HAL_DMA_Abort(&hdma_tim5_ch1); \
  HAL_DMA_Abort(&hdma_tim5_ch2);

#define colonAnimationSync() \
  colonAnimationStop() \
  colonAnimationStart()

// Colon brightness tracking. The colons are TIM2 PWM (buffer_colons_L/R), NOT part of the segment
// scan and NOT coupled to dac_target — so out of the box they hold their animation brightness while
// the digits dim around them, blazing at low rail. colon_balance ties the colon PWM to the rail:
//   0/off (default) = stock (colons at full animation brightness)
//   1/on            = AUTO: scale the colon PWM by the calibrated rail curve (see colon_scale_for)
//   2..256          = fixed manual scale (of 256) for eyeball calibration across the rail
// The scale is folded into the animation buffer by loadColonAnimation and re-applied by
// colon_balance_poll() from the main loop when the rail moves — never from the DMA/scan ISR.
volatile uint16_t colon_balance = 0;
static   uint16_t colonScale    = 256;   // applied fixed-point scale, 256 = full animation brightness
static volatile uint8_t colonForce = 0;  // config set colon_balance — apply once even if within the AUTO hysteresis

void loadColonAnimation(void){


  switch (colonMode) {
    case COLON_MODE_SLOWFADE:
      for (int k=0;k<100;k++) {
        buffer_colons_R[k] =
        buffer_colons_L[k] = k*2;
        buffer_colons_R[k+100] =
        buffer_colons_L[k+100] = 198-k*2;
      }
      break;
    case COLON_MODE_HEARTBEAT:
      for (int k=0;k<50;k++) {
        buffer_colons_L[k] = k*4;
      }
      for (int k=0;k<100;k++) {
        buffer_colons_L[k+50] = 200 - k*2;
      }
      for (int k=0;k<50;k++) {
        buffer_colons_L[k+150] = 0;
      }
      for (int k=0;k<200;k++) {
        buffer_colons_R[k] = buffer_colons_L[(k+175)%200];
      }

      break;
    case COLON_MODE_1PPS_SAWTOOTH:
      for (int k=0;k<100;k++) {
        buffer_colons_R[k] =
        buffer_colons_L[k] = 196-(k*k)/50;
        buffer_colons_R[k+100] =
        buffer_colons_L[k+100] = 196-(k*k)/50;
      }
      break;
    case COLON_MODE_ALT_SAWTOOTH:
      for (int k=0;k<100;k++) {
        buffer_colons_R[k]     = 0;
        buffer_colons_L[k+100] = 0;
        buffer_colons_L[k]     = 196-(k*k)/50;
        buffer_colons_R[k+100] = 196-(k*k)/50;
      }
      break;
    case COLON_MODE_TOGGLE:
      for (int k=0;k<100;k++) {
        buffer_colons_R[k] = 200;
        buffer_colons_L[k] = 200;
        buffer_colons_R[k+100] = 0;
        buffer_colons_L[k+100] = 0;
      }
      break;
    case COLON_MODE_SOLID:
      for (int k=0;k<200;k++) {
        buffer_colons_R[k] = 200;
        buffer_colons_L[k] = 200;
      }
      break;
  }

  // Track the rail: fold the current colon brightness into the freshly-loaded animation (256 = full,
  // so colon_balance = off is exact identity). The DMA reads this buffer live — refilling it in place
  // re-brightens the colons without a restart or a phase jump.
  for (int k = 0; k < 200; k++) {
    buffer_colons_L[k] = (uint16_t)(((uint32_t)buffer_colons_L[k] * colonScale) >> 8);
    buffer_colons_R[k] = (uint16_t)(((uint32_t)buffer_colons_R[k] * colonScale) >> 8);
  }
}

// Select the colon animation for the current display mode (idempotent, thread context).
// Alternate-timebase modes get their own animation so they read as "not civil" at a glance.
void applyColonForMode(void){
  uint8_t want = (colon_preview != 0xFF)                         // §3.5: an editor is previewing a choice
               ? colon_preview
               : is_alt_mode(displayMode)
               ? colonModeAlt : colonModeCivil;
  if (want != colonMode) {
    colonMode = want;
    loadColonAnimation();
  }
}

// AUTO colon scale vs rail (dac_target 0 = brightest .. 4095 = dimmest). A full eyeballed sweep on a
// production Mk IV (2026-07-11) landed a clean straight line: the colons want full animation scale at
// the bright half of the rail and taper linearly to a dim floor at the darkest, so the separators stay
// present but recessed while the digits blaze at low rail. Two anchors define it (both baked here and
// overridable per-hardware from config.txt, exactly like the BS brightness curve):
//   colon_full_at — the dac at/below which colons run at full scale (256). Default 2048 (mid rail).
//   colon_floor   — the minimum scale, reached at the dimmest rail (dac 4095). Default 20.
// The line runs from (colon_full_at, 256) toward (4095, 0), clamped at the top to 256 and the bottom to
// colon_floor: scale = clamp( 256*(4095-dac) / (4095-colon_full_at), colon_floor, 256 ). With the
// defaults this is ~ (4095-dac)/8 and passes through the measured points (dac 3276->102, 2867->153).
volatile int32_t colon_full_at = 2048;   // baked default; config "colon_full_at = N" overrides
volatile int32_t colon_floor   = 20;     // baked default; config "colon_floor = N" overrides
static uint16_t colon_scale_for(int32_t d){
  int32_t span = 4095 - colon_full_at; if (span < 1) span = 1;   // guard div-by-zero / inverted anchor
  int32_t s = 256 * (4095 - d) / span;
  if (s > 256) s = 256;
  if (s < colon_floor) s = colon_floor;
  if (s < 0) s = 0;
  return (uint16_t)s;
}
// Re-mirror the colon scale into the animation buffer when the live rail (or the config) moves it.
// Main-loop only (loadColonAnimation touches 400 samples); throttled so it reloads on real change.
void colon_balance_poll(void){
  if (displayMode == MODE_STANDBY) return;   // colons off (displayOff stops TIM2) — match segbal_poll's guard
  uint16_t want = (colon_balance == 0) ? 256
                : (colon_balance == 1) ? colon_scale_for((int32_t)dac_target)
                : (colon_balance > 256) ? 256 : colon_balance;
  int diff = (int)want - (int)colonScale; if (diff < 0) diff = -diff;
  if (diff >= 4 || colonForce) {             // hysteresis throttles AUTO drift; colonForce lands an explicit set
    colonForce = 0;
    colonScale = want;
    // loadColonAnimation() also runs in the USART2 ISR (button -> nextMode -> applyColonForMode); mask
    // JUST that IRQ so a button press can't re-enter mid-refill and tear buffer_colons. A few us, only
    // on a real brightness step, and PPS (EXTI9_5) + GPS (USART1) are untouched.
    NVIC_DisableIRQ(USART2_IRQn);
    loadColonAnimation();          // refills buffer_colons at the new scale; DMA picks it up, no restart
    NVIC_EnableIRQ(USART2_IRQn);
  }
}
