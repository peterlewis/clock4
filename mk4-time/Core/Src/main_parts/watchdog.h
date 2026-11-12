// main_parts/watchdog.h — the independent watchdog and its boot-loop guard.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

volatile uint8_t watchdog = 0;             // config "watchdog = on": arm the independent watchdog (explained below)
volatile _Bool wdt_dump_pending = 0;       // "wdt_dump = on" over serial -> one line: armed or not, resets, where it hung

// ==================== Watchdog (opt-in: watchdog = on) ==========================================
// The time row is painted by the SysTick handler and the scan runs on DMA, so a main loop that has
// hung - or a HardFault, whose handler spins - leaves a display that still looks alive: the last
// second frozen, or digits still counting with the timezone, menu, USB config and RTC writes dead
// behind them. Nothing recovers it but a power cycle. The independent watchdog (IWDG) runs on its own
// 32 kHz LSI and resets the MCU when the main loop stops refreshing it; the clock then comes back
// from the RTC within about a second and re-locks to GPS.
//
// TIMEOUT 8 s. The longest things the main loop does on purpose are a timezone lookup (~0.3 s) and
// a few QSPI sector erases in a row (<2 s worst case); a single HAL QSPI timeout is 5 s and should
// be survived, not punished with a reset. `loop_diag = on` reports the real worst gap ($PMLOOP).
//
// The loop is kicked from ONE place, the top of the main loop, so any stall anywhere trips it.
// It is armed only once the main loop is reached: the boot sequence before it reflashes the date
// board (doDateUpdate, tens of seconds, and hang_error is a deliberate stop with USB up so the file
// can be replaced) and must never be reset from under. A firmware update leaves through
// NVIC_SystemReset, and any reset stops the IWDG, so the bootloader always runs without it.
//
// BOOT-LOOP GUARD. A fault that recurs straight after every boot would turn "frozen" into "reboots
// forever". wdt_keep lives in RAM2, which survives a reset (not a power cycle) and which neither the
// startup code nor the bootloader touches; it counts consecutive watchdog resets. After
// WDT_STANDDOWN in a row the watchdog stands down for that boot, exactly the old behaviour, and
// re-arms once the clock has run WDT_HEALTHY_MS. It also keeps the main-loop section (the LP_MARK
// tag) that was running at the last watchdog reset: `wdt_dump = on` reports it.
//
// Off by default: with the key unset the IWDG is never started and behaviour is identical to stock.
#define WDT_MAGIC       0x57445431u   // "WDT1"
#define WDT_TIMEOUT_MS  8000u
#define WDT_STANDDOWN   3u            // consecutive watchdog resets before standing down
#define WDT_HEALTHY_MS  60000u        // uptime that proves a boot healthy: clears the run, re-arms
typedef struct {
  uint32_t magic;
  uint16_t resets;                    // watchdog resets since power-on
  uint8_t  consecutive;               // ...of which this many in a row, without a healthy run between
  uint8_t  tag;                       // main-loop section now running (LP_MARK); 0 = not in the loop yet
  uint8_t  last_tag;                  // the section running at the last watchdog reset; 0xFF = none
} wdt_keep_t;
#ifdef __EMSCRIPTEN__
static wdt_keep_t wdt_keep;                                    // emu: plain static (no RAM2 section)
static uint32_t wdt_emu_starts = 0, wdt_emu_kicks = 0;         // the emulator has no IWDG: count the calls
#else
__attribute__((section(".ram2"))) static wdt_keep_t wdt_keep;  // RAM2: NOLOAD, not zeroed, survives a reset
#endif
static uint8_t  wdt_armed = 0, wdt_stood_down = 0;
static uint32_t wdt_boot_ms = 0;

static void wdt_hw_start(void){
#ifdef __EMSCRIPTEN__
  wdt_emu_starts++;
#else
  DBGMCU->APB1FZR1 |= DBGMCU_APB1FZR1_DBG_IWDG_STOP;   // a debugger halt freezes the count instead of resetting
  IWDG->KR  = 0x0000CCCCu;                              // start (this also starts the LSI)
  IWDG->KR  = 0x00005555u;                              // unlock PR and RLR
  IWDG->PR  = 4u;                                       // LSI / 64 = 500 Hz
  IWDG->RLR = WDT_TIMEOUT_MS / 2u - 1u;                 // 3999 -> 8.0 s (LSI is +-5%)
  uint32_t t0 = uwTick;                                 // PR/RLR land on the LSI clock; never wait forever on it
  while (IWDG->SR && (uint32_t)(uwTick - t0) < 50u) {}
  IWDG->KR  = 0x0000AAAAu;                              // first refresh, at the new reload
#endif
}
static void wdt_hw_kick(void){
#ifdef __EMSCRIPTEN__
  wdt_emu_kicks++;
#else
  IWDG->KR = 0x0000AAAAu;
#endif
}
// Once per boot, before the main loop: account for the reset that got us here.
static void wdt_boot(_Bool was_wdt_reset){
  if (wdt_keep.magic != WDT_MAGIC) {                    // power-on: RAM2 holds noise
    wdt_keep.magic = WDT_MAGIC; wdt_keep.resets = 0; wdt_keep.consecutive = 0; wdt_keep.last_tag = 0xFF;
  }
  if (was_wdt_reset) {
    if (wdt_keep.resets < 0xFFFFu) wdt_keep.resets++;
    if (wdt_keep.consecutive < 0xFFu) wdt_keep.consecutive++;
    wdt_keep.last_tag = wdt_keep.tag;                   // where the main loop was when it stopped
  } else {
    wdt_keep.consecutive = 0;                           // any other reset is a fresh start
  }
  wdt_keep.tag = 0;
  wdt_stood_down = (wdt_keep.consecutive >= WDT_STANDDOWN);
  wdt_armed = 0;                                        // every reset stops the IWDG
  wdt_boot_ms = uwTick;
}
static void wdt_boot_hw(void){
#ifndef __EMSCRIPTEN__
  _Bool was = (RCC->CSR & RCC_CSR_IWDGRSTF) != 0;
  RCC->CSR |= RCC_CSR_RMVF;     // clear the reset flags: they persist otherwise, and every later reset would count
  wdt_boot(was);
#endif
}
// Top of every main-loop pass: the one place the watchdog is refreshed.
static void wdt_poll(void){
  if (wdt_keep.consecutive && (uint32_t)(uwTick - wdt_boot_ms) >= WDT_HEALTHY_MS) {
    wdt_keep.consecutive = 0;                           // this boot has proved healthy
    wdt_stood_down = 0;
  }
  if (!wdt_armed) {
    if (!watchdog || wdt_stood_down) return;
    wdt_hw_start();
    wdt_armed = 1;
  }
  wdt_hw_kick();
}
// "wdt_dump = on" over serial: one line. Same CDC/BUSY-retry contract as the other dump steps.
static SMALL_CODE void wdt_dump_step(void){
  static int      dn = -1;
  static uint16_t busy_ct = 0;
  static char     dline[192];
  if (!wdt_dump_pending) return;
  if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) { wdt_dump_pending = 0; dn = -1; return; }

  if (dn < 0) {
    const char *verdict =
        (wdt_armed && !watchdog) ? "ARMED (watchdog = off takes effect at the next reset)" :
        wdt_armed                ? "ARMED" :
        !watchdog                ? "OFF (watchdog = on arms it)" :
        wdt_stood_down           ? "STOOD DOWN (3 watchdog resets in a row; re-arms after 60 s of running)" :
                                   "ON (arms on the next main-loop pass)";
    char tag[8];
    if (wdt_keep.last_tag == 0xFF) { tag[0] = '-'; tag[1] = 0; }
    else snprintf(tag, sizeof tag, "%u", (unsigned)wdt_keep.last_tag);
    int nn = snprintf(dline, sizeof dline,
        "# watchdog: cfg=%u armed=%u timeout=%ums resets=%u consecutive=%u last_tag=%s -- %s\r\n",
        (unsigned)watchdog, (unsigned)wdt_armed, (unsigned)WDT_TIMEOUT_MS,
        (unsigned)wdt_keep.resets, (unsigned)wdt_keep.consecutive, tag, verdict);
    if (nn <= 0 || nn >= (int)sizeof dline) { wdt_dump_pending = 0; return; }
    dn = nn; busy_ct = 0;
  }

  __disable_irq();
  uint8_t r = CDC_Copy_Transmit((uint8_t*)dline, (uint16_t)dn);
  __enable_irq();
  if (r == USBD_BUSY) { if (++busy_ct > 5000) { wdt_dump_pending = 0; dn = -1; } return; }
  dn = -1; wdt_dump_pending = 0;
}
