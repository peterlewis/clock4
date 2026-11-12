// main_parts/cuckoo.h — scheduled flourishes on the time row.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

// ================= CUCKOO — scheduled display flourishes (CUCKOO_SPEC.md v2) =================
// Two intrinsic tiers, after the Westminster pattern: each quarter (:15/:30/:45) gets one fixed
// small gesture — the NOD — and the hour gets the full configured piece. One config key,
// enable-and-select fused: `cuckoo = off | carry | heartbeat | pendulum | trust` (off default).
// There is no interval knob: the cadence is anchored to the face's own structure, and an
// interval would only license flourishes that no longer mark the hour.
//
// TIME-BOARD-ONLY, for two hardware reasons: the time row reads left-to-right in both poses
// (only the date board rotates, and it senses its own orientation — this board cannot), and the
// date row's UART carries text with no brightness, so whatever it holds (date, ADEV, star,
// countdown, text) keeps reading truthfully through every flourish.
//
// RENDERING rides the seg_balance dither mirror (one interleave engine, two clients): each
// piece writes a per-digit multiplier ck_bright[row] (0..16, 16 = nominal) and segbal_poll()
// composes it into the per-column duty exactly as the significance fade composes digit_bright.
// While a piece is in flight the D-cycle mirror is forced on (identity duty when seg_balance
// is off); when idle every level is 16 and the display path is byte-identical to stock.
// All timing derives from the live counters and the displayed stamp — never loop counts; every
// envelope is a terminating integer countdown, so a piece cannot fail to end, and its final
// frame is the plain live face.

// Catalogue as SHIPPED: trust only (first bench verdict, 2026-07-20 — "just keep trust and off
// for now"). carry / heartbeat / pendulum are PARKED: their bodies live in git (59fc649) and
// return through the same enum when their visual rework earns it. Parked means absent.
enum { CK_OFF = 0, CK_TRUST, CK_PIECES };
#define CK_NOD 0xFE
volatile uint8_t cuckoo = CK_OFF;      // the one config key: off, or the hourly piece
static uint8_t ck_menu_prev = 0;       // the menu's CUCKOO editor is open and previewing choices
// (CK_GAMMA — the intent->duty perceptual curve — lives with the forward block above displayOff,
//  because segbal_poll's compose reads it before this section is reached.)

// element rows: 0..4 = port-B big-digit categories (10h h 10m m 10s), 5 = seconds units
// (port-C column 0), 6..8 = ds/cs/ms (port-C columns 1..3). Left-to-right = rows 0..5.
// (CK_ROWS + the tentative ck_bright declaration live above displayOff.)
static uint8_t ck_bright[CK_ROWS] = {16,16,16,16,16,16,16,16,16};
static uint8_t  ck_anim = 0xFF;        // 0xFF idle; else CK_* piece or CK_NOD
static uint8_t  ck_phase = 0;          // per-piece state-machine step
static uint8_t  ck_armed = 0;          // 1 = edge-start pending
static uint8_t  ck_arm_sel = 0;        // what the pending edge starts (piece or CK_NOD)
static uint16_t ck_tick = 0;           // 10 ms ticks since piece start
static uint8_t  ck_last_cs = 0xFF;     // centisecond edge detector (main-loop tick)
static time_t   ck_last_arm = 0;       // trigger de-dupe: one start per armed second

static void ck_set_all(uint8_t level){
  for (uint8_t d = 0; d < CK_ROWS; d++) ck_bright[d] = level;
}

void cuckoo_abort(void){
  ck_anim = 0xFF; ck_armed = 0;
  ck_set_all(16);            // identity — segbal_poll drops the mirror when nothing needs it
}

static uint8_t cuckoo_active(void){ return ck_anim != 0xFF; }

static void ck_start(uint8_t anim){
  if (ck_anim != 0xFF) return;               // never preempt a running piece
  if (segbal_depth() == 0) return;           // scan too slow for the dither canvas — honest skip
  ck_anim = anim; ck_phase = 0; ck_tick = 0;
  ck_set_all(16);
}

// ---- the NOD — the fixed quarter gesture --------------------------------------------------
// A single shallow brightness trough sweeps the six big digits left to right: digit k starts
// at k*30 ms, dips 16 -> 8 -> 16 over 400 ms. Floor 8/16 keeps every digit fully legible;
// colons and the sub-second digits are untouched (fast elements get no blooms). Brightness
// only, no data — identical every quarter, in any pose, over any date-row content, and
// unchanged in holdover. NUMBERS PROVISIONAL pending the bench check (spec: respiration,
// not a power sag).
static void ck_nod_tick(void){
  for (uint8_t d = 0; d < 6; d++){
    uint16_t start = (uint16_t)(d * 3);                        // 30 ms stagger
    uint8_t lv = 16;
    if (ck_tick >= start){
      uint16_t age = (uint16_t)(ck_tick - start);              // trough is 40 ticks wide
      if (age < 40){
        uint16_t depth = (age < 20) ? age : (uint16_t)(40 - age);   // 0..20..0
        lv = (uint8_t)(16 - (depth * 8) / 20);                 // 16 -> 8 -> 16
      }
    }
    ck_bright[d] = lv;
  }
  if (ck_tick >= 6u * 3u + 40u) cuckoo_abort();                // ~550 ms and the row is clear
}

// ---- trust: how much the instrument actually knows right now ------------------------------
// X-ray dip, then a relight wave walks HH -> MM -> SS -> ds -> cs -> ms, each digit relighting
// to the confidence the tolerance ladder holds for it. Locked: full wave + one unified pulse.
// In holdover the wave dies at the honest position, holds the picture, then the dead rows fade
// back up to the live face (whose dashes are the resting form of the same claim).
static uint8_t ck_conf_of(uint32_t age, uint32_t tol){
  if (tol == 0 || age * 2u < tol) return 16;                   // comfortably inside the window
  if (age >= tol) return 0;                                    // past it: the ladder dashes this digit
  return (uint8_t)((32u * (tol - age)) / tol);                 // linear roll-off, top half
}
static uint8_t ck_conf(uint8_t row){
  if (row <= 5) return (had_pps || rtc_good) ? 16 : 2;         // whole seconds: set, or barely
  if (!had_pps && !rtc_good) return 0;                         // never locked: unanchored decimals
  uint32_t age = (uint32_t)currentTime - last_pps_time;
  uint32_t cal = (uint32_t)currentTime - rtc_last_calibration;
  if (row == 8) return ck_conf_of(age, config.tolerance_1ms);
  if (row == 7) return ck_conf_of(age, config.tolerance_10ms);
  // deciseconds mirror the ladder exactly: fresh PPS OR RTC-calibrated, whichever trusts more
  uint8_t a = ck_conf_of(age, config.tolerance_10ms);
  uint8_t b = ck_conf_of(cal, config.tolerance_100ms);
  return a > b ? a : b;
}

static void ck_trust_tick(void){
  if (ck_tick < 30){ ck_set_all(2); return; }                  // the x-ray dip
  for (uint8_t p = 0; p < CK_ROWS; p++)                        // the relight wave, MSB first
    ck_bright[p] = (ck_tick >= (uint16_t)(30 + p * 8)) ? ck_conf(p) : 2;
  if (ck_tick < 112) return;                                   // wave still walking (+ a beat)
  uint16_t t = (uint16_t)(ck_tick - 112);
  _Bool all16 = 1;
  for (uint8_t p = 0; p < CK_ROWS; p++) if (ck_conf(p) < 16) all16 = 0;
  if (all16){                                                  // locked: the unified pulse
    uint8_t lv = 16;
    if (t < 8)       lv = (uint8_t)(16 - t / 2);
    else if (t < 16) lv = (uint8_t)(12 + (t - 8) / 2);
    ck_set_all(lv);
    if (t >= 40) cuckoo_abort();
  } else {                                                     // honest picture, then back up
    for (uint8_t p = 0; p < CK_ROWS; p++){
      uint8_t c = ck_conf(p);
      uint8_t lv = c;
      if (c < 16) lv = (t >= 60) ? 16 : (uint8_t)(c + ((16 - c) * t) / 60);
      ck_bright[p] = lv;
    }
    if (t >= 80) cuckoo_abort();
  }
}

// ---- preview: play a piece NOW, outside the schedule ---------------------------------------
// Serves the menu editor (each value performs as it is selected) and the serial
// `cuckoo_preview` key. A preview PREEMPTS a running preview — a tap-through of the menu ring
// must respond per tap — which is a deliberate user act, not a scheduler exception. The
// honesty gates hold: pendulum without a fresh PPS previews as the NOD (the same stand-in the
// hour would get), and standby/text modes refuse exactly as the scheduler does.
static void ck_preview(uint8_t piece){
  cuckoo_abort();
  if (displayMode == MODE_STANDBY || countMode != COUNT_NORMAL) return;
  if (piece == CK_NOD) { ck_start(CK_NOD); return; }
  if (piece == CK_OFF || piece >= CK_PIECES) return;      // previewing "off" is the plain face
  ck_start(piece);
}

// ---- the scheduler + the 10 ms tick --------------------------------------------------------
// Main-loop poll. Triggers read the DISPLAYED stamp (nextBcd holds the shown values until the
// .900 restage). The cadence law: `minute==0 -> the piece; minute in {15,30,45} -> the nod`.
// The nod stands in when the hour piece cannot run honestly (pendulum in holdover) — the hour
// boundary is never wholly silent, and a stand-in gesture never forges the absent performance.
// MODE_STANDBY refuses to start anything (a dark clock stays dark) and aborts what runs;
// text/countdown modes likewise (the time row is not showing time).
void cuckoo_poll(void){
  if (centisec == ck_last_cs) return;
  ck_last_cs = centisec;

  if (ck_anim != 0xFF && (displayMode == MODE_STANDBY || countMode != COUNT_NORMAL)){
    cuckoo_abort();
    return;
  }

  if (ck_anim == 0xFF){
    if (cuckoo == CK_OFF || cuckoo >= CK_PIECES) { ck_armed = 0; return; }
    if (displayMode == MODE_STANDBY)            { ck_armed = 0; return; }
    if (countMode != COUNT_NORMAL)              { ck_armed = 0; return; }

    uint8_t ss = (uint8_t)(nextBcd.tenSeconds * 10 + nextBcd.seconds);
    uint8_t mm = (uint8_t)(nextBcd.tenMinutes * 10 + nextBcd.minutes);

    // Arm at .500 of the :59 second before the boundary; start ON the edge. (When the parked
    // pieces return, this is where a piece that cannot run honestly hands its hour to the nod,
    // and where a pre-edge piece like carry starts early — see the spec's stand-in law.)
    if (!ck_armed && ss == 59 && decisec == 5 && currentTime != ck_last_arm){
      uint8_t mm_next = (uint8_t)((mm + 1) % 60);
      if (mm_next == 0){                                        // the hour: the piece
        ck_last_arm = currentTime;
        ck_armed = 1; ck_arm_sel = cuckoo;
      } else if (mm_next == 15 || mm_next == 30 || mm_next == 45){   // a quarter: the nod
        ck_last_arm = currentTime;
        ck_armed = 1; ck_arm_sel = CK_NOD;
      }
    }
    if (ck_armed == 1 && decisec == 0 && centisec < 5){
      ck_armed = 0;
      ck_start(ck_arm_sel);
    }
    return;
  }

  ck_tick++;
  if      (ck_anim == CK_NOD)   ck_nod_tick();
  else if (ck_anim == CK_TRUST) ck_trust_tick();
  else cuckoo_abort();
}
// =============== end CUCKOO ==================================================================
