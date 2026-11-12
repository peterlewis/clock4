// main_parts/pps.h — the PPS edge: capture, the three edge handlers, and the $PMTXTS and $PMBRIT sentences.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

// --- PPS host timestamping ----------------------------------------------------------------
// Optional: emit one proprietary NMEA sentence ($PMTXTS) per PPS edge over the CDC port so a
// host can measure the clock's timing stability (phase jitter, oscillator drift, holdover) —
// things the plain NMEA stream cannot convey. Enabled by config "pps = on". Capture happens in
// the PPS ISR (cheap, just snapshots); the sentence is formatted + sent from the main loop.
volatile uint8_t pps_ts_enabled = 0;
volatile _Bool   pps_record_pending = 0;
volatile struct {
  uint32_t seq;        // increments every PPS edge (32-bit: no practical wrap; host detects gaps)
  uint32_t systick;    // SysTick->VAL at the edge, captured BEFORE the reload (down-counter)
  uint16_t subms;      // 0..999 modelled ms-of-second at the edge, BEFORE the counters reset
  uint32_t epoch;      // currentTime at the edge (Unix seconds, UTC)
  int32_t  calerr;     // debug_rtc_val: signed LSE cycle error over CAL_PERIOD s (=> ppm on host)
  uint32_t sincecal;   // seconds since last successful RTC calibration (holdover age)
  int16_t  temp;       // die temperature (°C) — for host-side ppm-vs-temperature characterisation
  uint8_t  flags;      // bit0 data_valid, bit1 had_pps, bit2 rtc_good
  uint32_t dwt_pps;    // DWT->CYCCNT (free-running 12.5ns) latched at the edge — SOF-correlation timebase
} pps_cap;

// --- SOF correlation (experimental: sub-ms USB timestamping without a hardware PPS wire) -----------
// Latched by the USB SOF interrupt (PCD_SOFCallback, usbd_conf.c) every 1ms WHEN pps_ts_enabled: the
// 11-bit USB frame number and the DWT count at that Start-Of-Frame. Emitted in $PMTXTS so a host —
// which can read each USB frame's own arrival time in hardware — can place the PPS edge on its clock
// via the frame, immune to the ~6ms host-driven read jitter. Written in the SOF ISR, read at emit
// under __disable_irq. pps_sof_valid gates emission of the tail: it is 0 until the first SOF has
// latched a real anchor (so the first PPS after enumeration, or with pps just toggled on, or the
// emulator which has no SOF, emits the plain 9-field sentence rather than a stale (0,0) anchor).
volatile uint32_t pps_sof_dwt   = 0;   // DWT->CYCCNT at the most recent SOF
volatile uint16_t pps_sof_frame = 0;   // USB 11-bit frame number at that SOF (matches host frame mod 2048)
volatile uint8_t  pps_sof_valid = 0;   // 1 once a real SOF anchor has been latched; 0 = emit 9-field only

// A $PMTXTS timestamp is actually awaiting emission ONLY when PPS-out is enabled and a capture is
// pending. pps_record_pending on its own is set on every PPS edge (it also feeds ADEV/RTC/tempcomp)
// and is cleared only by the emit path, so on a GPS-locked clock with PPS-out off (the default) it
// latches high forever — gating a flash commit on it directly wedges persistence permanently. Both
// commit gates in store.h defer a page erase around a real pending emit via this predicate.
static inline _Bool pps_emit_pending(void){ return pps_ts_enabled && pps_record_pending; }

// Snapshot the timing state at the instant of the PPS edge. MUST run before SysTick->VAL is
// reloaded and before millisec/centisec/decisec are zeroed, so it captures the phase error
// between the firmware's modelled second and the true GPS edge.
#define capturePPS() do { \
    pps_cap.dwt_pps  = DWT->CYCCNT; \
    pps_cap.systick  = SysTick->VAL; \
    pps_cap.subms    = (uint16_t)decisec*100 + (uint16_t)centisec*10 + millisec; \
    pps_cap.epoch    = (uint32_t)currentTime; \
    pps_cap.calerr   = debug_rtc_val; \
    pps_cap.sincecal = (uint32_t)currentTime - (uint32_t)rtc_last_calibration; \
    pps_cap.temp     = die_temp_c; \
    pps_cap.flags    = (data_valid?1:0) | (had_pps?2:0) | (rtc_good?4:0); \
    pps_cap.seq++; \
    pps_record_pending = 1; \
    adev_push_dwt(pps_cap.dwt_pps, pps_cap.epoch); /* free-running Allan-deviation sample */ \
  } while(0)

// PPS rising edge
void PPS(void)
{
  capturePPS();
  SysTick->VAL = SysTick->LOAD;

  buffer_c[3].low=cLut[0];
  buffer_c[2].low=cLut[0];
  buffer_c[1].low=cLut[0];
  loadNextTimestamp();
  millisec=0;
  centisec=0;
  decisec=0;

  __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_7);

  // clear systick flag if set?

  // During first power up PPS can be emitted before the GPS leapsecond offset is known
  // In this case, it is safest to pretend PPS hasn't happened
  if (!data_valid) return;

  calibrateRTC();

  if ((currentTime & 1) ==0) {colonAnimationSync()}

  had_pps = 1;
  last_pps_time = (uint32_t)currentTime;
}

void PPS_NoUpdate(void)
{
  capturePPS();
  SysTick->VAL = SysTick->LOAD;
  triggerPendSV();

  millisec=0;
  centisec=0;
  decisec=0;

  __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_7);

  if (!data_valid) return;

  calibrateRTC();

  had_pps = 1;
  last_pps_time = (uint32_t)currentTime;
}

void PPS_Countdown(void)
{
  capturePPS();
  SysTick->VAL = SysTick->LOAD;

  buffer_c[3].low=cLut[9];
  buffer_c[2].low=cLut[9];
  buffer_c[1].low=cLut[9];
  loadNextTimestamp();
  millisec=0;
  centisec=0;
  decisec=0;

  __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_7);

  if (!data_valid) return;
  calibrateRTC();
  if ((currentTime & 1) ==0) {colonAnimationSync()}

  had_pps = 1;
  last_pps_time = (uint32_t)currentTime;
}

void PPS_Init(void){
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /*Configure GPIO pin : PC7 */
  GPIO_InitStruct.Pin = GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  SetPPS( &PPS );
}

// Format + send one $PMTXTS sentence from the values captured at the last PPS edge.
// Runs in the main loop (snprintf is fine here, never in the ISR). Clears pps_record_pending
// on a successful send and for any undeliverable record (no host, formatting failure) — a
// fresh record arrives on the next edge, so only USBD_BUSY is worth retrying.
// Sentence: $PMTXTS,<seq>,<epoch>,<subms>,<systick>,<load>,<calerr>,<sincecal>,<temp>,<flags>,<dwt_pps>,<sof_frame>,<dwt_sof>*CC
//   subms+(load-systick)/(load+1) = modelled sub-second position at the edge (phase error);
//   ppm = calerr * 1e6 / (32768 * CAL_PERIOD)  [CAL_PERIOD=63];  temp = die °C;
//   flags: b0 valid, b1 pps, b2 rtc.
//   SOF-correlation tail (experimental): dwt_pps = DWT cycle count at the PPS edge; sof_frame = USB
//   11-bit frame number of the most recent SOF; dwt_sof = DWT at that SOF. A host that knows each USB
//   frame's own arrival time places the edge as hostTime(sof_frame) + (dwt_pps-dwt_sof)/f_dwt, immune
//   to USB read jitter. dwt_pps deltas (~80e6/s) self-calibrate f_dwt, so no core-clock assumption.
static uint8_t emitPPSTimestamp(void){
  // With no enumerated host (e.g. charger-only power) CDC can never accept the sentence;
  // drop the record before doing any formatting work, otherwise the pending flag would
  // re-run the whole format-and-fail cycle every main-loop pass until a host appears.
  if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) {
    pps_record_pending = 0;
    return USBD_FAIL;
  }

  __disable_irq();                       // atomic snapshot of the ISR-written capture
  uint32_t snap_seq = pps_cap.seq;
  uint32_t st       = pps_cap.systick;
  uint16_t subms    = pps_cap.subms;
  uint32_t epoch    = pps_cap.epoch;
  int32_t  calerr   = pps_cap.calerr;
  uint32_t sincecal = pps_cap.sincecal;
  int16_t  temp     = pps_cap.temp;
  uint8_t  flags    = pps_cap.flags;
  uint32_t dwt_pps  = pps_cap.dwt_pps;   // DWT at the PPS edge (SOF-correlation timebase)
  uint32_t sof_dwt  = pps_sof_dwt;       // DWT at the most recent SOF ...
  uint16_t sof_fr   = pps_sof_frame;     // ... and that SOF's 11-bit USB frame number ...
  uint8_t  sof_ok   = pps_sof_valid;     // ... valid only once a real SOF has latched an anchor
  __enable_irq();

  uint32_t load = SysTick->LOAD;         // constant; sent so the host needn't assume core clock

  char body[128];                        // everything between '$' and '*'
  int n = snprintf(body, sizeof body, "PMTXTS,%lu,%lu,%u,%lu,%lu,%ld,%lu,%d,%X",
                   (unsigned long)snap_seq, (unsigned long)epoch, (unsigned)subms,
                   (unsigned long)st, (unsigned long)load, (long)calerr,
                   (unsigned long)sincecal, (int)temp, (unsigned)flags);
  if (n < 0 || n >= (int)sizeof body) { pps_record_pending = 0; return USBD_FAIL; }
  // Append the SOF-correlation tail only when a real anchor exists — never a stale (0,0). Absent tail =
  // the plain sentence a 9-field parser expects (also the emulator's output, which has no USB SOF).
  if (sof_ok) {
    int t = snprintf(body + n, sizeof body - n, ",%lu,%u,%lu",
                     (unsigned long)dwt_pps, (unsigned)sof_fr, (unsigned long)sof_dwt);
    if (t < 0 || t >= (int)(sizeof body - n)) { pps_record_pending = 0; return USBD_FAIL; }
    n += t;
  }

  uint8_t cks = 0;                       // standard NMEA XOR checksum
  for (int i = 0; i < n; i++) cks ^= (uint8_t)body[i];

  char line[NMEA_BUF_SIZE];              // must fit the CDC txbuf[NMEA_BUF_SIZE] downstream
  int m = snprintf(line, sizeof line, "$%s*%02X\r\n", body, (unsigned)cks);
  if (m < 0 || m >= (int)sizeof line) { pps_record_pending = 0; return USBD_FAIL; }

  // The CDC IN endpoint is shared with the ISR NMEA passthrough; serialise the (tiny) submit,
  // and clear the pending flag only if no fresh PPS edge arrived since the snapshot (so a
  // record captured mid-send isn't silently dropped). FAIL also clears: the record is
  // undeliverable (USB de-inited under us), unlike BUSY where the host may drain the FIFO.
  __disable_irq();
  uint8_t r = CDC_Copy_Transmit((uint8_t*)line, (uint16_t)m);
  if (r != USBD_BUSY && pps_cap.seq == snap_seq) pps_record_pending = 0;
  __enable_irq();
  return r;
}

// Format + send one $PMBRIT sentence. Main loop only. The DAC DMA callbacks write the rail, so read
// it once; the rest is main-loop state. Same CDC contract as emitPPSTimestamp: serialise the submit
// against the ISR NMEA passthrough, and only BUSY is worth retrying.
static uint8_t emitBrightnessReport(void){
  if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) return USBD_FAIL;   // no host: skip this second

  __disable_irq();
  float dt = dac_target;                 // 0 = brightest .. 4095 = dimmest (inverted, as stored)
  __enable_irq();
  uint32_t adc = ADC1->DR & 0xFFFu;
  char src = (displayMode == MODE_STANDBY) ? 'S'
           : (config.brightness_override >= 0.0f) ? 'M' : 'A';
  int32_t dac = (int32_t)(4095.0f - dt + 0.5f);
  if (dac < 0) dac = 0; else if (dac > 4095) dac = 4095;
  uint32_t segk  = (src != 'S' && seg_balance && segbal_depth()) ? segbal_strength() : 0;
  unsigned colon = (src == 'S') ? 0u : (unsigned)colonScale;

  char body[48];                         // everything between '$' and '*'
  int n = snprintf(body, sizeof body, "PMBRIT,%lu,%ld,%c,%lu,%u",
                   (unsigned long)adc, (long)dac, src, (unsigned long)segk, colon);
  if (n < 0 || n >= (int)sizeof body) return USBD_FAIL;

  uint8_t cks = 0;                       // standard NMEA XOR checksum
  for (int i = 0; i < n; i++) cks ^= (uint8_t)body[i];

  char line[NMEA_BUF_SIZE];              // must fit the CDC txbuf[NMEA_BUF_SIZE] downstream
  int m = snprintf(line, sizeof line, "$%s*%02X\r\n", body, (unsigned)cks);
  if (m < 0 || m >= (int)sizeof line) return USBD_FAIL;

  __disable_irq();
  uint8_t r = CDC_Copy_Transmit((uint8_t*)line, (uint16_t)m);
  __enable_irq();
  return r;
}

// Main loop: one report a second while brightness_report is on. BUSY holds the window open so the
// next pass retries; a report that went, or had no host to take it, closes it until the next second.
void brightness_report_poll(void){
  if (!brightness_report || (uint32_t)(uwTick - brit_win) < 1000u) return;
  if (emitBrightnessReport() != USBD_BUSY) brit_win = uwTick;
}
