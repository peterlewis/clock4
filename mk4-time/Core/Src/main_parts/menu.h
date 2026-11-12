// main_parts/menu.h — the two-button menu.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

// ---- The menu FSM's own state (the layer and the date-row text are in main.c: other sections read them) ----
static uint8_t   menu_section = 0;      // L1/L2 current section (SEC_*); preserved across exits (resume)
static uint8_t   menu_idx     = 0;      // L2 cursor into menu_items[] (ABSOLUTE index; walk by .section)
static int32_t   menu_val     = 0;      // L3 working value (already live via set-hook)
static uint8_t   menu_chord   = 0;      // a chord gesture is in progress (>=1 stage seen)
static uint8_t   menu_stage   = 0;      // 0..3 self-labeled chord stage currently shown
static uint32_t  menu_last_ms = 0;      // uwTick of the last event (drives 15 s idle)
static _Bool     menu_repaint = 0;      // a repaint was deferred out of the SysTick sendDate window
// §3c hold-acceleration: a run of same-direction STEP taps within ACCEL_GAP_MS grows the increment
// (main-loop-only state; never volatile / ISR-touched). Reset on enter-edit and on exit/idle.
static uint32_t  menu_run_last_ms = 0;
static int8_t    menu_run_dir = 0;
static uint16_t  menu_run_len = 0;
// ISR -> main-loop event ring (single-producer USART2 ISR, single-consumer menu_poll)
#define MENU_EVQ 16
static volatile uint8_t menu_evq[MENU_EVQ];
static volatile uint8_t menu_ev_h = 0, menu_ev_t = 0;
// Called from the USART2 ISR: O(1) enqueue only, so nextMode()/reset never run at ISR priority.
void menu_isr_event(uint8_t evt){
  uint8_t nh = (uint8_t)((menu_ev_h + 1u) & (MENU_EVQ - 1));
  if (nh != menu_ev_t) { menu_evq[menu_ev_h] = evt; menu_ev_h = nh; }   // drop if full
}

// ================= On-device 2-button menu (receiving FSM) ======================================
// Runs entirely in the MAIN LOOP (menu_poll), fed button events from the USART2 ISR via menu_evq.
// The 9-digit TIME row never leaves live GPS time; all menu chrome lives on the 10-char DATE row via
// menu_text (see sendDate). Settings edit LIVE (set-hooks mutate the running globals). Persistence to
// flash is a separate layer; here edits last until reboot. Buttons live on the DATE board, which must
// emit the 0x94/95/96 chord protocol for the menu to be reachable — until then this stays dormant and
// a bare 0x93 keeps its legacy reset. See EVT_* in main.h.
static int32_t menu_orig = 0;   // value captured on entering L2, for CANCEL
static uint8_t  menu_acolx_orig = 0;   // colonAltExplicit snapshot: CANCEL/idle must not leak the explicit flag (s_acolon sets it on every restore-write)
static uint8_t  menu_l0_snap_mode = 0; // displayMode before an L0 single tap fired nextMode...
static uint32_t menu_l0_snap_ms = 0;   // ...so a staggered both-press (tap leaked, then SETUP fires) can undo the accidental mode change
static uint8_t  menu_l0_snap_valid = 0;

// ---- date-row ownership + rendering ----
static void menu_show(const char *s){
  uint8_t n=0; while (n<10 && s[n]){ menu_text[n]=s[n]; n++; } menu_text[n]=0;
  if (decisec!=9) sendDate(1); else menu_repaint=1;   // keep menu paints out of the SysTick window
}
static void menu_flash(const char *s){ menu_show(s); }   // transient; cleared by the next render
static void menu_to_L0(void){
  menu_layer=L0_CLOCK; menu_chord=0; menu_stage=0; menu_run_dir=0; menu_run_len=0; menu_text[0]=0;
  if (colon_preview != 0xFF){ colon_preview = 0xFF; applyColonForMode(); }  // §3.5: never leave a preview stuck if we idle/EXIT mid-edit
  if (ck_menu_prev){ ck_menu_prev = 0; cuckoo_abort(); }                    // likewise the cuckoo editor's performing piece
  // KEEP menu_section/menu_idx: SETUP re-entry resumes on the last section (and last item, §fire_stage).
  if (decisec!=9) sendDate(1); else menu_repaint=1;      // restore the normal date row
}

// ---- mode toggle with the empty-ring guard (LASt) ----
static void menuSetMode(uint8_t m, _Bool on){
  if (!on){
    uint8_t j=0;
    for (uint8_t i=0;i<NUM_DISPLAY_MODES;i++)
      if (i!=MODE_STANDBY && config.modes_enabled[i]) j++;
    if (config.modes_enabled[m] && m!=MODE_STANDBY && j<=1){ menu_flash("LASt"); return; }  // refuse
  }
  config.modes_enabled[m]=on;
  if (!config.modes_enabled[displayMode]) nextMode(0);   // we disabled the current view -> advance
}

// ---- get/set hooks (edit the live globals; set-hooks apply the effect immediately) ----
static int32_t g_bright(const MItem*m){ (void)m; return (int32_t)config.brightness_override; }
static void    s_bright(const MItem*m,int32_t v){ (void)m; config.brightness_override=(float)v; }
static int32_t g_colon (const MItem*m){ (void)m; return colonModeCivil; }
static void    s_colon (const MItem*m,int32_t v){ (void)m; colonModeCivil=(uint8_t)v; if(colon_preview!=0xFF)colon_preview=(uint8_t)v; applyColonForMode(); }
static int32_t g_acolon(const MItem*m){ (void)m; return colonModeAlt; }
static void    s_acolon(const MItem*m,int32_t v){ (void)m; colonModeAlt=(uint8_t)v; colonAltExplicit=1; if(colon_preview!=0xFF)colon_preview=(uint8_t)v; applyColonForMode(); }
static int32_t g_page  (const MItem*m){ (void)m; return (int32_t)page_ms(); }   // EFFECTIVE (5500 default / 250 floor), not the raw-0 sentinel
static void    s_page  (const MItem*m,int32_t v){ (void)m; config.page_ms=(uint16_t)v; }
static int32_t g_sig   (const MItem*m){ (void)m; return significance_fade; }
static void    s_sig   (const MItem*m,int32_t v){ (void)m; significance_fade=v?1:0; }
static int32_t g_pps   (const MItem*m){ (void)m; return pps_ts_enabled; }
static void    s_pps   (const MItem*m,int32_t v){ (void)m; pps_ts_enabled=v?1:0; }
static int32_t g_brit  (const MItem*m){ (void)m; return brightness_report; }
static void    s_brit  (const MItem*m,int32_t v){ (void)m; brightness_report=v?1:0; }
static int32_t g_nmea  (const MItem*m){ (void)m; return nmea_cdc_level; }
static void    s_nmea  (const MItem*m,int32_t v){ (void)m; nmea_cdc_level=(uint8_t)v; }
static int32_t g_matrix(const MItem*m){ (void)m; return (int32_t)matrix_freq_hz; }
static void    s_matrix(const MItem*m,int32_t v){   // §4: scrub the NUMBER only; the rate is applied ONCE
  (void)m; if(v<1000)v=1000; if(v>100000)v=100000;   // on commit/cancel (setDisplayFreq there — see menu_commit_edit /
  matrix_freq_hz=(uint32_t)v;                         // menu_cancel_edit), never live. Live-writing the scan timers
}                                                     // TIM1/TIM7 — which clock the display DMA (TIM7 drives
                                                      // buffer_c->GPIOC) — on every tap stalled the very display the
                                                      // menu renders on: the digits corrupted then froze, needing an
                                                      // unplug. The number still previews (the L3 render reads menu_val),
                                                      // so nothing is lost — only the hardware apply is deferred to commit.
// TEMPCOMP: one on-device toggle arms the whole self-learning compensator — learn (sample ppm-vs-die-
// temp while GPS-locked) AND apply (steer SysTick from the model during GPS-loss holdover). config.txt
// still exposes tc_learn / tc_apply separately for asymmetric setups; the menu treats them as a pair.
static int32_t g_tc    (const MItem*m){ (void)m; return (tc_learn && tc_apply) ? 1 : 0; }
static void    s_tc    (const MItem*m,int32_t v){ (void)m; tc_learn = tc_apply = tc_persist = v?1:0; }   // the WHOLE compensator: learn + steer + persist the model
// BALANCE: one DISP toggle arms BOTH brightness-uniformity systems at their baked AUTO curves —
// per-segment duty equalisation (seg_balance) AND rail-tied colon dimming (colon_balance). config.txt
// keeps the finer seg_balance / colon_balance keys (incl. manual strengths) for calibration.
static int32_t g_bal   (const MItem*m){ (void)m; return (seg_balance && colon_balance) ? 1 : 0; }
static void    s_bal   (const MItem*m,int32_t v){ (void)m;
  // ON enables AUTO only where the sub-system is OFF, so toggling BALANCE never stomps a hand-tuned
  // manual seg_balance (2..300) / colon_balance (2..256) down to the coarse AUTO(1); OFF disables both.
  if (v){ if(!seg_balance) seg_balance=1; if(!colon_balance) colon_balance=1; }
  else  { seg_balance = colon_balance = 0; }
  colonForce = 1;
}
// F HOLD: in holdover, run at the oscillator frequency measured while locked (freq_hold). It sits
// beside TEMPCOMP: both decide what the clock does when GPS goes away.
static int32_t g_fh    (const MItem*m){ (void)m; return freq_hold ? 1 : 0; }
static void    s_fh    (const MItem*m,int32_t v){ (void)m; freq_hold = v?1:0; }
// WATCHDOG: ON arms it on the next main-loop pass. The hardware has no off switch, so OFF (and an
// abandoned edit that had turned it on) takes effect at the next reset; until then it keeps being fed.
static int32_t g_wdt   (const MItem*m){ (void)m; return watchdog ? 1 : 0; }
static void    s_wdt   (const MItem*m,int32_t v){ (void)m; watchdog = v?1:0; }
static int32_t g_mode  (const MItem*m){ return config.modes_enabled[m->lo]; }
static void    s_mode  (const MItem*m,int32_t v){ menuSetMode((uint8_t)m->lo, v?1:0); }
static void    s_fwcrc (const MItem*m,int32_t v){ (void)m; menuSetMode(MODE_FIRMWARE_CRC_T,v?1:0); config.modes_enabled[MODE_FIRMWARE_CRC_D]=v?1:0; if (!config.modes_enabled[displayMode]) nextMode(0); }  // mirror menuSetMode's guard: never strand the view on a just-disabled mode
// Factory reset (MIT_ACTION): EDIT opens a "SURE?" confirm; SAVE fires it -> factory_reset_step erases the
// on-device store and re-reads config.txt (back to defaults, no reboot). CANCEL / a zero commit is a no-op.
static int32_t g_reset (const MItem*m){ (void)m; return 0; }
static void    s_reset (const MItem*m,int32_t v){ (void)m; if (v) factory_reset_pending = 1; }

static const char *const en_colon[] = {"SLOWFADE","HEARTBt","1PPS SAW","ALT SAW","TOGGLE","FULL"};    // FULL not SOLID: S/O/I read as 5/0/1 on 7-seg
static const char *const en_nmea[]  = {"ALL","RMC","NONE"};
static const char *const en_cuckoo[]= {"OFF","TRUST"};   // shipped catalogue; parked pieces return here when reworked

// CUCKOO: selecting a value in the editor PERFORMS it on the time row immediately (ck_preview) —
// the menu's own colon-preview idiom applied to the flourishes. Only while the editor is open
// (ck_menu_prev, armed in menu_enter_edit); the cancel-path restore write is silent.
static int32_t g_cuckoo(const MItem*m){ (void)m; return cuckoo; }
static void    s_cuckoo(const MItem*m,int32_t v){ (void)m;
  uint8_t pv = cuckoo;
  cuckoo = (uint8_t)v;
  if (ck_menu_prev && cuckoo != pv) ck_preview(cuckoo);
}

#define MODE_ROW(mo,sec,lab) { KID_MODE_BASE+(mo), MIT_TOGGLE, lab, (mo),1,1, NULL, g_mode, s_mode, (sec) }
// Physical order UNCHANGED (menu_idx absolute, persistence keys off key_id); .section groups the ring.
static const MItem menu_items[] = {
  { KID_BRIGHTNESS, MIT_STEP,  "BRIGHT",  -1,4095,256, NULL,     g_bright, s_bright, SEC_DISP },
  { KID_BALANCE,    MIT_TOGGLE,"BALANCE",  0,1,1,       NULL,     g_bal,    s_bal,    SEC_DISP },   // per-segment + colon brightness uniformity (baked AUTO curves)
  { KID_COLON,      MIT_ENUM,  "COLON",    0,5,1,      en_colon, g_colon,  s_colon,  SEC_DISP },
  { KID_COLON_ALT,  MIT_ENUM,  "ACOLON",   0,5,1,      en_colon, g_acolon, s_acolon, SEC_DISP },   // 6-char label leaves room for the value at L2 (COLONALT hid it)
  { KID_PAGE_MS,    MIT_STEP,  "PAGE",     250,60000,250,NULL,   g_page,   s_page,   SEC_DISP },   // shows seconds; "MS"/unit-S both misread on 7-seg
  { KID_SIG_FADE,   MIT_TOGGLE,"SIG FADE", 0,1,1,      NULL,     g_sig,    s_sig,    SEC_DISP },
  { KID_CUCKOO,     MIT_ENUM,  "CUCKOO",   0,1,1,      en_cuckoo,g_cuckoo, s_cuckoo, SEC_DISP },   // each value PERFORMS as selected (ck_preview)
  { KID_PPS,        MIT_TOGGLE,"PPS MSG",  0,1,1,      NULL,     g_pps,    s_pps,    SEC_SYS  },   // the $PMTXTS timestamp sentence, NOT a 1PPS hardware output
  { KID_BRIT,       MIT_TOGGLE,"BRT MSG",  0,1,1,      NULL,     g_brit,   s_brit,   SEC_SYS  },   // the $PMBRIT dimmer report (brightness_report)
  { KID_NMEA,       MIT_ENUM,  "NMEA",     0,2,1,      en_nmea,  g_nmea,   s_nmea,   SEC_SYS  },
  { KID_MATRIX_FREQ,MIT_STEP,  "MATRIX",   8000,100000,1000,NULL,g_matrix, s_matrix, SEC_SYS  },   // menu floor 8000 (flicker); config MATRIX_FREQUENCY reaches the 1000 hw floor
  { KID_WATCHDOG,   MIT_TOGGLE,"WATCHDOG", 0,1,1,      NULL,     g_wdt,    s_wdt,    SEC_SYS  },   // the independent watchdog (config: watchdog)
  { KID_RESET,      MIT_ACTION,"RESET",    0,0,0,       NULL,     g_reset,  s_reset,  SEC_SYS  },   // factory-reset the on-device settings (confirm required)
  { KID_TEMPCOMP,   MIT_TOGGLE,"TEMPCOMP", 0,1,1,      NULL,     g_tc,     s_tc,     SEC_DIAG },   // arms learn+apply+persist; the TC DATA mode below just displays the model
  { KID_FREQ_HOLD,  MIT_TOGGLE,"F HOLD",   0,1,1,      NULL,     g_fh,     s_fh,     SEC_DIAG },   // holdover at the measured oscillator frequency (config: freq_hold)
  MODE_ROW(MODE_ISO8601_STD,SEC_CAL,"ISO 8601"), MODE_ROW(MODE_ISO_ORDINAL,SEC_CAL,"ISO ORD"),
  MODE_ROW(MODE_ISO_WEEK,SEC_CAL,"ISO WEEK"),     MODE_ROW(MODE_UNIX,SEC_CAL,"UNIX"),
  MODE_ROW(MODE_JULIAN_DATE,SEC_CAL,"JULIAN"),    MODE_ROW(MODE_MODIFIED_JD,SEC_CAL,"MOD JD"),
  MODE_ROW(MODE_SHOW_OFFSET,SEC_CAL,"UTC OFFS"),  MODE_ROW(MODE_SHOW_TZ_NAME,SEC_CAL,"TZ NAME"),
  MODE_ROW(MODE_ZONE2,SEC_CAL,"ZONE 2"),
  MODE_ROW(MODE_WEEKDAY,SEC_CAL,"WEEKDAY"),       MODE_ROW(MODE_WEEKDA_DD,SEC_CAL,"WKDAY DD"),
  MODE_ROW(MODE_WDY_MM_DD,SEC_CAL,"WDY MMDD"),    MODE_ROW(MODE_STANDBY,SEC_DISP,"STANDBY"),
  MODE_ROW(MODE_SATVIEW,SEC_DIAG,"SATVIEW"),      MODE_ROW(MODE_SUN,SEC_ASTRO,"SUN"),
  MODE_ROW(MODE_SUN_AZEL,SEC_ASTRO,"SUN AZEL"),   MODE_ROW(MODE_MOON,SEC_ASTRO,"MOON"),
  MODE_ROW(MODE_GRID,SEC_ASTRO,"GRID"),           MODE_ROW(MODE_LATLON,SEC_ASTRO,"LAT LON"),
  MODE_ROW(MODE_LST,SEC_ASTRO,"LST"),             MODE_ROW(MODE_SOLAR,SEC_ASTRO,"SOLAR"),
  MODE_ROW(MODE_ADEV,SEC_DIAG,"ADEV"),            MODE_ROW(MODE_STAR,SEC_ASTRO,"STAR"),
  MODE_ROW(MODE_DARK,SEC_ASTRO,"DARK"),
  MODE_ROW(MODE_TEMPCOMP,SEC_DIAG,"TC DATA"),   // the model READOUT; "TEMPCOMP" beside it is the enable
  { KID_MODE_BASE+MODE_FIRMWARE_CRC_T, MIT_TOGGLE, "FW CRC", MODE_FIRMWARE_CRC_T,1,1, NULL, g_mode, s_fwcrc, SEC_DIAG },
  MODE_ROW(MODE_VBAT, SEC_DIAG, "VBAT"),      // coin-cell health belongs beside the diagnostics (was config-only)
  // Read-only INFO rows (no editor, never persisted; .lo tags the readout). The everyday questions:
  // "am I disciplined right now?" and "did my star catalogue actually load?" — the second would have
  // surfaced a failed card a session earlier than the serial diagnostics did.
  { 0, MIT_INFO, "PPS",   0,0,0, NULL, NULL, NULL, SEC_DIAG  },   // "PPS LOCK" / "HOLD <s>" / "PPS ----"
  { 0, MIT_INFO, "STARS", 1,0,0, NULL, NULL, NULL, SEC_ASTRO },   // "STARS <n>": stars loaded from /STARS.BIN (0 = no file, or an invalid one)
};
static const char *const sect_name[NSEC] = { "CAL", "ASTRO", "DISP", "DIAG", "SYS" };
#define MENU_N ((uint8_t)(sizeof(menu_items)/sizeof(menu_items[0])))

static void menu_fmt_val(const MItem*m, int32_t v, char*out){   // out must hold >=10
  if (m->key_id==KID_BRIGHTNESS && v<0) { strcpy(out,"AUTO"); return; }
  if (m->type==MIT_ENUM && m->enums && v>=0 && v<=m->hi) { uint8_t n=0; const char*e=m->enums[v]; while(n<9&&e[n]){out[n]=e[n];n++;} out[n]=0; return; }
  if (m->type==MIT_TOGGLE) { strcpy(out, v?"ON":"OFF"); return; }
  snprintf(out,10,"%ld",(long)v);
}
// ---- section walk (physical table order UNCHANGED; step over rows in the current section) ----
static uint8_t menu_first_in_section(uint8_t sec){
  for (uint8_t i=0;i<MENU_N;i++) if (menu_items[i].section==sec) return i;
  return 0;   // every section is compile-time non-empty; 0 is a safe fallback
}
static uint8_t menu_step_in_section(uint8_t from, int dir){
  uint8_t i=from;
  for (uint8_t k=0;k<MENU_N;k++){                       // bounded; wraps within the whole table
    i=(uint8_t)((i + (dir>0 ? 1u : (unsigned)(MENU_N-1))) % MENU_N);
    if (menu_items[i].section==menu_section) return i;
  }
  return from;                                          // lone item in section -> stay put
}
static void menu_render_item(void){
  if (menu_layer==L1_SECTION){ menu_show(sect_name[menu_section]); return; }  // section ring == breadcrumb
  const MItem *m=&menu_items[menu_idx];
  char buf[20];
  if (menu_layer==L2_ITEM){
    if (m->type==MIT_INFO){                                                    // live read-only rows (no get/set hooks)
      if (m->lo==0){                                                           // PPS discipline state
        if (had_pps && (uint32_t)((uint32_t)currentTime - last_pps_time) <= 2u)
          snprintf(buf,sizeof buf,"PPS LOCK");
        else if (last_pps_time)
          snprintf(buf,sizeof buf,"HOLD %lu",(unsigned long)((uint32_t)currentTime - last_pps_time));
        else
          snprintf(buf,sizeof buf,"PPS ----");
      } else {                                                                 // star catalogue count (0 = no/invalid STARS.BIN)
        snprintf(buf,sizeof buf,"STARS %u",(unsigned)star_count);
      }
      menu_show(buf); return;
    }
    // §3b never hide a NUMBER: STEP items get a compact unit form + label-trim (the value is the point).
    // ENUM/TOGGLE keep the LABEL recognisable (you scroll by label) — truncate the value, or label-only.
    char val[10]; int32_t v = m->get(m);
    if (m->type==MIT_STEP){
      if      (m->key_id==KID_PAGE_MS)     snprintf(val,sizeof val,"%ld.%ld",(long)(v/1000),(long)((v%1000)/100)); // seconds, NO unit-S ('S' == the '5' glyph; "5.5S" read as 5.55)
      else if (m->key_id==KID_MATRIX_FREQ) snprintf(val,sizeof val,"%ld",(long)(v/1000));                          // 20 (kHz; K/Z have no 7-seg glyph, the MATRIX label carries the unit) -> "MATRIX 20"
      else                                 menu_fmt_val(m, v, val);            // BRIGHT etc.
      if (snprintf(buf,sizeof buf,"%s %s",m->label,val) > 10){                 // overflow -> keep the value, trim the label
        int labcap = 10 - 1 - (int)strlen(val);
        snprintf(buf,sizeof buf,"%.*s %s", labcap<0?0:labcap, m->label, val);
      }
    } else if (m->type==MIT_TOGGLE) {                                          // TOGGLE: the STATE must always be visible — full "ON"/"OFF" ("OF" rendered "0F" and read as the word of), trim the label if the row is tight (labels stay recognisable; you scrolled to it).
      const char *st = v ? "ON" : "OFF";
      int labcap = 10 - 1 - (int)strlen(st), ll = (int)strlen(m->label);
      if (ll > labcap) ll = labcap;
      snprintf(buf,sizeof buf,"%.*s %s", ll, m->label, st);
    } else if (m->type==MIT_ACTION) {                                          // ACTION: just the label at L2 (the confirm lives at L3)
      snprintf(buf,sizeof buf,"%.10s", m->label);
    } else {                                                                   // ENUM: keep the LABEL (you scroll by label), truncate the (long) value
      menu_fmt_val(m, v, val);
      if (snprintf(buf,sizeof buf,"%s %s",m->label,val) > 10){
        int valcap = 10 - (int)strlen(m->label) - 1;
        if (valcap >= 2) snprintf(buf,sizeof buf,"%s %.*s", m->label, valcap, val);   // keep label, truncate value
        else             snprintf(buf,sizeof buf,"%s", m->label);              // no room -> label only
      }
    }
    menu_show(buf);
  } else {                       // L3_EDIT: show the value being scrubbed
    if (m->type==MIT_ACTION)        menu_show("DELETE ALL");                                // action confirm ('?' has no 7-seg glyph — "SURE?" rendered "5URE-"); APPLY fires, CANCEL aborts
    else if (m->key_id==KID_MATRIX_FREQ) { snprintf(buf,sizeof buf,"%ld",(long)(menu_val/1000)); menu_show(buf); }  // kHz, matches the L2 form (stored value stays Hz; step 1000 Hz = 1 kHz)
    else if (m->key_id==KID_PAGE_MS) { snprintf(buf,sizeof buf,"%ld.%ld",(long)(menu_val/1000),(long)((menu_val%1000)/100)); menu_show(buf); }  // seconds in the editor too (was raw ms: "5500")
    else { menu_fmt_val(m, menu_val, buf); menu_show(buf); }
  }
}

// ---- L2 value editor (live-preview on the real digits) ----
static void menu_enter_edit(void){
  const MItem *m=&menu_items[menu_idx];
  if (m->type==MIT_INFO){ menu_render_item(); return; }   // nothing to edit — repaint the live readout
  menu_orig = m->get(m); menu_val = menu_orig; menu_run_dir=0; menu_run_len=0;
  if (m->key_id==KID_COLON_ALT) menu_acolx_orig = colonAltExplicit;   // s_acolon sets the flag on EVERY write incl. restores — snapshot so CANCEL/idle can put it back
  if (m->key_id==KID_COLON || m->key_id==KID_COLON_ALT){        // §3.5: begin previewing the choice under the cursor
    colon_preview = (uint8_t)menu_orig; applyColonForMode();
  }
  if (m->key_id==KID_CUCKOO){                                    // the CUCKOO editor performs each choice as it is selected
    ck_menu_prev = 1; ck_preview((uint8_t)menu_orig);
  }
  menu_layer=L3_EDIT; menu_render_item();
}
// §3c: coarse-while-held, fine-on-single-tap. A same-direction run within ACCEL_GAP_MS grows the
// increment by doubling after a short grace, capped at range/16; a pause or reversal drops back to
// fine. Accelerated steps snap to the coarse grid so you can't sail past a round target.
#define ACCEL_GAP_MS   350u   // slower than this (or a reversal) = a new run -> back to 1x
#define ACCEL_GRACE    2      // first taps of a run stay at 1x (precise nudge)
#define ACCEL_DOUBLE   2      // double the increment every 2 further held taps
#define ACCEL_SPAN_DIV 16     // fast cap = range/16  (~full sweep in a few seconds @5 Hz)
#define ACCEL_RUN_CAP  4000u  // bound run length so the tier arithmetic can't overflow
static int32_t menu_accel_inc(const MItem*m,int dir){
  uint32_t now=uwTick;
  if (dir==menu_run_dir && (uint32_t)(now-menu_run_last_ms)<=ACCEL_GAP_MS){
    if (menu_run_len<ACCEL_RUN_CAP) menu_run_len++;
  } else { menu_run_dir=(int8_t)dir; menu_run_len=0; }     // pause or reversal -> fine again
  menu_run_last_ms=now;
  int32_t inc=m->step, cap=(m->hi-m->lo)/ACCEL_SPAN_DIV; if(cap<m->step) cap=m->step;
  if (menu_run_len>=ACCEL_GRACE){
    uint16_t tiers=(uint16_t)((menu_run_len-ACCEL_GRACE)/ACCEL_DOUBLE);
    while (tiers-- && inc<cap) inc<<=1;
    if (inc>cap) inc=cap;
  }
  return inc*dir;
}
static void menu_edit_step(int dir){
  const MItem *m=&menu_items[menu_idx];
  int32_t want;
  if (m->type==MIT_ACTION) return;                                   // confirm screen ("SURE?") — nothing to scrub
  if (m->type==MIT_ENUM){ want=menu_val+dir; if(want>m->hi)want=0; if(want<0)want=m->hi; }
  else if (m->type==MIT_TOGGLE){ want=!menu_val; }
  else { int32_t d=menu_accel_inc(m,dir), inc=d<0?-d:d;    // MIT_STEP: accelerated increment
         want=menu_val+d;
         if (inc>m->step) want=(want/inc)*inc;             // snap to the coarse grid -> can't overshoot round targets
         if(want>m->hi)want=m->hi; if(want<m->lo)want=m->lo; }
  m->set(m, want);
  int32_t now = m->get(m);
  if (now!=want && m->type==MIT_TOGGLE){ menu_val=now; menu_flash("LASt"); return; }  // refused (LASt)
  menu_val=now; menu_render_item();
}
// §3.5: leave colon-preview and repaint the colon the display context actually calls for.
static void menu_colon_preview_end(void){
  if (colon_preview != 0xFF){ colon_preview = 0xFF; applyColonForMode(); }
}
// Leave cuckoo-preview: whatever piece the editor was performing stops on the plain face.
// (The selected VALUE survives — commit persists it, cancel already restored it via set.)
static void menu_cuckoo_preview_end(void){
  if (ck_menu_prev){ ck_menu_prev = 0; cuckoo_abort(); }
}
static void menu_cancel_edit(void){
  const MItem *m=&menu_items[menu_idx];
  m->set(m, menu_orig);                                            // restore pre-edit value
  if (m->key_id==KID_COLON_ALT) colonAltExplicit = menu_acolx_orig; // the restore-write just re-set the explicit flag — an abandoned edit must not defeat the auto-distinguish on the next config load
  if (m->key_id==KID_MATRIX_FREQ) setDisplayFreq(matrix_freq_hz);  // §4: force the last rate to the date board (set-hook throttles)
  menu_colon_preview_end();                                        // §3.5: back to the context colon
  menu_cuckoo_preview_end();                                       // the abandoned editor's piece stops on the plain face
  menu_layer=L2_ITEM; menu_render_item();
}
static void menu_commit_edit(void){
  const MItem *m=&menu_items[menu_idx];
  if (m->type==MIT_ACTION) { m->set(m, 1); }   // fire the one-shot action (e.g. factory reset) — nothing to persist
  else if (menu_val != menu_orig) {
    menu_record_key(m->key_id, menu_val);   // record for flash (value already applied live)
    if (m->key_id==KID_MATRIX_FREQ) setDisplayFreq(matrix_freq_hz);  // §4: authoritative final sync past the UART throttle
  } else {
    // no-op visit (or a LASt-refused tap that bounced back): nothing changed — don't burn a flash
    // record on it, and don't let the visit's live writes leak the COLONALT explicit flag either.
    if (m->key_id==KID_COLON_ALT) colonAltExplicit = menu_acolx_orig;
    if (m->key_id==KID_MATRIX_FREQ) setDisplayFreq(matrix_freq_hz);
  }
  menu_colon_preview_end();                                        // §3.5: back to the context colon
  menu_cuckoo_preview_end();                                       // the committed piece now waits for its hour
  menu_layer=L2_ITEM; menu_render_item();
}

// ---- rolling self-labeled chord stages (read the label, release on the one you want) ----
// Stage map (review-refined): APPLY and CANCEL are separated by the harmless "----" buffer stage so
// overshooting a commit can never silently discard the edit; the deep hold (stage 3) is a consistent
// "bail out" everywhere — CLOCK at L1/L2, CANCEL in editors — and at L0 arms REBOOT only on the
// SECOND stage cycle (~4.3 s: watch SETUP > ---- > ---- > SETUP > ---- > REBOOT) so an exploratory
// over-hold of the menu-entry gesture can't reset the clock.
static const char *const chord_L0[4]    = { "", "SETUP", "",       "REBOOT" };   // stage-3 label gated on the 2nd cycle (see menu_show_stage)
static const char *const chord_L1sec[4] = { "", "ENTER", "EXIT",   "CLOCK" };  // L1_SECTION
static const char *const chord_L2itm[4] = { "", "EDIT",  "BACK",   "CLOCK" };  // L2_ITEM
static const char *const chord_L3edt[4] = { "", "APPLY", "",       "CANCEL" };  // L3_EDIT ("SAVE" rendered "5A?E"; buffer stage between commit and discard)
static uint8_t menu_cycle = 0;          // completed 1-2-3 stage cycles within ONE chord hold (REBOOT arming)
static void menu_show_stage(void){
  const char *const *t = (menu_layer==L0_CLOCK)   ? chord_L0
                       : (menu_layer==L1_SECTION) ? chord_L1sec
                       : (menu_layer==L2_ITEM)    ? chord_L2itm : chord_L3edt;
  const char *s = (menu_stage<=3)? t[menu_stage] : "";
  if (menu_layer==L0_CLOCK && menu_stage==3 && menu_cycle==0) s = "";  // REBOOT arms on the 2nd cycle only — never label what won't fire
  if (menu_layer==L2_ITEM && menu_stage==1 && menu_items[menu_idx].type==MIT_INFO) s = "";  // read-only row: no EDIT to offer
  if (menu_layer==L3_EDIT && menu_items[menu_idx].type==MIT_TOGGLE && (menu_stage==1||menu_stage==2)) s = "DONE";  // a toggle exits-and-saves on either shallow release
  menu_show(s[0]? s : "----");
}
static void menu_fire_stage(void){
  if (!menu_chord) return;
  if (menu_layer==L0_CLOCK){
    if (menu_stage==1){                                                // SETUP -> section ring (resumes menu_section)
      // A staggered both-press leaked a single tap first (nextMode fired) — undo it, the user was
      // reaching for the menu. The undo goes THROUGH nextMode: a bare displayMode write left the
      // leaked mode's machinery installed (countMode, the SysTick/PPS handlers, the colon, a
      // standby display-off), so a leak into LST kept sidereal time ticking on a civil face.
      if (menu_l0_snap_valid && (uint32_t)(uwTick - menu_l0_snap_ms) < 2500u
          && displayMode != menu_l0_snap_mode && config.modes_enabled[menu_l0_snap_mode]){
        requestMode = menu_l0_snap_mode;
        nextMode(0);
      }
      menu_layer=L1_SECTION; menu_render_item();
    }
    else if (menu_stage==3 && menu_cycle>=1){ buttonsBothHeld(); }     // REBOOT (2nd cycle, ~4.3s) = NVIC reset (L0 ONLY). Factory reset is SYS>RESET.
    else menu_to_L0();                                                 // released on an unlabeled stage: restore the clock row (was: stuck showing "----")
  } else if (menu_layer==L1_SECTION){
    if (menu_stage==1){                                                // ENTER -> item ring of this section
      if (menu_items[menu_idx].section != menu_section) menu_idx = menu_first_in_section(menu_section);
      menu_layer=L2_ITEM; menu_render_item();     // land directly on the first item (resumes last item if still in-section) — no separate section banner
    } else menu_to_L0();                                               // EXIT (2) / CLOCK (3) -> clock
  } else if (menu_layer==L2_ITEM){
    if (menu_stage==1) menu_enter_edit();                              // EDIT -> open the item (enter, change, exit)
    else if (menu_stage==2){ menu_layer=L1_SECTION; menu_render_item(); } // BACK -> section ring
    else menu_to_L0();                                                 // CLOCK (deep hold) -> bail to the clock from anywhere
  } else { /* L3_EDIT */
    const MItem *mi = &menu_items[menu_idx];
    if (mi->type==MIT_TOGGLE){
      if (menu_stage<=2) menu_commit_edit();                           // DONE: exit = save on either shallow release (enter, toggle, exit)
      else menu_cancel_edit();                                         // deep hold = CANCEL, consistent with the value editors (stage-3 label says so)
    }
    else if (menu_stage==1) menu_commit_edit();                        // APPLY
    else if (menu_stage==3) menu_cancel_edit();                        // CANCEL (buffered one stage past APPLY)
    else menu_render_item();                                           // "----" buffer stage: no-op, but repaint the editor (the stage label took the row)
  }
  menu_chord=0; menu_stage=0; menu_cycle=0; menu_l0_snap_valid=0;
}
static void menu_dispatch(uint8_t e){
  if (e>=EVT_CHORD_S1 && e<=EVT_CHORD_S3){
    if (!menu_chord) menu_cycle=0;
    else if (e==EVT_CHORD_S1 && menu_stage==3) menu_cycle++;   // stages wrapped 3->1: one full cycle held through
    menu_chord=1; menu_stage=(uint8_t)(e-EVT_CHORD_S1+1); menu_show_stage(); return;
  }
  if (e==EVT_CHORD_REL){
    if (menu_chord) menu_fire_stage();
    else if (menu_layer==L0_CLOCK) buttonsBothHeld();    // BACKWARD COMPAT: stock date board 0x93 = reset
    return;
  }
  if (menu_layer==L0_CLOCK){
    menu_l0_snap_mode = displayMode; menu_l0_snap_ms = uwTick; menu_l0_snap_valid = 1;  // if this tap turns out to be a staggered half of the SETUP chord, fire_stage undoes the mode change
    if (e==EVT_BTN1) button1pressed(); else if (e==EVT_BTN2) button2pressed();
  } else if (menu_layer==L1_SECTION){
    if (e==EVT_BTN1) menu_section=(uint8_t)((menu_section+1)%NSEC);
    else if (e==EVT_BTN2) menu_section=(uint8_t)((menu_section+NSEC-1)%NSEC);
    menu_render_item();
  } else if (menu_layer==L2_ITEM){
    if (e==EVT_BTN1) menu_idx=menu_step_in_section(menu_idx,+1);
    else if (e==EVT_BTN2) menu_idx=menu_step_in_section(menu_idx,-1);
    menu_render_item();
  } else { /* L3_EDIT */
    if (e==EVT_BTN1) menu_edit_step(+1); else if (e==EVT_BTN2) menu_edit_step(-1);
  }
}
// Main-loop tick: flush a deferred repaint, enforce 15 s idle, then drain the event ring. The
// decisec!=9 gate keeps EVERY menu-originated sendDate (incl. the L0 nextMode path) out of the 1 ms
// window where the SysTick ISR runs its own non-reentrant sendDate(0).
void menu_poll(void){
  if (menu_repaint && decisec!=9){ menu_repaint=0; sendDate(1); }
  if (menu_layer!=L0_CLOCK && (uint32_t)(uwTick-menu_last_ms) >= MENU_IDLE_MS) {
    if (menu_layer==L3_EDIT) menu_cancel_edit();   // idle-out mid-edit == abandon: revert the live scrub + flush the final MATRIX rate to the date board, exactly like CANCEL (menu_to_L0 alone did neither)
    menu_to_L0();
  }
  // §7A display refinement (spec: mechanism A — time-board only, works on a stock date board):
  //  - EDITOR BLINK: a resting editor value blinks at 1 Hz (the digital-watch "you are setting
  //    this" idiom); it stays SOLID while actively scrubbing (any event in the last 600 ms) so the
  //    number never flickers under your thumb. ACTION confirms stay steady.
  //  - IDLE WARNING: ~3 s before the 15 s idle-abandon strikes, the whole row flickers once
  //    (300 ms) — "act or lose the edit". Any button press resets the clock on both behaviours.
  // Chord stage labels always win (menu_chord suppresses both).
  {
    static uint8_t menu_blanked = 0;
    uint8_t want = 0;
    if (menu_layer != L0_CLOCK && !menu_chord){
      uint32_t since = (uint32_t)(uwTick - menu_last_ms);
      if (since >= MENU_IDLE_MS - 3000u && since < MENU_IDLE_MS - 2700u) want = 1;   // the T-3 s flicker
      else if (menu_layer == L3_EDIT && menu_items[menu_idx].type != MIT_ACTION
               && since > 600u && (((since - 600u) / 500u) & 1u)) want = 1;          // resting blink (visible phase first)
    }
    if (want != menu_blanked){
      menu_blanked = want;
      if (want) menu_show(" ");            // blank the row (menu still owns it — a bare space pads to 10)
      else if (menu_layer != L0_CLOCK) menu_render_item();
    }
  }
  if (decisec==9) return;
  while (menu_ev_t != menu_ev_h){
    uint8_t e = menu_evq[menu_ev_t]; menu_ev_t=(uint8_t)((menu_ev_t+1)&(MENU_EVQ-1));
    menu_last_ms = uwTick;
    menu_dispatch(e);
  }
  // Flush a pending edit to flash only back at the clock, with the display UART idle and no PPS
  // timestamp waiting to emit — a page erase stalls the CPU ~20 ms and mustn't delay a $PMTXTS.
  // NB: guard on pps_emit_pending() (a REAL pending $PMTXTS emit), not pps_record_pending alone —
  // that flag latches high on any GPS-locked clock with PPS-out off, and would wedge the gate forever.
  if (menu_dirty && menu_layer==L0_CLOCK && !waitingForLatch &&
      huart2.gState==HAL_UART_STATE_READY && !pps_emit_pending()){
    if (ee_commit()) menu_dirty=0;
  }
}
