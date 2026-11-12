// main_parts/scan.h — the display scan: on, off, and per-segment brightness balance (seg_balance).
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

uint16_t display_scan_len = 5;   // last DMA transfer count set here — segbal_poll steers 5 <-> 80

void setDisplayPWM(uint32_t bright){
  display_scan_len = (uint16_t)bright;
  HAL_DMA_Abort(&hdma_tim1_up);
  HAL_DMA_Abort(&hdma_tim7_up);
  HAL_DMA_Start(&hdma_tim1_up, (uint32_t)buffer_b, (uint32_t)&GPIOB->ODR, bright);
  HAL_DMA_Start(&hdma_tim7_up, (uint32_t)buffer_c, (uint32_t)&GPIOC->ODR, bright);
}

// CUCKOO (defined below segbal, which composes its levels): forward surface for the sections
// above it. ck_bright is a tentative definition here; the initialized definition follows.
#define CK_ROWS 9
static uint8_t ck_bright[CK_ROWS];
static uint8_t cuckoo_active(void);
void cuckoo_abort(void);
// Perceptual gamma: pieces write INTENT levels (0..16, meaning perceived lightness); the segbal
// compose maps them to dither DUTY through this table. Light output is linear in lit cycles but
// the eye is not (lightness ~ luminance^(1/2.2)): duties 13/16 and 16/16 differ by barely 7%
// perceived, which is why the bench found carry and pendulum nearly invisible while trust (a
// 2->16 swing) read fine. duty = 16*(L/16)^2.2, floored at 1 for any lit intent, so intent
// differences read evenly across the whole range. THE bench-tuning knob is this curve, not
// per-piece numbers.
static const uint8_t CK_GAMMA[17] = {0,1,1,1,1,1,2,3,4,5,6,7,9,10,12,14,16};

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

// --- Per-segment brightness balance (seg_balance) --------------------------------------------
// The display is voltage-driven (buffer chips + 10R per segment; the DAC sets the rail), so all
// lit segments of a digit share the drop across that digit's common return path: FEWER lit
// segments leaves more voltage per LED, so '-', '1' and '7' glow visibly brighter than '8' —
// worst at low brightness where the rail sits just above the LED forward voltage. There is no
// per-digit analog knob, but there is headroom in the scan: buffer_b/buffer_c are sized [80] and
// the DMA is circular, so the 5-step scan can be replayed as 16 cycles of 5 slots. Slots 0..4
// stay the live "master" slots every existing writer already targets; segbal_poll() (main loop,
// at most 1 kHz) mirrors them into slots 5..79 with each digit lit in s of its 16 cycles,
//     s = 16 - strength * (16 - 2 * popcount(segments)) / 100
// so at full strength s = 2N and every lit segment averages the same duty-per-segment product:
// (1/N) * (2N/16) = 1/8, uniform across the display (an 8-segment '8.' keeps 16/16 — sparse
// digits dim DOWN to its level, dense digits are untouched). The cycle mask picks each digit's s
// lit cycles as a NESTED rank window: rank = bit-reversed k rotated by a fixed per-digit phase
// (cycle 0 — the master slot — pinned at rank 0, so masters are never rewritten and count toward
// the duty exactly). Two properties are load-bearing, and each was a hardware-visible artefact
// when missing:
//  - NESTED (s+1's lit set = s's plus exactly one cycle): s changes whenever a digit's segment
//    count does — every second boundary at least — and a spread that RESHUFFLES on s±1 (e.g.
//    (k*s) % 16 < s, the first ship) re-phases the whole column's light at that instant: a
//    once-per-second step in the ghost bleed of unlit segments + a beat on the sub-second digits.
//  - DECORRELATED (distinct phase per digit): un-rotated nested ranks light every digit on the
//    SAME low-rank cycles, so the shared rail sees a sawtooth (all-on cycles vs sparse cycles,
//    per-cycle load variance ~14 vs ~1.4 rotated in sim) whose shape steps with the time content
//    — the whole DATE row blipped at 1 Hz and per-digit brightness left calibration (the K curve
//    was eyeballed against decorrelated patterns).
// Positions stay near-evenly spread at every s (a rotated rank window bit-reverses to a van-der-
// Corput run; worst case s = 1 is the master alone, unchanged from stock 5-slot timing).
//
// Above 100 the map turns into a power law, s = 16*(N/8)^(strength/100) — continuous at 100
// (gamma 1 = the linear map). Near the LED forward-voltage knee (low rail) segment current is
// exponential in voltage, so the real bloom ratio between a '-' and an '8.' can exceed what any
// LINEAR duty map can counter; hardware A/B showed 100 under-correcting at low brightness. The
// exponent steepens the curve: at 200, a 1-segment digit runs 1/16 duty against the 8-segment
// digit's 16/16. Tune live over serial against the actual display.
// Off (0, the default) leaves the stock 5-slot scan and all timing untouched.
//
// The bloom is rail-dependent, and NOT monotonically: hardware calibration (eyeball-matched at
// four rail levels) found a valley — strong compensation needed at the dim end (the LED knee,
// where current is exponential in voltage), moderate again at full rail (maximum-current IR drop
// in the shared return), mild in between. "seg_balance = on" applies that measured curve,
// interpolated by the live rail (dac_target, 4095 = dimmest); it is the whole intended interface.
// A numeric value (2..300) instead applies a fixed manual strength for experiments.
// seg_balance = 0/off (the default) keeps the stock scan and stock timing untouched.
volatile uint16_t seg_balance = 0;     // 0 = off (stock) · 1/on = AUTO (calibrated curve) · 2..300 = fixed manual strength

#define SEGBAL_BSEG_MASK 0x01FCu       // GPIOB word: bits 2..8 are segments; everything else
                                       // (bCat column selects etc.) passes through unmasked

// rank(k) for D=16 (bit-reversed k). For D=8/4 shift right by 1/2 — dropping the low bits of the
// reversal is exactly the 3-/2-bit reversal, so one table serves every depth.
static const uint8_t SEGBAL_REV16[16] = {0,8,4,12,2,10,6,14,1,9,5,13,3,11,7,15};
// Per-digit phases for the mirror ranks (col x bank; DP rides its digit's phase so its cycles
// stay a subset of the digit's). Rank 0 = the master slot is pinned; the phase rotates ranks
// 1..D-1 among themselves (mod D-1), so every digit still lights the master and its mirrors sit
// in a distinct rank window. Values are spread over the 15-ring (idx*4 mod 15, all distinct).
static const uint8_t SEGBAL_PH_B[5] = {0, 4, 8, 12, 1};
static const uint8_t SEGBAL_PH_C[5] = {5, 9, 13, 2, 6};

// Mirror freshness across main-loop stalls. The sub-second MASTERS are written from the SysTick
// ISR (SysTick_CountUp_*), so the stock 5-slot scan never lags the main loop — but the mirrors
// carry (D-1)/D of the light when balancing, and a refill that lives only in the main loop goes
// stale for the length of any long pass (the once-per-second PendSV display prep, housekeeping,
// a flash commit): hardware showed the sub-second digits freezing once per second, present since
// the first segbal ship and independent of the dither order. segbal_poll() therefore EXPORTS the
// per-column lit-cycle bitmaps, and every SysTick tick re-copies the live master values through
// them (segbal_isr_refresh), pinning mirror VALUE freshness to the same 1 ms the masters get.
// The bitmaps may lag a main-loop pass behind a glyph change — a duty lag of one pass,
// imperceptible where a frozen digit was not.
static volatile uint16_t segbal_lit_b[5], segbal_lit_c[5], segbal_lit_dp[5];  // bit k = cycle k lit
static volatile uint8_t  segbal_mirror_live = 0;   // ISR refresh armed (D-cycle scan up, bitmaps valid)

// Lit cycles (of 16) for a digit with n lit segments at effective strength `eff` (0..300).
// n <= 8 always (7 segments + DP). Returns 16 (always lit) .. 1 (floor for any lit digit).
static uint32_t segbal_duty(uint32_t n, uint32_t eff){
  if (n == 0) return 0;
  if (eff <= 100)                                          // linear blend: 100 -> s = 2n exactly
    return 16u - eff * (16u - 2u*n) / 100u;
  float s = 16.0f * powf((float)n / 8.0f, (float)eff / 100.0f);   // power law, gamma > 1
  uint32_t si = (uint32_t)(s + 0.5f);
  return si < 1u ? 1u : (si > 16u ? 16u : si);
}

// Effective strength for THIS refill. AUTO (seg_balance = 1/on) follows the hardware calibration:
// a full eyeballed sweep across the whole rail on a production Mk IV (2026-07-11) landed the even
// point on a clean EXPONENTIAL — the round-number rails fell on a x3-per-half-brightness geometric
// progression (rail brightest -> 10, mid -> 30, dimmest -> 90), i.e. K = 10 * 9^(dac/4095). That is
// exactly the LED knee: near the bottom of the rail, segment current is exponential in forward
// voltage, so a sparse digit pulls away exponentially fast and needs exponentially more duty haircut.
// 4095 is the 12-bit DAC full-scale code (dac_target maxes there, matching the original firmware); the
// intermediate breakpoints below stay on clean powers of two and K evaluates to the measured anchors.
// Sampled to a 9-point LUT (cheaper than a per-refill powf, and exponentials interpolate linearly to
// well under 1 K). A numeric value (2..300) instead applies a fixed manual strength for experiments.
// seg_balance = 0/off (the default) keeps the stock scan and stock timing untouched.
static const uint16_t SEGBAL_AUTO_DAC[9] = { 0, 512, 1024, 1536, 2048, 2560, 3072, 3584, 4095 };
static const uint16_t SEGBAL_AUTO_K[9]   = { 10,  13,   17,   23,   30,   39,   52,   68,   90 };  // 10*9^(dac/4095)

static uint32_t segbal_strength(void){
  if (seg_balance != 1) return seg_balance;                // manual fixed strength, or 0 = off
  int32_t d = (int32_t)dac_target;
  if (d <= SEGBAL_AUTO_DAC[0]) return SEGBAL_AUTO_K[0];
  for (uint32_t i = 1; i < 9; i++) {
    if (d <= (int32_t)SEGBAL_AUTO_DAC[i]) {
      int32_t d0 = SEGBAL_AUTO_DAC[i-1], d1 = SEGBAL_AUTO_DAC[i];
      int32_t k0 = SEGBAL_AUTO_K[i-1],   k1 = SEGBAL_AUTO_K[i];
      return (uint32_t)(k0 + (k1 - k0) * (d - d0) / (d1 - d0));
    }
  }
  return SEGBAL_AUTO_K[8];
}

// Dither depth (cycles per column) for the CURRENT scan rate. display_frequency is a user config
// (1..100 kHz) that retunes TIM1, and a fixed 16-cycle dither at a slow scan would visibly strobe:
// the dither repeats at step/(5*D) Hz, so pick the deepest D of {16,8,4} that stays >= ~200 Hz,
// and return 0 (balancing unavailable) below ~4 kHz steps. Stock rate (~62 kHz steps) gives D=16.
static uint32_t segbal_depth(void){
  uint32_t step = 16000000u / (TIM1->ARR + 1u);            // TIM1 clock 16 MHz (see setDisplayFreq)
  if (step >= 16000u) return 16u;
  if (step >=  8000u) return 8u;
  if (step >=  4000u) return 4u;
  return 0u;
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

  // CUCKOO (see the engine below): a piece in flight forces the D-cycle mirror exactly like a
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
      // so they read perceptually (see the table above). The >=1-cycle floor applies only
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
// millisecond, so the mirrors can never go staler than the masters (see the bitmap block above).
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
