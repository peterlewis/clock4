// main_parts/freq_hold.h — the oscillator's measured offset and learned wander.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

// Frequency hold (opt-in; explained below): in holdover, also steer out the oscillator's own offset,
// measured against GPS while locked. Independent of the tempcomp keys; the two corrections add.
volatile _Bool freq_hold = 0;

// ==================== Frequency hold (opt-in: freq_hold = on) ===================================
// In holdover the display runs on the SysTick at its NOMINAL reload, so it drifts at the oscillator's
// own offset from nominal. The tempcomp steering above only applies the CHANGE with temperature since
// GPS loss; the offset the oscillator already had at that moment was left in. Yet the firmware
// measures that offset exactly, every locked second: DWT->CYCCNT is latched at each PPS edge and is
// never re-pinned, so (this edge - last edge) - nominal is the oscillator's error in ticks per GPS
// second. freq_hold keeps the measurement and, in holdover, steers it out.
//
// Measured on a production Mk IV from 24 days of 1 Hz records (pccd): the offset sat at +0.12 ppm,
// steady to +-0.015 ppm from day to day. Replaying 44,000 GPS losses on that data, median / 95th
// percentile holdover error:
//                      10 min          1 h             6 h              24 h
//   unsteered        64 / 100 us     386 / 593 us    2.4 / 3.5 ms     10.6 / 10.9 ms
//   freq_hold       0.5 / 3.8 us     6.9 / 75 us     0.15 / 1.0 ms     2.0 / 3.8 ms
//
// THE ESTIMATE: 16 s blocks. A block's sum of per-second errors depends only on its two end edges
// (the inner edges cancel), so PPS jitter averages out, and a 16 s sum IS the mean in 1/16 tick per
// second: no division, and the fixed point the steering then uses. The estimate is the MEDIAN of the
// last five completed blocks (80 s), which shrugs off one bad block; the block in progress is never
// used, so the last seconds before a loss - when a failing fix may already be bending the PPS - stay
// out. Longer windows were no better on the data. Only edges with a valid fix count, the first ten
// after (re)acquisition are skipped while the receiver settles, and a >100 ppm step restarts the block.
//
// RESOLUTION: the tick ISR spreads whole ticks per second across the milliseconds. 1 tick/s is
// 12.5 ns/s, 45 us an hour - worth having, the data says (13.9 -> 6.9 us median at 1 h). So the
// governor carries the 1/16 remainder from second to second and adds a tick when it overflows.
#define FH_BLOCK_S  16u               // seconds per block
#define FH_BLOCKS   5u                // completed blocks kept
#define FH_MIN      3u                // completed blocks needed before there is an estimate (48 s of lock)
#define FH_FP       16                // steering fixed point: 1/16 tick per second (== FH_BLOCK_S)
static int32_t  fh_ring[FH_BLOCKS];   // completed block sums; a sum is that block's mean in 1/16 tick/s
static uint8_t  fh_count = 0, fh_head = 0;
static int32_t  fh_sum = 0;           // the block in progress
static uint8_t  fh_n = 0;
static uint32_t fh_prev_dwt = 0;
static _Bool    fh_have_prev = 0;
static uint8_t  fh_warm = 0;

// ---- The oscillator's WANDER, learned (feeds the significance-fade bound; see computeHoldoverFade) --
// How far does the frequency move once nobody is watching it? Consecutive 1024 s mean frequencies
// answer that directly: half the squared change between two of them is the Allan variance at 1024 s,
// and a random-walking frequency has variance a^2 * tau, so one number - the level a - predicts the
// time error of any holdover as a * tau^1.5. The clock averages it with a long memory (128 windows,
// about 36 hours), because the wander comes in bursts: a quiet hour says little about the next one.
// 64 of the 16 s blocks make a window; any break in the blocks breaks the window too.
#define FW_BLOCKS   64u               // 16 s blocks per 1024 s window
#define FW_WINDOW_S 1024.0f
#define FW_MEMORY   128u              // windows the average remembers
#define FW_MIN      8u                // windows needed before the learned level is used (2.3 h of lock)
static int32_t  fw_sum = 0;           // the window in progress (sum of block sums = ticks over the window)
static uint8_t  fw_n = 0;
static float    fw_prev_y = 0;        // the previous window's mean, ticks per second
static _Bool    fw_have_prev = 0;
static float    fw_var = 0;           // learned Allan variance at 1024 s, (ticks/s)^2
static uint16_t fw_count = 0;         // window pairs behind it
static _Bool    fw_new_pair = 0;      // a pair was just learned: the store checks whether it is worth saving

static void fw_break(void){ fw_sum = 0; fw_n = 0; fw_have_prev = 0; }
static void fw_block(int32_t block_sum){
  fw_sum += block_sum;
  if (++fw_n < FW_BLOCKS) return;
  float y = (float)fw_sum / FW_WINDOW_S;
  if (fw_have_prev) {
    float dy = y - fw_prev_y;
    if (fw_count < 0xFFFFu) fw_count++;
    fw_var += (0.5f * dy * dy - fw_var) / (float)(fw_count < FW_MEMORY ? fw_count : FW_MEMORY);
    fw_new_pair = 1;
  }
  fw_prev_y = y; fw_have_prev = 1;
  fw_sum = 0; fw_n = 0;
}
static void fh_learn(void){
  static uint32_t last_seq = 0;

  __disable_irq();                            // tear-free copy (tc_hse_learn pattern)
  uint32_t seq   = pps_cap.seq;
  uint32_t dwt   = pps_cap.dwt_pps;
  uint8_t  flags = pps_cap.flags;
  __enable_irq();

  if (seq == last_seq) return;                // no new edge since last pass
  _Bool contiguous = (seq == last_seq + 1);
  last_seq = seq;

  if ((flags & 0x3) != 0x3 || !contiguous) {  // no valid fix at this edge, or edges were missed
    fh_have_prev = 0; fh_warm = 0; fh_sum = 0; fh_n = 0;
    fw_break();
    return;                                   // completed blocks are kept: an old measurement beats none
  }
  if (!fh_have_prev) { fh_prev_dwt = dwt; fh_have_prev = 1; return; }
  int32_t d = (int32_t)((uint32_t)(dwt - fh_prev_dwt) - (tc_nom_load + 1u) * 1000u);   // wrap-exact
  fh_prev_dwt = dwt;
  if (fh_warm < 10) { fh_warm++; return; }    // settle after (re)acquisition

  int32_t tpp = tc_tpp();
  if (d > 100 * tpp || d < -100 * tpp) { fh_sum = 0; fh_n = 0; fw_break(); return; }   // >100 ppm: a glitch, not the oscillator
  fh_sum += d;
  if (++fh_n >= FH_BLOCK_S) {
    fh_ring[fh_head] = fh_sum;
    fw_block(fh_sum);                         // the wander learner counts the same block
    fh_head = (uint8_t)((fh_head + 1u) % FH_BLOCKS);
    if (fh_count < FH_BLOCKS) fh_count++;
    fh_sum = 0; fh_n = 0;
  }
}
// The current estimate, in 1/16 tick per second: the median of the completed blocks.
static _Bool fh_estimate(int32_t *e_fp){
  if (fh_count < FH_MIN) return 0;
  int32_t v[FH_BLOCKS];
  uint8_t n = fh_count;
  for (uint8_t i = 0; i < n; i++) {           // insertion sort, n <= 5
    int32_t x = fh_ring[i]; uint8_t j = i;
    while (j > 0 && v[j-1] > x) { v[j] = v[j-1]; j--; }
    v[j] = x;
  }
  *e_fp = (n & 1u) ? v[n/2] : (v[n/2 - 1] + v[n/2]) / 2;
  return 1;
}
