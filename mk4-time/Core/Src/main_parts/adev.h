// main_parts/adev.h — the Allan and Hadamard deviation engine.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

// ---- Live Allan deviation of the FREE-RUNNING HSE (MODE_ADEV) ----------------------------------
// The honest oscillator-stability signal is the free-running DWT phase, NOT the SysTick residual:
// capturePPS() re-pins SysTick every second (an infinite-gain phase reset), so the residual's ADEV
// rolls off artificially past tau=1 s and measures the discipline loop, not the crystal. DWT->CYCCNT
// is never re-pinned; its per-second delta minus the 80 MHz core count is the bare fractional-
// frequency error. We integrate that to cumulative phase (int32 ticks, 12.5 ns) in a RAM2 ring and
// compute the overlapping Allan deviation (IEEE-1139) on demand. A host can reconstruct the
// disciplined-output curve from $PMTXTS. Verified vs a double-precision reference in the emulator.
#define ADEV_N     4096u          // 1 Hz phase samples (16 KB of RAM2); tau to 1024 s @ N-2m=2048 overlaps
#define ADEV_OCT   11u            // octave taus 1,2,4,...,1024 s
#define ADEV_FCPU  80000000       // core ticks per true GPS second
#ifdef __EMSCRIPTEN__
static int32_t adev_x[ADEV_N];                                     // emu: plain static (no RAM2 section)
#else
__attribute__((section(".ram2"))) static int32_t adev_x[ADEV_N];   // RAM2 @ 0x10000000, off the CRC path
#endif
static uint32_t adev_prev_dwt, adev_prev_epoch;
// Cumulative phase, ticks. UNSIGNED on purpose: with a static ppm-scale offset the phase ramps
// monotonically and an int32 accumulator hits signed-overflow UB after ~311 days of continuous lock
// at 1 ppm. Unsigned wrap is defined; the ring stores the (int32_t) view and the second difference
// is computed back in unsigned, so everything stays exact modulo 2^32 (true |d| << 2^31 always).
static uint32_t adev_phase_u;
static uint16_t adev_widx, adev_valid;
static uint8_t  adev_have_prev;
static float    adev_sigma_cache[ADEV_OCT];
static volatile uint8_t  adev_noct;
static volatile uint32_t adev_last_ms;   // uwTick of the last accepted sample: the display's staleness tell

static void adev_reset(void){
  memset((void*)adev_x, 0, sizeof adev_x);   // RAM2 is NOT zeroed at boot — clear explicitly
  adev_phase_u=0; adev_widx=0; adev_valid=0; adev_have_prev=0; adev_noct=0;
  for (uint32_t i=0;i<ADEV_OCT;i++) adev_sigma_cache[i]=0.0f;
}
// Append a phase sample directly (also the emu/test path — bypasses the DWT delta).
static void adev_push_x(int32_t x){
  adev_x[adev_widx]=x;
  adev_widx=(uint16_t)((adev_widx+1u)%ADEV_N);
  if (adev_valid<ADEV_N) adev_valid++;
  adev_last_ms=uwTick;
}
// One phase sample per locked second from the PPS edge (ISR context). ONE missed second (a receiver
// hiccup / USB stall) is tolerated with a linear midpoint so a 68-minute record isn't discarded over
// a single dropout; anything longer is a real gap — restart the chain (overlapping ADEV needs
// contiguous samples) AND zero adev_noct immediately so the display can't keep painting the stale
// pre-gap curve until the next page flip.
static void adev_push_dwt(uint32_t dwt, uint32_t epoch){
  uint32_t de = adev_have_prev ? (uint32_t)(epoch-adev_prev_epoch) : 0u;
  if (de==1u){
    adev_phase_u += (dwt-adev_prev_dwt) - (uint32_t)ADEV_FCPU;  // exact across one 53.7 s DWT wrap
    adev_push_x((int32_t)adev_phase_u);
  } else if (de==2u){                                           // single missed PPS: interpolate the midpoint
    int32_t inc = (int32_t)((dwt-adev_prev_dwt) - 2u*(uint32_t)ADEV_FCPU);
    adev_phase_u += (uint32_t)(inc/2);        adev_push_x((int32_t)adev_phase_u);
    adev_phase_u += (uint32_t)(inc - inc/2);  adev_push_x((int32_t)adev_phase_u);
  } else {                                                      // first sample or a real gap -> restart at 0
    adev_phase_u=0; adev_x[0]=0; adev_widx=1; adev_valid=1;
    adev_noct=0;                                                // honest NOW, not at the next page flip
  }
  adev_prev_dwt=dwt; adev_prev_epoch=epoch; adev_have_prev=1;
}
// Overlapping Allan deviation for averaging factor m (tau = m s), read time-ordered across the ring.
static float adev_sigma_for_m(uint32_t m){
  uint32_t N=adev_valid;
  if (N < 2u*m+1u) return 0.0f;
  uint16_t base=(adev_valid<ADEV_N)?0u:adev_widx;            // oldest sample
  uint32_t cnt=N-2u*m;
  int64_t S=0;
  for (uint32_t i=0;i<cnt;i++){
    uint32_t a=(uint32_t)adev_x[(uint16_t)((base+i)%ADEV_N)];
    uint32_t b=(uint32_t)adev_x[(uint16_t)((base+i+m)%ADEV_N)];
    uint32_t c=(uint32_t)adev_x[(uint16_t)((base+i+2u*m)%ADEV_N)];
    int32_t d=(int32_t)(c-2u*b+a);                            // second difference, ticks (mod-2^32 exact: the accumulator wraps unsigned)
    S += (int64_t)d*d;
  }
  double var=(double)S/(2.0*(double)cnt);                    // int64 kernel; one double sqrt/octave
  return (float)(sqrt(var)/((double)ADEV_FCPU*(double)m));   // -> fractional frequency (dimensionless)
}
// Recompute the octave cache (thread context). Publish adev_noct under IRQ mask (torn-read guard).
// PUBLICATION gate: valid >= 4m (>= 2m overlapping triplets), stricter than the mathematical minimum
// 2m+1 — at exactly 2m+1 an octave is a SINGLE second-difference shown at full display authority
// (~100% error bars). adev_sigma_for_m itself keeps the mathematical gate for the test/oracle path.
static void adev_reduce(void){
  uint8_t noct=0;
  for (uint8_t k=0;k<ADEV_OCT;k++){
    uint32_t m=1u<<k;
    if (adev_valid < 4u*m) break;
    adev_sigma_cache[k]=adev_sigma_for_m(m);
    noct=(uint8_t)(k+1);
  }
  __disable_irq(); adev_noct=noct; __enable_irq();
}
// Display-path refresh: the page flip needs ONE octave, not all eleven — a full reduce over a filled
// ring is ~41k MAC iterations (~4-6 ms) recomputed per flip. Compute noct arithmetically and only
// the sigma the current page shows (~0.2-0.4 ms worst case). The $PMADEV dump still full-reduces.
static void adev_display_update(void){
  uint8_t noct=0;
  for (uint8_t k=0;k<ADEV_OCT;k++){ if (adev_valid < 4u*(1u<<k)) break; noct=(uint8_t)(k+1); }
  if (noct){
    uint32_t k=(uwTick/page_ms())%noct;
    adev_sigma_cache[k]=adev_sigma_for_m(1u<<(uint8_t)k);
  }
  __disable_irq(); adev_noct=noct; __enable_irq();
}
// Overlapping HADAMARD deviation for averaging factor m — the third-difference kernel cancels
// LINEAR FREQUENCY DRIFT (temperature ramp / aging) that plain ADEV retains as a tau^+1 slope, so
// the long-tau octaves report the oscillator, not the ramp. Serial-only ($PMHDEV): reuses the same
// phase ring, no extra RAM, computed on demand. Same unsigned modulo arithmetic as the ADEV kernel.
static float hdev_sigma_for_m(uint32_t m){
  uint32_t N=adev_valid;
  if (N < 3u*m+1u) return 0.0f;
  uint16_t base=(adev_valid<ADEV_N)?0u:adev_widx;
  uint32_t cnt=N-3u*m;
  int64_t S=0;
  for (uint32_t i=0;i<cnt;i++){
    uint32_t a=(uint32_t)adev_x[(uint16_t)((base+i)%ADEV_N)];
    uint32_t b=(uint32_t)adev_x[(uint16_t)((base+i+m)%ADEV_N)];
    uint32_t c=(uint32_t)adev_x[(uint16_t)((base+i+2u*m)%ADEV_N)];
    uint32_t d4=(uint32_t)adev_x[(uint16_t)((base+i+3u*m)%ADEV_N)];
    int32_t d=(int32_t)(d4 - 3u*c + 3u*b - a);              // third difference, ticks
    S += (int64_t)d*d;
  }
  double var=(double)S/(6.0*(double)cnt);
  return (float)(sqrt(var)/((double)ADEV_FCPU*(double)m));
}
static uint8_t hdev_noct(void){                             // same 4m maturity gate as the ADEV publish path
  uint8_t n=0;
  for (uint8_t k=0;k<ADEV_OCT;k++){ if (adev_valid < 4u*(1u<<k)) break; n=(uint8_t)(k+1); }
  return n;
}
// Display accessors — sendDate() is defined above the engine, so it reads through these.
static uint8_t adev_disp_noct(void){ return adev_noct; }
static float   adev_disp_sigma(uint8_t k){ return (k < ADEV_OCT) ? adev_sigma_cache[k] : 0.0f; }
