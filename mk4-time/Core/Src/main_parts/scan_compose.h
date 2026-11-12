// main_parts/scan_compose.h — the display on and off, and the balance, the fade and the cuckoo composed into the scan.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

void displayOff(void){

  cuckoo_abort();   // a dark display ends any cuckoo; the mirror drops on the next poll

  uart2_tx_buffer[0]=' '; //in case already waiting for latch
  uart2_tx_buffer[1]= CMD_LOAD_TEXT;
  uart2_tx_buffer[2]= CMD_RELOAD_TEXT;
  HAL_UART_AbortTransmit(&huart2);
  HAL_UART_Transmit_DMA(&huart2, uart2_tx_buffer, 3);

  HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);
  HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_2);

  HAL_DMA_Abort(&hdma_tim1_up);
  HAL_DMA_Abort(&hdma_tim7_up);
  GPIOB->ODR=0;
  GPIOC->ODR=0;
}
void displayOn(void){
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);
  setDisplayPWM(5);
}

// Forward the duty table (ALWAYS on the 0..16 scale — the date board rescales to its own depth)
// to the DATE BOARD over the shared UART, so both rows equalise together from the one config key.
// Sent from the main loop only when the UART is idle and the date board is not inside its latch
// window (its RX is disabled there — bytes would be lost). The date board commits the 9 bytes
// atomically, so an interrupted frame is harmless; a periodic re-send makes delivery eventual.
#define CMD_SET_SEG_BALANCE 0x94
static void segbal_forward(uint32_t eff){
  static uint32_t eff_sent = 0xFFFFFFFFu;
  static uint16_t resend = 0;
  static uint8_t  tx[10];
  if (eff == eff_sent && ++resend < 2048u) return;         // re-assert every ~2 s (lost-frame heal)
  if (waitingForLatch || huart2.gState != HAL_UART_STATE_READY) return;   // retry a later poll
  tx[0] = CMD_SET_SEG_BALANCE;
  for (uint32_t n = 0; n <= 8u; n++) tx[1 + n] = (uint8_t)segbal_duty(n, eff);
  if (HAL_UART_Transmit_DMA(&huart2, tx, 10) == HAL_OK) { eff_sent = eff; resend = 0; }
}

void segbal_poll(void){
  static uint16_t last_ms = 0xFFFF;

  if (displayMode == MODE_STANDBY) { segbal_mirror_live = 0; return; }   // display is off — never (re)start its DMA here

  uint32_t D = segbal_depth();
  uint32_t eff = (seg_balance && D) ? segbal_strength() : 0;
  segbal_forward(eff);                              // keep the date row in step (0 = identity/off)

  // Gradual significance fade — the per-digit dimmer PR #9 anticipated. While any sub-second
  // digit is mid-fade, run the mirror even with seg_balance off (identity duty) so digit_bright
  // (0..FADE_MAX, recomputed each second from the live U(τ)) can scale each digit's cycles: the
  // real display renders the partial fade the emulator always could. Columns c1/c2/c3 = ds/cs/ms;
  // the decimal point (c0.high, cSegDP) follows digit_bright[3]. Big digits never fade.
  uint32_t fading = significance_fade && D && countMode == COUNT_NORMAL &&
                    (digit_bright[0] < FADE_MAX || digit_bright[1] < FADE_MAX ||
                     digit_bright[2] < FADE_MAX || digit_bright[3] < FADE_MAX);

  // CUCKOO (cuckoo.h): a piece in flight forces the D-cycle mirror exactly like a
  // mid-fade digit does, and composes per-digit multipliers into the duty in the loop below.
  uint32_t performing = D && cuckoo_active();

  if (!eff && !fading && !performing) {
    segbal_mirror_live = 0;                         // stop the ISR refresh before dropping the scan
    if (display_scan_len != 5) setDisplayPWM(5);    // live-disable: back to the stock scan
    return;
  }

  // Refill at most once per ms: the masters change at most that fast (the sub-second digits
  // exactly that fast), and the mirror may lag a main-loop pass behind them harmlessly.
  uint16_t ms = (uint16_t)decisec*100 + (uint16_t)centisec*10 + millisec;
  uint16_t want_len = (uint16_t)(5u * D);
  if (ms == last_ms && display_scan_len == want_len) return;
  last_ms = ms;

  uint32_t revsh = (D == 16u) ? 0u : (D == 8u) ? 1u : 2u;  // SEGBAL_REV16 shift for this depth
  for (uint32_t col = 0; col < 5; col++) {
    uint16_t mb = buffer_b[col];
    uint8_t  ml = buffer_c[col].low, mh = buffer_c[col].high;
    uint32_t nb = (uint32_t)__builtin_popcount(mb & SEGBAL_BSEG_MASK);
    // The GPIOC digit is 7 segments in .low plus its decimal point in .high (cSegDP). The REST of
    // .high is that bank's one-cold column select + enables (set once in SysInit) — addressing,
    // not LEDs — and must survive in every mirror slot, exactly like GPIOB's bCat bits.
    uint32_t nc = (uint32_t)__builtin_popcount(ml) + ((mh >> 4) & 1u);
    // duty on the 0..16 scale, rescaled to this depth (D=16 is exact; lit digits keep >= 1 cycle)
    uint32_t sb = (segbal_duty(nb, eff) * D + 8u) / 16u;  if (nb && !sb) sb = 1u;
    uint32_t sc = (segbal_duty(nc, eff) * D + 8u) / 16u;  if (nc && !sc) sc = 1u;
    uint32_t sdp = sc;                                     // the DP rides its digit's cycles...
    if (fading) {
      if (col >= 1 && col <= 3) {                          // ds/cs/ms: scale by live significance
        uint32_t f = digit_bright[col - 1];
        sc = (sc * f + 8u) / 16u;
        if (f && ml && !sc) sc = 1u;                       // mid-fade digits never fully dark...
        if (!f) sc = 0u;                                   // ...but zero significance is zero
        sdp = sc;
      } else if (col == 0) {                               // ...except the seconds column's DP,
        uint32_t f = digit_bright[3];                      // which fades with the 0.1 s digit
        sdp = (sc * f + 8u) / 16u;
        if (f && !sdp) sdp = 1u;
        if (!f) sdp = 0u;
      }
    }
    if (performing) {
      // Per-digit cuckoo multipliers, composed multiplicatively (the same law as the fade):
      // B col k is big digit row k; C col 0 is the seconds units (row 5), cols 1..3 are
      // ds/cs/ms (rows 6..8); the DP rides its digit. Intent levels pass through CK_GAMMA
      // so they read perceptually (CK_GAMMA, in cuckoo.h). The >=1-cycle floor applies only
      // to duties that arrived non-zero — a digit the significance fade already
      // extinguished stays extinguished (the cuckoo must never resurrect what the fade
      // claims is gone).
      uint32_t g = CK_GAMMA[ck_bright[col]], p;
      p = sb;  sb  = (sb  * g + 8u) / 16u;  if (g && p && !sb)  sb  = 1u;  if (!g) sb  = 0u;
      uint32_t rowc = (col == 0) ? 5u : (col <= 3 ? 5u + col : 0xFFu);
      if (rowc != 0xFFu) {
        g = CK_GAMMA[ck_bright[rowc]];
        p = sc;  sc  = (sc  * g + 8u) / 16u;  if (g && p && !sc)  sc  = 1u;  if (!g) sc  = 0u;
        p = sdp; sdp = (sdp * g + 8u) / 16u;  if (g && p && !sdp) sdp = 1u;  if (!g) sdp = 0u;
      }
    }
    uint16_t cat = mb & (uint16_t)~SEGBAL_BSEG_MASK;
    uint8_t  csel = mh & (uint8_t)~cSegDP;                 // GPIOC column select + enables
    uint32_t ring = D - 1u;                                // mirror ranks 1..D-1 rotate mod D-1
    uint32_t pb = SEGBAL_PH_B[col] % ring;
    uint32_t pc = SEGBAL_PH_C[col] % ring;
    uint16_t bm_b = 1u, bm_c = 1u, bm_dp = 1u;             // cycle 0 = the master, always lit
    for (uint32_t k = 1; k < D; k++) {
      uint32_t i = col + 5u*k;
      uint32_t r = (uint32_t)(SEGBAL_REV16[k] >> revsh);   // 1..D-1 for k >= 1
      uint32_t rb = r - 1u + pb; if (rb >= ring) rb -= ring;
      uint32_t rc = r - 1u + pc; if (rc >= ring) rc -= ring;
      uint32_t lb = (rb + 1u < sb), lc = (rc + 1u < sc), ldp = (rc + 1u < sdp);
      bm_b |= (uint16_t)(lb << k); bm_c |= (uint16_t)(lc << k); bm_dp |= (uint16_t)(ldp << k);
      buffer_b[i]      = lb ? mb : cat;                    // column select stays in every slot
      buffer_c[i].low  = lc ? ml : 0;
      buffer_c[i].high = csel | (ldp ? (mh & cSegDP) : 0);
    }
    segbal_lit_b[col] = bm_b; segbal_lit_c[col] = bm_c; segbal_lit_dp[col] = bm_dp;
  }
  if (display_scan_len != want_len) setDisplayPWM(want_len);   // extend to the D-cycle scan
  segbal_mirror_live = 1;                                  // bitmaps valid — arm the ISR refresh
}

// SysTick-side mirror refresh: re-copy the live master values through the exported bitmaps every
// millisecond, so the mirrors can never go staler than the masters (see the bitmap block in scan.h).
// Cost with D=16: 5 x 15 slot writes, ~10 us at 80 MHz — 1% of one SysTick period, only while
// the D-cycle scan is up. Bitmaps and masters are each written whole (halfword stores), so the
// worst race with the main-loop refill is one slot showing one frame of the other's value.
void segbal_isr_refresh(void){
  if (!segbal_mirror_live || display_scan_len <= 5) return;
  uint32_t D = (uint32_t)display_scan_len / 5u;
  for (uint32_t col = 0; col < 5; col++) {
    uint16_t mb = buffer_b[col];
    uint8_t  ml = buffer_c[col].low, mh = buffer_c[col].high;
    uint16_t cat  = mb & (uint16_t)~SEGBAL_BSEG_MASK;
    uint8_t  csel = mh & (uint8_t)~cSegDP;
    uint16_t lb = segbal_lit_b[col], lc = segbal_lit_c[col], ldp = segbal_lit_dp[col];
    for (uint32_t k = 1; k < D; k++) {
      uint32_t i = col + 5u*k;
      buffer_b[i]      = ((lb >> k) & 1u) ? mb : cat;
      buffer_c[i].low  = ((lc >> k) & 1u) ? ml : 0;
      buffer_c[i].high = csel | (((ldp >> k) & 1u) ? (mh & cSegDP) : 0);
    }
  }
}
