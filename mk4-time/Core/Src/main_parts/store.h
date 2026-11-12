// main_parts/store.h — SETTINGS.BIN: the menu settings store and the tempcomp store.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

// ---- Menu persistence: the RAM override store (mirrors the winning flash record) ----------------
// Records ONLY what the user changed on-device (which keys, their values, and the config.txt mtime at
// edit time), so the boot merge can honour config.txt precedence. cfg_*_defined marks which keys the
// current config.txt actually set, rebuilt each readConfigFile().
static struct {
  _Bool    valid;
  uint16_t stamp_fdate, stamp_ftime;   // config.txt mtime when the override was made
  uint16_t simple_mask;                // bit per persisted KID (u16: KID_MATRIX_FREQ=8 and up overflow a u8)
  int16_t  brightness; uint8_t colon, alt_colon; uint16_t page_ms;
  uint8_t  sig_fade, pps, nmea;
  uint32_t matrix_freq;                // KID_MATRIX_FREQ (u32: 100000 > u16)
  uint8_t  tc;                         // KID_TEMPCOMP: 1 = learn+apply+persist armed
  uint8_t  bal;                        // KID_BALANCE: 1 = seg+colon balance AUTO
  uint8_t  cuckoo;                     // KID_CUCKOO: the hourly piece (CK_OFF..CK_TRUST)
  uint64_t modes_mask, modes_val;      // bit per MODE_* ordinal (u64: MODE_STAR=31 filled a u32; ZONE2/RELATIV/HOLD need >=32)
  uint8_t  brit;                       // KID_BRIT: 1 = the 1 Hz $PMBRIT dimmer report (brightness_report)
  uint8_t  fh;                         // KID_FREQ_HOLD: 1 = freq_hold on
  uint8_t  wdt;                        // KID_WATCHDOG: 1 = watchdog on
} ovr;
static uint16_t cfg_simple_defined;    // bit per KID set by config.txt this load
static uint64_t cfg_modes_defined;     // bit per MODE_* ordinal set by config.txt this load
static uint32_t cfg_sig[16];           // per KID: a hash of the file's line(s) for that setting, this load
static uint32_t cfg_line_hash;         // the hash of the line now being parsed (readConfigFile sets it)
// config.txt owns this setting on this load. A setting can take several lines (tc_learn, tc_apply and
// tc_persist are all TEMPCOMP), so the hashes are chained: any of them changing changes the result.
#define cfg_owns(kid) do { cfg_simple_defined |= (uint16_t)(1u<<(kid)); \
                           cfg_sig[kid] = (cfg_sig[kid] * 16777619u) ^ cfg_line_hash; } while(0)
// What config.txt said when the stored choices were last merged with it, so the next load can tell
// which lines have changed. Kept in RAM and, beside every menu record, in the store (ee_pack_x): a
// clock is often plugged into a computer, edited, and unplugged before it has looked at the file
// again, and the comparison then has to be made at the next power-on.
static uint16_t cfg_prev_simple;
static uint64_t cfg_prev_modes, cfg_prev_modes_val;
static uint16_t cfg_prev_sig[16];      // the line hashes, folded to 16 bits
static _Bool    cfg_prev_known = 0;    // 0 until a load, or the store, has supplied the above
static _Bool    ee_ext_stored = 0;     // the newest menu record has its file record beside it
static _Bool    menu_dirty = 0;        // a menu edit is awaiting flash commit
volatile _Bool factory_reset_pending = 0;     // "factory_reset = on" over serial -> factory-reset the menu store
volatile _Bool menu_dump_pending = 0;      // "menu_dump = on" over serial -> emit the store-persistence diagnostic
volatile _Bool menu_export_pending = 0;    // a saved menu change, or "menu_export = on" over serial -> send the choices as config lines

// ---- Auto-persist the learned model to flash retained memory (opt-in: tc_persist = on) ----------
// The record stores exactly what tc_dump prints and tc_seed re-loads (ppm-domain coefficients), so
// boot warm-starts through the UNCHANGED tc_seed_apply path — auto-persist = "auto tc_dump into
// flash", auto-seed = "auto paste + tc_seed = on". tc2 is both the RAM shadow of the winning flash
// record AND the "last persisted model" the change-detector compares against.
volatile _Bool tc_persist = 0;              // config master opt-in (default off = stock behaviour)
volatile _Bool tc_forget_pending = 0;       // "tc_forget = on" over serial -> erase the tempcomp store
static   _Bool tc_model_dirty = 0;          // live model has moved beyond threshold since last persist
static uint32_t tc_last_commit_ms = 0;      // uwTick at last commit (MONOTONIC — never GPS wall time)
static _Bool   tc_persist_seeded = 0;       // a flash record seeded the model this boot (one-shot guard)
static struct {
  _Bool valid, hse_valid, lse_valid;
  int16_t t0, lo, hi;
  float hse_b, hse_c, lse_a, lse_b, lse_c, hse_resid, lse_resid;
  uint32_t n_hse, n_lse;
} tc2;
// config.txt "which tempcomp keys were set this load" — for the auto-seed precedence (config wins).
#define CFG_TC_SEED 1u
#define CFG_TC_T0   2u
#define CFG_TC_LO   4u
#define CFG_TC_HI   8u
static uint8_t cfg_tc_defined = 0;

// ================= Menu persistence: firmware-owned emulated-EEPROM =============================
// Two 4 KB pages, append-only ping-pong. Each 64-byte record: {magic, generation, schema, config.txt
// mtime stamp, the packed override set, CRC16 in the LAST doubleword}. Boot picks the highest-
// generation CRC-valid record. Each is followed by a file record (ee_pack_x): what config.txt said
// when these choices were last merged with it. The pages live in /SETTINGS.BIN on the QSPI FAT volume on EVERY clock,
// whatever the STM32L476 variant: the bootloader treats every die as 256 KB (boot + app end at
// 0x08040000 even on a 1 MB RG) and every board carries the same QSPI chip, so one medium means one
// path to test. The RG-only internal-flash home (the top of bank 2) is retired: records an earlier
// build left there are never read again, so an RG clock upgrading starts from config.txt.
#define EE_MAGIC   0x4D4B3445u        // "MK4E"  (menu store)
#define EE_SCHEMA  1u
#define EE_REC_SZ  64u                // 8 doublewords; CRC16 lives in DW7 so it programs last
#define EE_QSPI_PGSZ 4096u            // W25Q128 erase unit (one FAT cluster == one MSC block == one sector)
#define EE_SLOTS   (EE_QSPI_PGSZ / EE_REC_SZ)
#define EE2_MAGIC  0x4D4B3454u        // "MK4T"  (tempcomp store — distinct magic so the two never alias)
#define EE2_SCHEMA 1u
static uint32_t ee_page_a=0, ee_page_b=0, ee_active=0, ee_gen=0;
static uint16_t ee_next=0;            // next free slot in the active page
static _Bool    ee_avail=0;
// SECOND, independent store for the self-learning tempcomp model — its OWN page pair, so its write
// wear budget is fully disjoint from the menu store's (see the tempcomp-persist block far below).
static uint32_t ee2_page_a=0, ee2_page_b=0, ee2_active=0, ee2_gen=0;
static uint16_t ee2_next=0;
static _Bool    ee2_avail=0;

// --- Backing medium: /SETTINGS.BIN on the QSPI FAT volume (EE_BK_QSPI), a 16 KiB contiguous
// host-visible file whose four 4 KB sectors we rewrite RAW through qspi_drv — never touching FAT
// metadata, the dirent, or the file size, so the host, the bootloader and fsck all still see an
// ordinary opaque file. No file (or a fragmented one) -> EE_BK_NONE = the RAM-only fallback. File
// layout: +0x0000 menu page A | +0x1000 menu page B | +0x2000 tempcomp page A | +0x3000 tempcomp page B.
typedef enum { EE_BK_NONE=0, EE_BK_QSPI } EEBacking;
static EEBacking ee_backing = EE_BK_NONE;
static uint8_t   ee_sfile_state = 0;          // 0 no SETTINGS.BIN | 1 ok | 2 fragmented/too small | 3 QSPI read fault
// Set by ee_rd32/ee_rd when a QSPI read fails. A failed read returns zeros, and zeros look exactly
// like "no record here, page not blank" - the state that licenses an ERASE. So every path that is
// about to erase or program checks ee_fault_check() after its reads and before the first write.
static _Bool     ee_rd_fault = 0;
// Host-write generation: STORAGE_Write_FS bumps it on EVERY host MSC write. A commit whose mapping
// generation is stale must re-resolve the file's physical base first — the host may have moved,
// rewritten or deleted SETTINGS.BIN, and a blind write against the old base could hit fwt.bin or the
// FAT itself (the one clobber path the design review flagged as boot-medium-endangering).
volatile uint32_t settings_map_gen = 0;
static uint32_t   settings_map_seen = 0;

#ifdef __EMSCRIPTEN__
// The emu models the QSPI store with TRUE NOR semantics: program can only clear bits (AND), erase
// refills a whole 4 KB sector with 0xFF. The old memcpy-overwrite shim let every persistence test
// pass while the hardware silently never persisted; never bring it back.
#define EE_SFILE_SZ 16384u
static uint8_t ee_sfile[EE_SFILE_SZ];
static uint8_t ee_sfile_attached = 1;   // SETTINGS.BIN present on the emulated card (tests can detach)
static uint8_t ee_sfile_frag = 0;       // pretend the file is fragmented -> resolver must reject
static uint8_t ee_sfile_rdfault = 0;    // every read fails, as a QSPI HAL error would (tests)
static uint32_t ee_rd32(uint32_t a){ if (ee_sfile_rdfault){ ee_rd_fault = 1; return 0; } uint32_t v; memcpy(&v, &ee_sfile[a], 4); return v; }
static void ee_rd(uint32_t a, void*d, uint32_t n){ if (ee_sfile_rdfault){ ee_rd_fault = 1; memset(d, 0, n); return; } memcpy(d, &ee_sfile[a], n); }
static void ee_erase(uint32_t a){ memset(&ee_sfile[a & ~(EE_QSPI_PGSZ-1u)], 0xFF, EE_QSPI_PGSZ); }
static void ee_prog_dw(uint32_t a, uint64_t v){
  uint8_t s[8]; memcpy(s,&v,8);
  for (int i=0;i<8;i++) ee_sfile[a+i] &= s[i];                    // NOR: 1->0 only
}
#else
// Native: straight to the QSPI chip. Every caller holds a resolved SETTINGS.BIN (ee_avail/ee2_avail
// gate them all). Reads retry through the driver's reentrancy self-abort the same way USER_read does
// (the MSC ISR always releases the lock before returning to thread mode).
static QSPI_STATUS ee_qspi_rd(uint32_t a, void*d, uint32_t n){
  QSPI_STATUS s;
  do { s = QSPI_Read((uint8_t*)d, a, n); } while (s == QSPI_STATUS_LOCKED);
  return s;
}
static uint32_t ee_rd32(uint32_t a){
  uint32_t v;
  if (ee_qspi_rd(a, &v, 4) != QSPI_STATUS_OK) { ee_rd_fault = 1; return 0; }   // zeros + the fault flag: callers must check before any erase
  return v;
}
static void ee_rd(uint32_t a, void*d, uint32_t n){
  if (ee_qspi_rd(a, d, n) != QSPI_STATUS_OK) { ee_rd_fault = 1; memset(d, 0, n); }
}
static void ee_erase(uint32_t a){ QSPI_Erase_Sector(a); }
static void ee_prog_dw(uint32_t a, uint64_t v){ uint8_t b[8]; memcpy(b,&v,8); QSPI_Program(b, a, 8); }
#endif
// The store's one rule for a failed read: stop, and touch nothing. Returns 1 if a read has failed
// since boot; both stores then run RAM-only until the next power-on (which retries), so no erase or
// program can ever be decided from bytes that were never read.
static _Bool ee_fault_check(void){
  if (!ee_rd_fault) return 0;
  ee_backing = EE_BK_NONE; ee_avail = 0; ee2_avail = 0; ee_sfile_state = 3;
  return 1;
}

static uint16_t ee_crc16(const uint8_t*p, uint32_t n){   // CRC-16-CCITT (poly 0x1021, init 0xFFFF)
  uint16_t c=0xFFFF;
  for (uint32_t i=0;i<n;i++){ c ^= (uint16_t)((uint16_t)p[i]<<8);
    for (int b=0;b<8;b++) c = (c&0x8000)?(uint16_t)((c<<1)^0x1021):(uint16_t)(c<<1); }
  return c;
}
// Record byte layout: 0 magic u32 | 4 gen u32 | 8 schema u16 | 10 fdate u16 | 12 ftime u16 |
// 14 simple_mask u16 | 16 modes_mask_lo u32 | 20 modes_val_lo u32 | 24 brightness i16 | 26 colon u8 |
// 27 alt_colon u8 | 28 page_ms u16 | 30 sig_fade u8 | 31 pps u8 | 32 nmea u8 | 33 matrix_freq u32 | 37 tc u8 | 38 bal u8 | 39 cuckoo u8 |
// 40 modes_mask_hi u32 | 44 modes_val_hi u32 | 48 brit u8 | 49 freq_hold u8 | 50 watchdog u8 | 51..61 rsvd | 62 crc16.
// (bytes 15, 33..36, 37, 38, 39, 40..47 and 48..50 were zeroed padding in every prior record, so widening simple_mask
//  u8->u16, adding matrix_freq u32 + tc u8 + bal u8 + cuckoo u8 + brit u8 + freq_hold u8 + watchdog u8, and splitting the mode masks u32->u64
//  with their high words at 40/44 all round-trip old records with those fields clear -> modes >=32 read disabled;
//  EE_SCHEMA stays 1.)
static void ee_pack(uint8_t*r, uint32_t gen){
  memset(r,0,EE_REC_SZ);
  uint32_t mg=EE_MAGIC; memcpy(r+0,&mg,4); memcpy(r+4,&gen,4);
  uint16_t sc=EE_SCHEMA; memcpy(r+8,&sc,2);
  memcpy(r+10,&ovr.stamp_fdate,2); memcpy(r+12,&ovr.stamp_ftime,2);
  memcpy(r+14,&ovr.simple_mask,2);
  uint32_t mm_lo=(uint32_t)ovr.modes_mask, mv_lo=(uint32_t)ovr.modes_val;
  uint32_t mm_hi=(uint32_t)(ovr.modes_mask>>32), mv_hi=(uint32_t)(ovr.modes_val>>32);
  memcpy(r+16,&mm_lo,4); memcpy(r+20,&mv_lo,4);
  memcpy(r+24,&ovr.brightness,2); r[26]=ovr.colon; r[27]=ovr.alt_colon;
  memcpy(r+28,&ovr.page_ms,2); r[30]=ovr.sig_fade; r[31]=ovr.pps; r[32]=ovr.nmea;
  memcpy(r+33,&ovr.matrix_freq,4); r[37]=ovr.tc; r[38]=ovr.bal; r[39]=ovr.cuckoo;
  memcpy(r+40,&mm_hi,4); memcpy(r+44,&mv_hi,4);
  r[48]=ovr.brit; r[49]=ovr.fh; r[50]=ovr.wdt;
  uint16_t crc=ee_crc16(r,62); memcpy(r+62,&crc,2);
}
// The file record: what config.txt said at the last merge. It is written in the slot after the menu
// record of the same generation and is only believed there; a menu record without one (an older
// build's, or a write cut short between the two) falls back on the stamp. Its magic and schema differ
// from the menu record's, so a build that does not know it skips the slot.
// Layout: 0 magic u32 | 4 gen u32 | 8 schema u16 | 10 settings the file mentions u16 |
// 12 modes it mentions u64 | 20 what it says for them u64 | 28 line hash per setting, 16 x u16 |
// 60..61 rsvd | 62 crc16.
#define EE_XMAGIC  0x4D4B3458u        // "MK4X"
#define EE_XSCHEMA 0x5801u
static void ee_pack_x(uint8_t*r, uint32_t gen){
  memset(r,0,EE_REC_SZ);
  uint32_t mg=EE_XMAGIC; memcpy(r+0,&mg,4); memcpy(r+4,&gen,4);
  uint16_t sc=EE_XSCHEMA; memcpy(r+8,&sc,2);
  memcpy(r+10,&cfg_prev_simple,2); memcpy(r+12,&cfg_prev_modes,8); memcpy(r+20,&cfg_prev_modes_val,8);
  memcpy(r+28,cfg_prev_sig,32);
  uint16_t crc=ee_crc16(r,62); memcpy(r+62,&crc,2);
}
static void ee_unpack_x(const uint8_t*r){
  memcpy(&cfg_prev_simple,r+10,2); memcpy(&cfg_prev_modes,r+12,8); memcpy(&cfg_prev_modes_val,r+20,8);
  memcpy(cfg_prev_sig,r+28,32);
  cfg_prev_known=1;
}
static void ee_unpack(const uint8_t*r){
  memcpy(&ovr.stamp_fdate,r+10,2); memcpy(&ovr.stamp_ftime,r+12,2);
  memcpy(&ovr.simple_mask,r+14,2);
  uint32_t mm_lo,mv_lo,mm_hi,mv_hi;
  memcpy(&mm_lo,r+16,4); memcpy(&mv_lo,r+20,4);
  memcpy(&mm_hi,r+40,4); memcpy(&mv_hi,r+44,4); ovr.brit=r[48]; ovr.fh=r[49]; ovr.wdt=r[50];
  ovr.modes_mask=((uint64_t)mm_hi<<32)|mm_lo; ovr.modes_val=((uint64_t)mv_hi<<32)|mv_lo;
  memcpy(&ovr.brightness,r+24,2); ovr.colon=r[26]; ovr.alt_colon=r[27];
  memcpy(&ovr.page_ms,r+28,2); ovr.sig_fade=r[30]; ovr.pps=r[31]; ovr.nmea=r[32];
  memcpy(&ovr.matrix_freq,r+33,4); ovr.tc=r[37]; ovr.bal=r[38]; ovr.cuckoo=r[39];
  ovr.valid=1;
}
// Resolve /SETTINGS.BIN into physical QSPI sector addresses through the read-only FATFS the firmware
// already mounts. The file must be >=16 KiB and CONTIGUOUS (verified via the fastseek cluster-link
// map: a single fragment yields exactly ulen==4 = [used, ncl, tcl, 0]); anything else -> reject ->
// RAM-only fallback. The physical base is the same arithmetic FATFS itself uses (clust2sect:
// (sclust-2)*csize + database), times the fixed 4096-byte sector (_MIN_SS==_MAX_SS==4096, and the MSC
// maps blk*0x1000 -> QSPI 1:1), so store and host agree on physical bytes exactly.
#ifdef __EMSCRIPTEN__
static _Bool settings_resolve(void){
  ee_sfile_state = ee_sfile_attached ? (ee_sfile_frag ? 2 : 1) : 0;
  if (ee_sfile_state != 1) return 0;
  ee_page_a = 0x0000u; ee_page_b = 0x1000u;
  ee_backing = EE_BK_QSPI;
  return 1;
}
#else
static _Bool settings_resolve(void){
  FIL f; DWORD clmt[8];
  ee_sfile_state = 0;
  if (f_open(&f, "/SETTINGS.BIN", FA_READ) != FR_OK) return 0;        // absent -> RAM-only
  _Bool ok = 0;
  do {
    if (f_size(&f) < 4u*EE_QSPI_PGSZ) { ee_sfile_state = 2; break; }  // too small
    f.cltbl = clmt; clmt[0] = 8;                                      // fastseek CLMT (8 dwords)
    if (f_lseek(&f, CREATE_LINKMAP) != FR_OK) { ee_sfile_state = 2; break; }   // overflow = fragmented
    if (clmt[0] != 4) { ee_sfile_state = 2; break; }                  // must be ONE fragment
    DWORD ncl = clmt[1], sclust = clmt[2];
    if ((uint32_t)ncl * USERFatFS.csize * 4096u < 4u*EE_QSPI_PGSZ) { ee_sfile_state = 2; break; }
    uint32_t sect = (sclust - 2u) * USERFatFS.csize + USERFatFS.database;   // == clust2sect
    uint32_t base = sect * 4096u;
    if (base + 4u*EE_QSPI_PGSZ > 0x1000000u) { ee_sfile_state = 2; break; } // must fit the 16 MB chip
    ee_page_a = base; ee_page_b = base + EE_QSPI_PGSZ;
    ee_backing = EE_BK_QSPI; ee_sfile_state = 1; ok = 1;
  } while (0);
  f_close(&f);
  return ok;
}
#endif
static void ee_init_base(void){
  ee_backing = EE_BK_NONE;
  ee_rd_fault = 0;                                   // a new boot retries the medium
#ifdef __EMSCRIPTEN__
  settings_resolve();                                // emu: the same file, attachable/detachable by tests
#else
  fatfs_busy = 1;
  settings_resolve();                                // no usable file -> stays EE_BK_NONE (RAM-only)
  fatfs_busy = 0;
#endif
  ee_avail = (ee_backing != EE_BK_NONE);
  settings_map_seen = settings_map_gen;              // boot mapping is fresh by definition
}
static _Bool ee_page_blank(uint32_t base){
  for (uint32_t o = 0; o < EE_QSPI_PGSZ; o += 4) if (ee_rd32(base + o) != 0xFFFFFFFFu) return 0;
  return 1;
}
// Scan both pages for the highest-generation CRC-valid record; position the append cursor. Shared by
// boot load (unpack=1) and the post-host-write remap rescan (unpack=0 — RAM state may be newer than
// anything stored, so a rescan must never clobber ovr).
static void ee_scan(_Bool unpack){
  uint8_t rec[EE_REC_SZ], best[EE_REC_SZ];           // best = the CRC-verified bytes of the winner (never re-read)
  int found=0; uint32_t best_gen=0, best_page=ee_page_a; uint16_t best_slot=0;
  ee_active=ee_page_a; ee_next=0;
  for (int pg=0; pg<2; pg++){
    uint32_t base = pg? ee_page_b : ee_page_a;
    for (uint16_t s=0; s<EE_SLOTS; s++){
      uint32_t addr = base + (uint32_t)s*EE_REC_SZ;
      if (ee_rd32(addr) != EE_MAGIC) continue;
      ee_rd(addr, rec, EE_REC_SZ);
      uint16_t sc; memcpy(&sc,rec+8,2); if (sc!=EE_SCHEMA) continue;
      uint16_t crc; memcpy(&crc,rec+62,2); if (ee_crc16(rec,62)!=crc) continue;   // torn write -> reject
      uint32_t gen; memcpy(&gen,rec+4,4);
      if (!found || gen>best_gen){ found=1; best_gen=gen; best_page=base; best_slot=s; memcpy(best,rec,EE_REC_SZ); }
    }
  }
  if (found){
    if (unpack) {
      ee_unpack(best);                                      // a record that passed its CRC is safe to apply, fault or not
      ee_ext_stored = 0;
      if (best_slot + 1u < EE_SLOTS) {                      // its file record, if it has one: the next slot, same generation
        uint32_t xa = best_page + (uint32_t)(best_slot+1)*EE_REC_SZ;
        if (ee_rd32(xa) == EE_XMAGIC) {
          ee_rd(xa, rec, EE_REC_SZ);
          uint16_t sc, crc; uint32_t gen;
          memcpy(&sc,rec+8,2); memcpy(&crc,rec+62,2); memcpy(&gen,rec+4,4);
          if (sc==EE_XSCHEMA && ee_crc16(rec,62)==crc && gen==best_gen) { ee_unpack_x(rec); ee_ext_stored = 1; }
        }
      }
    }
    ee_active=best_page; ee_next=(uint16_t)(best_slot+1);   // append after the winner
    if (best_gen > ee_gen) ee_gen = best_gen;               // gen stays monotonic across remaps
  }
  if (ee_fault_check()) return;      // a read failed: "no record" may be a lie - never erase on it
  if (!found && ee_backing == EE_BK_QSPI){
    // First use of a freshly-provisioned (or host-rewritten) file: NOR can only program 1->0, so a
    // 0x00-filled or garbage-filled file would AND every record into a CRC failure FOREVER. Erase the
    // pair to true 0xFF before the first append. Mapping is fresh here (boot or just-revalidated).
    _Bool blank = ee_page_blank(ee_page_a) && ee_page_blank(ee_page_b);
    if (ee_fault_check()) return;    // "not blank" from a failed read is not a reason to erase
    if (!blank){
      ee_erase(ee_page_a); ee_erase(ee_page_b);
      ee_active=ee_page_a; ee_next=0;
    }
  }
  // NOR skip-to-blank: never leave the cursor on a non-blank slot (a torn write there would AND-merge
  // into unpredictable bytes). Cursor may land == EE_SLOTS -> the next commit rolls over onto an
  // erased page.
  while (ee_next < EE_SLOTS && ee_rd32(ee_active + (uint32_t)ee_next*EE_REC_SZ) != 0xFFFFFFFFu) ee_next++;
  ee_fault_check();
}
// Boot: derive the base, scan both pages, unpack the highest-generation CRC-valid record into ovr.
void ee_load(void){
  ee_init_base();
  ovr.valid=0; ee_active=ee_page_a; ee_next=0; ee_gen=0;
  cfg_prev_known = 0; ee_ext_stored = 0;   // a boot: what the file last said comes from the store, if it is there
  if (!ee_avail) return;
  ee_scan(1);
}
// "menu_dump = on" over serial: ONE human-readable line reporting whether the on-device override store
// (menu settings + tempcomp model) is backed by SETTINGS.BIN or RAM-only, and if RAM-only, why. It
// also reports the die's flash size (RG 1024 KB, RC 256 KB): the store no longer depends on it, but
// it's the one way to learn which STM32L476 a clock was built with without reading the chip marking.
// Same CDC/BUSY-retry contract as star_dump_step.
static void menu_dump_step(void){
  static int      dn = -1;
  static uint16_t busy_ct = 0;
  static char     dline[192];   // the RAM-ONLY verdicts run to ~165 chars: at NMEA_BUF_SIZE (128) the line
                                // overflowed and the dump was dropped in exactly the cases it exists to explain.
                                // 192 = the CDC transmit buffer.
  if (!menu_dump_pending) return;
  if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) { menu_dump_pending = 0; dn = -1; return; }

  if (dn < 0) {
#ifdef __EMSCRIPTEN__
    unsigned kb = 0;                                  // emu: RAM-backed store, no die flash-size register
#else
    unsigned kb = *(uint16_t*)FLASHSIZE_BASE;         // die-programmed flash size in KB (RG 1024, RC 256)
#endif
    static const char *bk_nm[2] = { "NONE", "QSPI" };
    const char *verdict =
        (ee_backing == EE_BK_QSPI)     ? "QSPI SETTINGS.BIN (settings persist)" :
        (ee_sfile_state == 3)          ? "RAM-ONLY (QSPI read fault - file left untouched, power-cycle to retry)" :
        (ee_sfile_state == 2)          ? "RAM-ONLY (SETTINGS.BIN fragmented/too small - reformat + recreate it first)" :
                                         "RAM-ONLY (no SETTINGS.BIN on the drive)";
    int nn = snprintf(dline, sizeof dline,
        "# menu store: avail=%u flash=%uKB backing=%s page_a=%08lX gen=%lu ovr=%u -- %s\r\n",
        (unsigned)ee_avail, kb, bk_nm[ee_backing], (unsigned long)ee_page_a,
        (unsigned long)ee_gen, (unsigned)ovr.valid, verdict);
    if (nn <= 0 || nn >= (int)sizeof dline) { menu_dump_pending = 0; return; }
    dn = nn; busy_ct = 0;
  }

  __disable_irq();
  uint8_t r = CDC_Copy_Transmit((uint8_t*)dline, (uint16_t)dn);
  __enable_irq();
  if (r == USBD_BUSY) { if (++busy_ct > 5000) { menu_dump_pending = 0; dn = -1; } return; }
  dn = -1; menu_dump_pending = 0;
}
// Forward decl (defined with ee2 below — it re-bases BOTH stores after a host write moves the file).
static void ee2_scan(_Bool unpack);
// QSPI-backing commit precondition. (1) Defer while host MSC I/O is in flight or just settled — the
// bus is busy and the FAT may be mid-mutation. (2) If the host has written ANYTHING since the mapping
// was last validated (settings_map_gen advanced), re-resolve /SETTINGS.BIN before trusting the cached
// physical base: the file may have moved (rebase + rescan, never unpack — RAM is newer), shrunk, or
// vanished (drop to RAM-only). This is what makes a stale-mapping write — the one failure the design
// review found that could hit fwt.bin or the FAT itself — impossible: no commit ever runs against a
// base the host could have invalidated.
static _Bool settings_mapping_ok(void){
  if (ee_backing != EE_BK_QSPI) return 1;
  if (qspi_write_time || qspi_usb_read_time) return 0;   // host mid-I/O: defer, retry next pass
  uint32_t snap = settings_map_gen;
  if (snap == settings_map_seen) return 1;
  if (delayedReadConfigFile) return 0;                   // remount+config re-read pending: after it
  uint32_t old_a = ee_page_a;
  fatfs_busy = 1;
  _Bool ok = settings_resolve();
  fatfs_busy = 0;
  if (!ok){                                              // file gone/fragmented -> RAM-only from here
    ee_backing = EE_BK_NONE; ee_avail = 0; ee2_avail = 0;
    return 0;
  }
  if (settings_map_gen != snap) return 0;                // host wrote DURING the resolve: retry
  if (ee_page_a != old_a){                               // file moved: rebase both stores + rescan
    ee2_page_a = ee_page_a + 2u*EE_QSPI_PGSZ; ee2_page_b = ee_page_a + 3u*EE_QSPI_PGSZ;
    ee_scan(0); ee2_scan(0);
    if (!ee_avail) return 0;                             // a read failed during the rescan: RAM-only now
  }
  settings_map_seen = snap;
  return 1;
}
// Content sentinel, QSPI only: the first word of the active page must be our magic or blank. If the
// host rewrote the file's CONTENTS in place (same clusters, garbage bytes), the mapping is fresh but
// the log is gone — re-initialise the pair (we own every byte of the file) and append from slot 0.
// Never reached with a stale mapping (settings_mapping_ok runs first), so the erase stays inside the
// file. `magic` distinguishes the menu pair from the tempcomp pair.
static _Bool ee_sentinel(uint32_t magic, uint32_t page_a, uint32_t page_b,
                         uint32_t *active, uint16_t *next){
  if (ee_backing != EE_BK_QSPI) return 1;
  uint32_t s0 = ee_rd32(*active);
  if (ee_fault_check()) return 0;                        // could not read the sentinel: not "garbage", unknown
  if (s0 == magic || s0 == 0xFFFFFFFFu) return 1;
  ee_erase(page_a); ee_erase(page_b);
  *active = page_a; *next = 0;
  _Bool erased = ee_rd32(page_a) == 0xFFFFFFFFu;         // erase verified
  return !ee_fault_check() && erased;
}
// Write ovr as the next record. Erase + switch page when the active one is full (CRC lands last, so a
// power loss mid-write just fails CRC and ee_load falls back to the prior generation). Caller gates.
static _Bool ee_commit(void){
  if (!ee_avail || !ovr.valid) return 0;
  if (!settings_mapping_ok()) return 0;
  uint8_t rec[EE_REC_SZ];
  fatfs_busy = 1;          // eject-triggered reset must defer across the WHOLE multi-op commit
  if (!ee_sentinel(EE_MAGIC, ee_page_a, ee_page_b, &ee_active, &ee_next)){ fatfs_busy = 0; return 0; }
  // Never program a non-blank slot (NOR AND-merges): skip past strays.
  while (ee_next < EE_SLOTS && ee_rd32(ee_active + (uint32_t)ee_next*EE_REC_SZ) != 0xFFFFFFFFu) ee_next++;
  if (ee_fault_check()){ fatfs_busy = 0; return 0; }   // "every slot is used" from failed reads must not erase the other page
  if (ee_next + 2u > EE_SLOTS){                         // room for the pair: the two records never straddle pages
    uint32_t other = (ee_active==ee_page_a)? ee_page_b : ee_page_a;
    ee_erase(other); ee_active=other; ee_next=0;
  }
  uint32_t addr = ee_active + (uint32_t)ee_next*EE_REC_SZ;
  ee_pack(rec, ee_gen+1);
  for (uint32_t o=0;o<EE_REC_SZ;o+=8){ uint64_t dw; memcpy(&dw, rec+o, 8); ee_prog_dw(addr+o, dw); }
  ee_gen++; ee_next++;
  // The menu record above is the commit point. The file record follows it: cut short here, the menu
  // record stands alone and the next boot falls back on the stamp. A page always starts with a menu
  // record, which is what ee_sentinel looks for.
  ee_ext_stored = 0;
  if (cfg_prev_known){
    addr += EE_REC_SZ;
    ee_pack_x(rec, ee_gen);
    for (uint32_t o=0;o<EE_REC_SZ;o+=8){ uint64_t dw; memcpy(&dw, rec+o, 8); ee_prog_dw(addr+o, dw); }
    ee_next++; ee_ext_stored = 1;
  }
  fatfs_busy = 0;
  menu_export_pending = 1;       // tell a listening host what the store now holds (menu_export_step)
  return 1;
}

// =============== Tempcomp store (ee2): auto-persist the learned model to its own page pair =========
// Parallel to the menu store, reusing its address helpers (ee_rd32/ee_rd/ee_erase/ee_prog_dw/ee_crc16)
// and framing (magic @0, gen @4, CRC @62 -> programs last -> torn write fails CRC -> prior gen wins).
// Record byte layout: 0 magic | 4 gen | 8 schema | 10 flags(b0 hse,b1 lse) | 12 t0 i16 | 14 hse_b f32 |
// 18 hse_c f32 | 22 lse_a f32 | 26 lse_b f32 | 30 lse_c f32 | 34 lo i16 | 36 hi i16 | 38 hse_resid f32 |
// 42 lse_resid f32 | 46 n_hse u32 | 50 n_lse u32 | 54 wander variance f32 | 58 wander windows u16 |
// 60..61 rsvd | 62 crc16.
// (54..61 were zeroed padding in every earlier record, so an old record reads as "no wander stored"
//  and EE2_SCHEMA stays 1. The wander is the fade bound's learned level: fw_var / fw_count.)
static void ee2_init_base(void){
  if (ee_backing == EE_BK_QSPI){
    // The file's upper half: menu owns +0x0000/+0x1000, tempcomp owns +0x2000/+0x3000. One resolve
    // (done by ee_init_base, which ee_load ran first) covers both stores.
    ee2_page_a = ee_page_a + 2u*EE_QSPI_PGSZ;
    ee2_page_b = ee_page_a + 3u*EE_QSPI_PGSZ;
    ee2_avail = 1;
    return;
  }
  ee2_avail = 0;                                     // no usable SETTINGS.BIN -> RAM-only, like the menu store
}
// What the store last held, beyond the tc2 fields: the record itself, so a commit that has no live
// model to write can carry the stored one forward byte for byte, and the stored wander.
static uint8_t tc2_raw[EE_REC_SZ];
static struct { float var; uint16_t count; } fw2;      // count < FW_MIN: no wander stored

// with_live: write the live model. Otherwise (a commit made only to save the wander, or a live model
// not yet good enough to persist) the stored model is carried forward untouched, or left absent.
static void tc_pack(uint8_t*r, uint32_t gen, _Bool with_live){
  if (with_live && (tc_hse_valid || tc_lse_valid)) {
    memset(r,0,EE_REC_SZ);
    uint32_t mg=EE2_MAGIC; memcpy(r+0,&mg,4);
    uint16_t sc=EE2_SCHEMA; memcpy(r+8,&sc,2);
    float tpp = (float)tc_tpp(); if (tpp<=0.0f) tpp = 80.0f;
    r[10] = (uint8_t)((tc_hse_valid?1:0) | (tc_lse_valid?2:0));
    int16_t t0=tc_t0; memcpy(r+12,&t0,2);
    float hb = tc_hse_valid? tc_hse_m[1]/tpp : 0.0f, hc = tc_hse_valid? tc_hse_m[2]/tpp : 0.0f;
    memcpy(r+14,&hb,4); memcpy(r+18,&hc,4);
    float la=tc_lse_valid?tc_lse_m[0]:0.0f, lb=tc_lse_valid?tc_lse_m[1]:0.0f, lc=tc_lse_valid?tc_lse_m[2]:0.0f;
    memcpy(r+22,&la,4); memcpy(r+26,&lb,4); memcpy(r+30,&lc,4);
    int16_t lo = tc_hse_valid? tc_hse_tmin : tc_lse_tmin;                // union coverage (like tc_dump case 9)
    int16_t hi = tc_hse_valid? tc_hse_tmax : tc_lse_tmax;
    if (tc_lse_valid){ if (tc_lse_tmin<lo) lo=tc_lse_tmin; if (tc_lse_tmax>hi) hi=tc_lse_tmax; }
    memcpy(r+34,&lo,2); memcpy(r+36,&hi,2);
    memcpy(r+38,&tc_hse_resid,4); memcpy(r+42,&tc_lse_resid,4);
    memcpy(r+46,&tc_n_hse,4); memcpy(r+50,&tc_n_lse,4);
  } else if (tc2.valid) {
    memcpy(r, tc2_raw, EE_REC_SZ);                      // the stored model, exactly as it was
  } else {
    memset(r,0,EE_REC_SZ);                              // no model at all: flags 0
    uint32_t mg=EE2_MAGIC; memcpy(r+0,&mg,4);
    uint16_t sc=EE2_SCHEMA; memcpy(r+8,&sc,2);
  }
  memcpy(r+4,&gen,4);
  float wv = 0.0f; uint16_t wc = 0;                     // the wander: the live level once learned, else what was stored
  if (fw_count >= FW_MIN)      { wv = fw_var;  wc = fw_count; }
  else if (fw2.count >= FW_MIN){ wv = fw2.var; wc = fw2.count; }
  memset(r+54,0,8); memcpy(r+54,&wv,4); memcpy(r+58,&wc,2);
  uint16_t crc=ee_crc16(r,62); memcpy(r+62,&crc,2);
}
static void tc_unpack(const uint8_t*r){
  uint8_t fl=r[10]; tc2.hse_valid=(fl&1)!=0; tc2.lse_valid=(fl&2)!=0;
  memcpy(&tc2.t0,r+12,2);
  memcpy(&tc2.hse_b,r+14,4); memcpy(&tc2.hse_c,r+18,4);
  memcpy(&tc2.lse_a,r+22,4); memcpy(&tc2.lse_b,r+26,4); memcpy(&tc2.lse_c,r+30,4);
  memcpy(&tc2.lo,r+34,2); memcpy(&tc2.hi,r+36,2);
  memcpy(&tc2.hse_resid,r+38,4); memcpy(&tc2.lse_resid,r+42,4);
  memcpy(&tc2.n_hse,r+46,4); memcpy(&tc2.n_lse,r+50,4);
  tc2.valid=1;
  memcpy(tc2_raw, r, EE_REC_SZ);
  // The stored wander, if it is believable: finite, not negative, under (100 ticks/s)^2, and learned.
  float wv; uint16_t wc; memcpy(&wv,r+54,4); memcpy(&wc,r+58,2);
  if (wc >= FW_MIN && isfinite(wv) && wv >= 0.0f && wv <= 1.0e4f) { fw2.var = wv; fw2.count = wc; }
  else                                                           { fw2.var = 0.0f; fw2.count = 0; }
}
// Scan/position for the tempcomp pair — twin of ee_scan (same NOR first-use + skip-to-blank rules).
static void ee2_scan(_Bool unpack){
  uint8_t rec[EE_REC_SZ], best[EE_REC_SZ];
  int found=0; uint32_t best_gen=0, best_page=ee2_page_a; uint16_t best_slot=0;
  ee2_active=ee2_page_a; ee2_next=0;
  for (int pg=0; pg<2; pg++){
    uint32_t base = pg? ee2_page_b : ee2_page_a;
    for (uint16_t s=0; s<EE_SLOTS; s++){
      uint32_t addr = base + (uint32_t)s*EE_REC_SZ;
      if (ee_rd32(addr) != EE2_MAGIC) continue;
      ee_rd(addr, rec, EE_REC_SZ);
      uint16_t sc; memcpy(&sc,rec+8,2); if (sc!=EE2_SCHEMA) continue;
      uint16_t crc; memcpy(&crc,rec+62,2); if (ee_crc16(rec,62)!=crc) continue;
      uint32_t gen; memcpy(&gen,rec+4,4);
      if (!found || gen>best_gen){ found=1; best_gen=gen; best_page=base; best_slot=s; memcpy(best,rec,EE_REC_SZ); }
    }
  }
  if (found){
    if (unpack) tc_unpack(best);
    ee2_active=best_page; ee2_next=(uint16_t)(best_slot+1);
    if (best_gen > ee2_gen) ee2_gen = best_gen;
  }
  if (ee_fault_check()) return;      // same rule as ee_scan: a failed read never licenses an erase
  if (!found && ee_backing == EE_BK_QSPI){
    _Bool blank = ee_page_blank(ee2_page_a) && ee_page_blank(ee2_page_b);
    if (ee_fault_check()) return;
    if (!blank){
      ee_erase(ee2_page_a); ee_erase(ee2_page_b);
      ee2_active=ee2_page_a; ee2_next=0;
    }
  }
  while (ee2_next < EE_SLOTS && ee_rd32(ee2_active + (uint32_t)ee2_next*EE_REC_SZ) != 0xFFFFFFFFu) ee2_next++;
  ee_fault_check();
}
void ee2_load(void){
  ee2_init_base();
  tc2.valid=0; ee2_active=ee2_page_a; ee2_next=0; ee2_gen=0;
  fw2.var=0.0f; fw2.count=0;         // the shadow mirrors the store: nothing is held until a record says so
  if (!ee2_avail) return;
  ee2_scan(1);
}
// Never write a zero/invalid record; update the shadow to what we just wrote (rebaselines the detector).
static _Bool ee2_commit(_Bool with_live){
  if (!ee2_avail) return 0;
  if (!(with_live && (tc_hse_valid || tc_lse_valid)) && fw_count < FW_MIN) return 0;   // nothing worth a record
  if (!settings_mapping_ok()) return 0;
  uint8_t rec[EE_REC_SZ];
  fatfs_busy = 1;
  if (!ee_sentinel(EE2_MAGIC, ee2_page_a, ee2_page_b, &ee2_active, &ee2_next)){ fatfs_busy = 0; return 0; }
  while (ee2_next < EE_SLOTS && ee_rd32(ee2_active + (uint32_t)ee2_next*EE_REC_SZ) != 0xFFFFFFFFu) ee2_next++;
  if (ee_fault_check()){ fatfs_busy = 0; return 0; }
  if (ee2_next >= EE_SLOTS){
    uint32_t other = (ee2_active==ee2_page_a)? ee2_page_b : ee2_page_a;
    ee_erase(other); ee2_active=other; ee2_next=0;
  }
  uint32_t addr = ee2_active + (uint32_t)ee2_next*EE_REC_SZ;
  tc_pack(rec, ee2_gen+1, with_live);
  for (uint32_t o=0;o<EE_REC_SZ;o+=8){ uint64_t dw; memcpy(&dw, rec+o, 8); ee_prog_dw(addr+o, dw); }
  ee2_gen++; ee2_next++;
  fatfs_busy = 0;
  tc_unpack(rec);          // shadow := just-persisted model
  return 1;
}
// PRE-MORTEM FIX (load sanity): reject a retained model with non-finite or physically-implausible
// coefficients before it can ever seed the timebase. Bounds are generous — they catch corruption, not
// legitimate fits (a real crystal's tempco is far smaller than these).
static _Bool tc2_sane(void){
  if (tc2.lo < -50 || tc2.hi > 100 || tc2.hi <= tc2.lo) return 0;
  if (tc2.t0 < -40 || tc2.t0 > 85) return 0;
  if (tc2.hse_valid){
    if (!isfinite(tc2.hse_b) || !isfinite(tc2.hse_c)) return 0;
    if (fabsf(tc2.hse_b) > 10.0f || fabsf(tc2.hse_c) > 2.0f) return 0;      // ppm/°C, ppm/°C²
  }
  if (tc2.lse_valid){
    if (!isfinite(tc2.lse_a) || !isfinite(tc2.lse_b) || !isfinite(tc2.lse_c)) return 0;
    if (fabsf(tc2.lse_a) > 200.0f || fabsf(tc2.lse_b) > 20.0f || fabsf(tc2.lse_c) > 5.0f) return 0;
  }
  return tc2.hse_valid || tc2.lse_valid;
}
// Boot: seed the learned model from flash THROUGH the existing tc_seed path (config.txt still wins).
void tc_seed_from_flash(void){
  if (!tc_persist) return;                                   // opt-in (default off)
  if (tc_seed_done) return;                                  // first boot pass only (live-reload = no-op)
  if (!ee2_avail || !tc2.valid) return;
  if (!tc2_sane()) { tc2.valid = 0; return; }                // reject implausible/corrupt model
  _Bool cfg_froze = !isnan(tc_cfg_hse[1]) || !isnan(tc_cfg_hse[2]) ||
                    !isnan(tc_cfg_lse[0]) || !isnan(tc_cfg_lse[1]) || !isnan(tc_cfg_lse[2]);
  if ((cfg_tc_defined & CFG_TC_SEED) && !tc_seed) return;    // tc_seed = off in config -> ignore flash
  if (cfg_froze && !tc_seed) return;                         // frozen config coeffs, no evolve -> config wins
  if (isnan(tc_cfg_hse[1]) && isnan(tc_cfg_hse[2]) && tc2.hse_valid){ tc_cfg_hse[1]=tc2.hse_b; tc_cfg_hse[2]=tc2.hse_c; }
  if (isnan(tc_cfg_lse[0]) && isnan(tc_cfg_lse[1]) && isnan(tc_cfg_lse[2]) && tc2.lse_valid){
    tc_cfg_lse[0]=tc2.lse_a; tc_cfg_lse[1]=tc2.lse_b; tc_cfg_lse[2]=tc2.lse_c; }
  if (!(cfg_tc_defined & CFG_TC_LO)) tc_seed_lo = tc2.lo;
  if (!(cfg_tc_defined & CFG_TC_HI)) tc_seed_hi = tc2.hi;
  if (!(cfg_tc_defined & CFG_TC_T0)) tc_t0 = tc2.t0;
  tc_seed = 1;
  tc_persist_seeded = 1;
}
// Runs immediately AFTER tc_seed_apply on the boot pass. PRE-MORTEM FIX (break the permanent-hold trap):
// cap the seeded prior order at 2 so the first genuine linear fit can supersede the seed — a stored
// quadratic can never lock the model for the life of a thermally-narrow deployment. Also restore the
// real sample counts + residual for display + holdover-fade honesty, and stamp the commit clock so the
// clock does not immediately rewrite the record it just loaded.
void tc_persist_after_seed(void){
  if (!tc_persist_seeded) return;
  tc_persist_seeded = 0;      // readConfigFile ends with this tail on every reload, not just at boot
  if (tc_hse_prior > 2) tc_hse_prior = 2;
  if (tc_lse_prior > 2) tc_lse_prior = 2;
  if (tc2.n_hse) tc_n_hse = tc2.n_hse;
  if (tc2.n_lse) tc_n_lse = tc2.n_lse;
  if (tc2.hse_valid) tc_hse_resid = tc2.hse_resid;
  if (tc2.lse_valid) tc_lse_resid = tc2.lse_resid;
  tc_last_commit_ms = uwTick;
}
// PRE-MORTEM FIX (persist gate): only a WELL-SUPPORTED model may reach flash — enough samples, real
// temperature coverage, and a believable fit residual. A shaky fit is never persisted, so it can never
// auto-seed a bad steer on the next boot.
static _Bool tc_model_supported(void){
  _Bool hse_ok = tc_hse_valid && tc_n_hse >= 300u && (tc_hse_tmax - tc_hse_tmin) >= 4
                 && tc_hse_resid < 5.0f*(float)tc_tpp();
  _Bool lse_ok = tc_lse_valid && tc_n_lse >= 60u  && (tc_lse_tmax - tc_lse_tmin) >= 4
                 && tc_lse_resid < 5.0f;
  return hse_ok || lse_ok;
}
// Change detector: set dirty when the live model has moved meaningfully vs the last-persisted shadow.
// Called after tc_fit(). "Meaningful" = validity gained, coverage expanded, or the modelled ppm at
// {lo, mid, hi} moved beyond a threshold (handles the quadratic term correctly).
static void tc_model_check(void){
  if (!tc_persist || !ee2_avail || tc_model_dirty) return;
  float tpp=(float)tc_tpp(); if (tpp<=0.0f) tpp=80.0f;
  if ((tc_hse_valid && !tc2.hse_valid) || (tc_lse_valid && !tc2.lse_valid)) { tc_model_dirty=1; return; }
  if (tc_hse_valid && (tc_hse_tmin < tc2.lo || tc_hse_tmax > tc2.hi)) { tc_model_dirty=1; return; }
  if (tc_lse_valid && (tc_lse_tmin < tc2.lo || tc_lse_tmax > tc2.hi)) { tc_model_dirty=1; return; }
  if (tc_hse_valid && tc2.hse_valid){
    int16_t T[3]={tc_hse_tmin,(int16_t)((tc_hse_tmin+tc_hse_tmax)/2),tc_hse_tmax};
    for (int i=0;i<3;i++){ float x=(float)(T[i]-tc_t0);
      float d=fabsf(((tc_hse_m[1]/tpp)*x+(tc_hse_m[2]/tpp)*x*x) - (tc2.hse_b*x+tc2.hse_c*x*x));
      if (d>0.5f){ tc_model_dirty=1; return; } }
  }
  if (tc_lse_valid && tc2.lse_valid){
    int16_t T[3]={tc_lse_tmin,(int16_t)((tc_lse_tmin+tc_lse_tmax)/2),tc_lse_tmax};
    for (int i=0;i<3;i++){ float x=(float)(T[i]-tc_t0);
      float d=fabsf((tc_lse_m[0]+tc_lse_m[1]*x+tc_lse_m[2]*x*x) - (tc2.lse_a+tc2.lse_b*x+tc2.lse_c*x*x));
      if (d>0.2f){ tc_model_dirty=1; return; } }
  }
}
// Main-loop: persist the model when dirty AND well-supported, throttled to >=30 min on a MONOTONIC
// clock (uwTick, never GPS wall time), on the menu commit's PPS-safe / UART-idle / L0 gate.
// The learned wander rides in the same record. It is worth a write when the level the bound USES has
// moved: never stored before, or changed by a quarter either way. Both sides are taken after the
// floor, so a quiet clock, whose learned level sits under the floor, is written once and then left.
static _Bool fw_dirty = 0;
static void fw_persist_check(void){
  if (!fw_new_pair) return;
  fw_new_pair = 0;
  if (!tc_persist || !ee2_avail || fw_count < FW_MIN) return;
  if (fw2.count < FW_MIN) { fw_dirty = 1; return; }
  float live = fade_wander_var();
  float kept = fw2.var > FADE_A_FLOOR ? fw2.var : FADE_A_FLOOR;
  if (live > 1.25f * kept || live < 0.8f * kept) fw_dirty = 1;
}
// Boot: a stored wander becomes the live one, so the bound does not spend 2.3 h on its default (which
// may be too TIGHT for a clock that had learned it wanders more). Once per boot, never over live data.
static _Bool fw_restored = 0;
void fw_restore_from_flash(void){
  if (fw_restored) return;
  fw_restored = 1;
  if (!tc_persist || !ee2_avail || fw_count || fw2.count < FW_MIN) return;
  fw_var = fw2.var; fw_count = fw2.count;
}
static void tc_persist_step(void){
  fw_persist_check();
  _Bool model = tc_model_dirty && tc_model_supported();     // only a well-supported model reaches flash
  if ((model || fw_dirty) && tc_persist && ee2_avail &&
      menu_layer==L0_CLOCK && !waitingForLatch &&
      huart2.gState==HAL_UART_STATE_READY && !pps_emit_pending() && !tc_dump_pending &&
      (uint32_t)(uwTick - tc_last_commit_ms) >= 1800000u){     // >= 30 min, wrap-safe
    if (ee2_commit(model)){ if (model) tc_model_dirty = 0; fw_dirty = 0; tc_last_commit_ms = uwTick; }
  }
}
// "tc_forget = on" over serial: erase the tempcomp store (drop the PERSISTED copy only; the live
// learned model is untouched — a full cold start is tc_reset + tc_forget). Serviced from the main loop.
static void tc_forget_step(void){
  if (!tc_forget_pending) return;
  if (ee2_avail && !settings_mapping_ok()) return;   // QSPI: never erase against a stale mapping; retry next pass
  tc_forget_pending = 0;
  tc2.valid = 0; tc_model_dirty = 0;
  fw2.var = 0.0f; fw2.count = 0; fw_dirty = 0;       // the stored wander goes with it
  if (!ee2_avail) return;
  fatfs_busy = 1;
  ee_erase(ee2_page_a); ee_erase(ee2_page_b);
  fatfs_busy = 0;
  ee2_active = ee2_page_a; ee2_next = 0; ee2_gen = 0;
}

// Merge the stored menu choices into the settings config.txt has just produced. Per setting:
//   - config.txt does not mention it: the menu's choice applies.
//   - config.txt mentions it, and its line is the same as at the last merge: the menu's choice still
//     applies. Saving the file for some other reason does not undo it.
//   - config.txt mentions it, and its line is new or has changed since the last merge: the file wins,
//     and the menu's choice is deleted from the store. It does not come back if the line is removed
//     later.
// A line that is removed never beats the menu: the file has stopped having an opinion.
//
// "The last merge" is cfg_prev_*: in RAM while the clock runs, and read back from the store at boot,
// so it does not matter whether the clock was running, or had caught up, when the file was changed.
// A store with no file record (an older build's, or a write cut short) falls back on the stamp, the
// modification time of the config.txt it was merged with: if the file still carries it, nothing has
// changed and every choice stands; if not, which lines changed is unknown and the file wins every
// setting it mentions, which is the rule this one replaced.
//
// have_file = 0: there is no config.txt to read. The stored choices apply and the memory of the last
// real file is kept, so a file that reappears (an editor that saves by delete and rename) is compared
// with the file before it, not with nothing.
static void menu_reconcile(_Bool have_file){
  _Bool stamp_ok = (ovr.stamp_fdate || ovr.stamp_ftime) &&
                   ovr.stamp_fdate==config.fdate && ovr.stamp_ftime==config.ftime;
  uint64_t now_modes_val = 0;          // what the file says for each mode it mentions: the parser has just
  for (uint8_t m=0;m<NUM_DISPLAY_MODES;m++)   // run, and nothing stored has been applied yet
    if ((cfg_modes_defined&(1ull<<m)) && config.modes_enabled[m]) now_modes_val |= (1ull<<m);
  uint16_t now_sig[16];
  for (uint8_t k=0;k<16;k++) now_sig[k] = (uint16_t)(cfg_sig[k] ^ (cfg_sig[k]>>16));

  if (!cfg_prev_known && have_file) {                  // no memory of a merge: the stamp decides
    if (ovr.valid && stamp_ok) {                       // the file the store was last merged with: nothing has changed
      cfg_prev_simple = cfg_simple_defined; cfg_prev_modes = cfg_modes_defined; cfg_prev_modes_val = now_modes_val;
      memcpy(cfg_prev_sig, now_sig, sizeof cfg_prev_sig);
    } else {                                           // unknown history: every line counts as new
      cfg_prev_simple = 0; cfg_prev_modes = 0; cfg_prev_modes_val = 0;
      memset(cfg_prev_sig, 0, sizeof cfg_prev_sig);
    }
    cfg_prev_known = 1;
  }
  if (ovr.valid) {
    if (have_file) {
      uint16_t changed_s = (uint16_t)~cfg_prev_simple;         // lines that were not there before...
      for (uint8_t k=0;k<16;k++) if (now_sig[k] != cfg_prev_sig[k]) changed_s |= (uint16_t)(1u<<k);   // ...or read differently
      uint16_t beat_s = ovr.simple_mask & cfg_simple_defined & changed_s;
      uint64_t beat_m = ovr.modes_mask & cfg_modes_defined & (~cfg_prev_modes | (now_modes_val ^ cfg_prev_modes_val));
      // TEMPCOMP and BALANCE each cover several config.txt keys. The file may have taken over only one
      // of them; the others still hold what the menu's choice set, and nothing owns that any more.
      // They go back to their defaults, which is where a power cycle would put them.
      if (beat_s & (1u<<KID_TEMPCOMP)) { cfg_release("tc_learn"); cfg_release("tc_apply"); cfg_release("tc_persist"); }
      if (beat_s & (1u<<KID_BALANCE))  { cfg_release("seg_balance"); cfg_release("colon_balance"); }
      ovr.simple_mask &= (uint16_t)~beat_s;
      ovr.modes_mask &= ~beat_m; ovr.modes_val &= ~beat_m;
      // What must reach the store: a deleted choice; and, while any choice is stored, the file this
      // merge was made against (its statements, and its stamp for the fallback).
      _Bool moved = cfg_prev_simple != cfg_simple_defined || cfg_prev_modes != cfg_modes_defined ||
                    cfg_prev_modes_val != now_modes_val || memcmp(cfg_prev_sig, now_sig, sizeof now_sig) != 0;
      _Bool holds = ovr.simple_mask || ovr.modes_mask;
      _Bool mtime = config.fdate || config.ftime;
      if (beat_s || beat_m || (holds && (moved || (mtime && !stamp_ok) || (ee_avail && !ee_ext_stored)))) {
        if (mtime) { ovr.stamp_fdate = config.fdate; ovr.stamp_ftime = config.ftime; }
        menu_dirty = 1;
      }
    }
    #define OVR_S(kid, apply) if (ovr.simple_mask&(1u<<(kid))) { apply; }
    OVR_S(KID_BRIGHTNESS, config.brightness_override=(float)ovr.brightness)
    OVR_S(KID_COLON,      colonModeCivil=ovr.colon)
    OVR_S(KID_COLON_ALT,  { colonModeAlt=ovr.alt_colon; colonAltExplicit=1; })
    OVR_S(KID_PAGE_MS,    config.page_ms=ovr.page_ms)
    OVR_S(KID_SIG_FADE,   significance_fade=ovr.sig_fade)
    OVR_S(KID_PPS,        pps_ts_enabled=ovr.pps)
    OVR_S(KID_BRIT,       brightness_report=ovr.brit?1:0)
    OVR_S(KID_FREQ_HOLD,  freq_hold=ovr.fh?1:0)
    OVR_S(KID_WATCHDOG,   watchdog=ovr.wdt?1:0)   // on arms it on the next main-loop pass; off waits for the next reset
    OVR_S(KID_NMEA,       nmea_cdc_level=ovr.nmea)
    OVR_S(KID_MATRIX_FREQ,setDisplayFreq(ovr.matrix_freq))   // clamping setter (never ARR-direct) -> a bad stored value can't brick
    OVR_S(KID_TEMPCOMP,   tc_learn=tc_apply=tc_persist=ovr.tc?1:0)
    OVR_S(KID_BALANCE,    { if(ovr.bal){ if(!seg_balance)seg_balance=1; if(!colon_balance)colon_balance=1; } else seg_balance=colon_balance=0; colonForce=1; })  // config parse ran first: a stored "on" must not clobber a manual strength
    OVR_S(KID_CUCKOO,     { uint8_t pv=cuckoo; cuckoo=(ovr.cuckoo<CK_PIECES)?ovr.cuckoo:CK_OFF; if(cuckoo!=pv) cuckoo_abort(); })  // range-clamped: a corrupt store must not select a ghost piece
    #undef OVR_S
    for (uint8_t m=0;m<NUM_DISPLAY_MODES;m++)
      if (ovr.modes_mask&(1ull<<m))
        config.modes_enabled[m] = (ovr.modes_val>>m)&1ull;   // postConfigCleanup's >=1 guard backstops this
  }
  if (have_file) {                                     // this file is "the last merge" for the next load
    cfg_prev_simple = cfg_simple_defined; cfg_prev_modes = cfg_modes_defined; cfg_prev_modes_val = now_modes_val;
    memcpy(cfg_prev_sig, now_sig, sizeof cfg_prev_sig);
  }
}
void menu_apply_overrides(void){ menu_reconcile(1); }
// Record a menu edit into the override store (value already applied live). The stamp is the
// config.txt this store is merged with (see menu_reconcile): the file now loaded.
static void menu_record_key(uint8_t key_id, int32_t v){
  ovr.stamp_fdate=config.fdate; ovr.stamp_ftime=config.ftime; ovr.valid=1; menu_dirty=1;
  if (key_id>=KID_MODE_BASE){
    uint8_t m=(uint8_t)(key_id-KID_MODE_BASE);
    ovr.modes_mask|=(1ull<<m); if(config.modes_enabled[m]) ovr.modes_val|=(1ull<<m); else ovr.modes_val&=~(1ull<<m);
    if (m==MODE_FIRMWARE_CRC_T){    // the FW-CRC row drives both display slots; persist both
      ovr.modes_mask|=(1ull<<MODE_FIRMWARE_CRC_D);
      if(config.modes_enabled[MODE_FIRMWARE_CRC_D]) ovr.modes_val|=(1ull<<MODE_FIRMWARE_CRC_D); else ovr.modes_val&=~(1ull<<MODE_FIRMWARE_CRC_D);
    }
    return;
  }
  ovr.simple_mask |= (uint16_t)(1u<<key_id);
  switch(key_id){
    case KID_BRIGHTNESS: ovr.brightness=(int16_t)v; break;
    case KID_COLON:      ovr.colon=(uint8_t)v; break;
    case KID_COLON_ALT:  ovr.alt_colon=(uint8_t)v; break;
    case KID_PAGE_MS:    ovr.page_ms=(uint16_t)v; break;
    case KID_SIG_FADE:   ovr.sig_fade=(uint8_t)v; break;
    case KID_PPS:        ovr.pps=(uint8_t)v; break;
    case KID_BRIT:       ovr.brit=(uint8_t)(v?1:0); break;
    case KID_FREQ_HOLD:  ovr.fh=(uint8_t)(v?1:0); break;
    case KID_WATCHDOG:   ovr.wdt=(uint8_t)(v?1:0); break;
    case KID_NMEA:       ovr.nmea=(uint8_t)v; break;
    case KID_MATRIX_FREQ:ovr.matrix_freq=(uint32_t)v; break;
    case KID_TEMPCOMP:   ovr.tc=(uint8_t)(v?1:0); break;
    case KID_BALANCE:    ovr.bal=(uint8_t)(v?1:0); break;
    case KID_CUCKOO:     ovr.cuckoo=(uint8_t)v; break;
  }
}

// ---- The menu's choices as config.txt lines, over serial ---------------------------------------
// After every saved change, and on "menu_export = on", the clock sends what the store holds in the
// words config.txt uses, one setting a line, so a host can write those lines into config.txt without
// knowing anything about the store:
//   $PMCFG,<gen>,<n>,<key>,<value>*CC      n counts from 1
//   $PMCFG,<gen>,0,END,<count>*CC          closes the set; <count> lines came before it
// An empty <value> means "config.txt should not have this line" (brightness on AUTO has no value to
// write). When the host does write them, each line is new or changed, so config.txt takes the
// setting back and the stored choice is deleted (menu_reconcile): the file becomes the one record.
// A setting made of several keys sends each of them. BALANCE sends the live strengths, because its
// "on" keeps a hand-set strength that config.txt gave.
// Same CDC contract as the dumps: one line a pass, a busy endpoint is retried, no host means no send.
static const char *const colon_key[] = { "slowfade", "heartbeat", "sawtooth", "alt_sawtooth", "toggle", "solid" };
static const char *const mode_key[NUM_DISPLAY_MODES] = {
  [MODE_ISO8601_STD]="MODE_ISO8601_STD", [MODE_ISO_ORDINAL]="MODE_ISO_ORDINAL", [MODE_ISO_WEEK]="MODE_ISO_WEEK",
  [MODE_UNIX]="MODE_UNIX", [MODE_JULIAN_DATE]="MODE_JULIAN_DATE", [MODE_MODIFIED_JD]="MODE_MODIFIED_JD",
  [MODE_SHOW_OFFSET]="MODE_SHOW_OFFSET", [MODE_SHOW_TZ_NAME]="MODE_SHOW_TZ_NAME", [MODE_WEEKDAY]="MODE_WEEKDAY",
  [MODE_WEEKDA_DD]="MODE_WEEKDA_DD", [MODE_WDY_MM_DD]="MODE_WDY_MM_DD", [MODE_STANDBY]="MODE_STANDBY",
  [MODE_COUNTDOWN]="MODE_COUNTDOWN", [MODE_SATVIEW]="MODE_SATVIEW", [MODE_DEBUG_BRIGHTNESS]="MODE_DEBUG_BRIGHTNESS",
  [MODE_DEBUG_RTC]="MODE_DEBUG_RTC", [MODE_TEXT]="MODE_TEXT", [MODE_FIRMWARE_CRC_T]="MODE_FIRMWARE_CRC",
  [MODE_VBAT]="MODE_VBAT", [MODE_DISPLAYTEST]="MODE_DISPLAYTEST", [MODE_TTFF]="MODE_TTFF",
#ifdef NONCOMPLIANT_DATE_MODES
  [MODE_DDMMYYYY]="MODE_DDMMYYYY",
#endif
  [MODE_SUN]="MODE_SUN", [MODE_SUN_AZEL]="MODE_SUN_AZEL", [MODE_MOON]="MODE_MOON", [MODE_GRID]="MODE_GRID",
  [MODE_LATLON]="MODE_LATLON", [MODE_DARK]="MODE_DARK", [MODE_TEMPCOMP]="MODE_TEMPCOMP", [MODE_LST]="MODE_LST",
  [MODE_SOLAR]="MODE_SOLAR", [MODE_ADEV]="MODE_ADEV", [MODE_STAR]="MODE_STAR", [MODE_ZONE2]="MODE_ZONE2",
};   // MODE_FIRMWARE_CRC_D has no key of its own: the one config.txt key sets both slots
#define MX_SIMPLE 17u                    // the lines before the modes
// Line `slot` of the export: 0 = the store does not hold it. val is at least 12 bytes.
static const char *menu_export_item(uint16_t slot, char *val){
  static const struct { uint8_t kid; const char *key; } sk[MX_SIMPLE] = {
    {KID_BRIGHTNESS,"brightness"}, {KID_COLON,"colon_mode"}, {KID_COLON_ALT,"colon_alt_mode"}, {KID_PAGE_MS,"page_ms"},
    {KID_SIG_FADE,"significance_fade"}, {KID_PPS,"pps"}, {KID_NMEA,"nmea"}, {KID_MATRIX_FREQ,"MATRIX_FREQUENCY"},
    {KID_TEMPCOMP,"tc_learn"}, {KID_TEMPCOMP,"tc_apply"}, {KID_TEMPCOMP,"tc_persist"},
    {KID_BALANCE,"seg_balance"}, {KID_BALANCE,"colon_balance"}, {KID_CUCKOO,"cuckoo"},
    {KID_BRIT,"brightness_report"}, {KID_FREQ_HOLD,"freq_hold"}, {KID_WATCHDOG,"watchdog"},
  };
  long num = -1; const char *word = NULL;
  if (slot >= MX_SIMPLE) {
    uint16_t m = (uint16_t)(slot - MX_SIMPLE);
    if (m >= NUM_DISPLAY_MODES || !mode_key[m] || !(ovr.modes_mask & (1ull<<m))) return NULL;
    strcpy(val, ((ovr.modes_val>>m)&1ull) ? "enabled" : "disabled");
    return mode_key[m];
  }
  if (!(ovr.simple_mask & (1u<<sk[slot].kid))) return NULL;
  switch (slot) {
    case 0:  if (ovr.brightness >= 0) num = 4095 - ovr.brightness; else word = ""; break;   // the key is read inverted; AUTO is "no line"
    case 1:  word = colon_key[ovr.colon     < 6 ? ovr.colon     : 0]; break;
    case 2:  word = colon_key[ovr.alt_colon < 6 ? ovr.alt_colon : 0]; break;
    case 3:  num = ovr.page_ms; break;
    case 4:  word = ovr.sig_fade ? "on" : "off"; break;
    case 5:  word = ovr.pps ? "on" : "off"; break;
    case 6:  word = ovr.nmea == NMEA_NONE ? "off" : (ovr.nmea == NMEA_RMC ? "rmc" : "all"); break;
    case 7:  num = (long)ovr.matrix_freq; break;
    case 8: case 9: case 10: word = ovr.tc ? "on" : "off"; break;
    case 11: if (seg_balance   > 1) num = seg_balance;   else word = seg_balance   ? "on" : "off"; break;
    case 12: if (colon_balance > 1) num = colon_balance; else word = colon_balance ? "on" : "off"; break;
    case 13: word = ovr.cuckoo == CK_TRUST ? "trust" : "off"; break;
    case 14: word = ovr.brit ? "on" : "off"; break;
    case 15: word = ovr.fh   ? "on" : "off"; break;
    default: word = ovr.wdt  ? "on" : "off"; break;
  }
  if (word) strcpy(val, word); else snprintf(val, 12, "%ld", num);
  return sk[slot].key;
}
static void menu_export_step(void){
  static uint16_t slot = 0, sent = 0;
  static uint32_t gen = 0;
  static int      dn = -1;                // a formatted line is waiting to go
  static uint16_t busy_ct = 0;
  static char     dline[96];
  if (!menu_export_pending) return;
  if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) { menu_export_pending = 0; dn = -1; slot = 0; sent = 0; return; }
  if (dn < 0) {
    if (slot == 0 && sent == 0) gen = ee_gen;             // one set describes one state of the store
    char body[80], val[12];
    const char *key = NULL;
    while (slot < MX_SIMPLE + NUM_DISPLAY_MODES && !(key = menu_export_item(slot, val))) slot++;
    int n;
    if (key) n = snprintf(body, sizeof body, "PMCFG,%lu,%u,%s,%s", (unsigned long)gen, (unsigned)(sent + 1u), key, val);
    else     n = snprintf(body, sizeof body, "PMCFG,%lu,0,END,%u", (unsigned long)gen, (unsigned)sent);
    if (n <= 0 || n >= (int)sizeof body) { menu_export_pending = 0; slot = 0; sent = 0; return; }
    uint8_t cks = 0; for (int i = 0; i < n; i++) cks ^= (uint8_t)body[i];
    dn = snprintf(dline, sizeof dline, "$%s*%02X\r\n", body, (unsigned)cks);
    busy_ct = 0;
    if (key) { slot++; sent++; } else slot = 0xFFFF;      // 0xFFFF: the END line is the one waiting
  }
  __disable_irq();
  uint8_t r = CDC_Copy_Transmit((uint8_t*)dline, (uint16_t)dn);
  __enable_irq();
  if (r == USBD_BUSY) { if (++busy_ct > 5000) { menu_export_pending = 0; dn = -1; slot = 0; sent = 0; } return; }
  dn = -1;
  if (slot == 0xFFFF) {                   // the set is complete...
    slot = 0; sent = 0;
    if (ee_gen == gen) menu_export_pending = 0;           // ...unless the store was saved again while it went out: send the new one
  }
}

// "factory_reset = on" over serial: factory-reset the on-device menu. Erase both emulated-EEPROM pages,
// forget the RAM override store, then re-read config.txt so the clock returns to a config.txt-only
// state immediately (no reboot). Serial-only + origin-guarded, like tc_reset. Serviced from the main
// loop (flash erase stalls the CPU, and the config re-read touches the FAT + non-reentrant sendDate).
static void factory_reset_step(void){
  if (!factory_reset_pending) return;
  if (ee_avail && !settings_mapping_ok()) return;   // QSPI: never erase against a stale mapping; retry
  factory_reset_pending = 0;
  memset(&ovr, 0, sizeof ovr);          // drop the RAM override store (also covers the no-SETTINGS.BIN case)
  menu_dirty = 0;
  if (ee_avail){
    fatfs_busy = 1;
    ee_erase(ee_page_a);
    ee_erase(ee_page_b);
    fatfs_busy = 0;
    ee_active = ee_page_a; ee_next = 0; ee_gen = 0;
  }
  // Factory means EVERYTHING learned goes too: wipe the retained tempcomp model (store) and the live
  // learned state. The config.txt re-read below then re-seeds from the file's tc_* block — the clock
  // returns to exactly what the file says, compensation included.
  tc_forget_pending = 1;
  tc_reset_pending  = 1;
  delayedReadConfigFile = 1;             // re-apply config.txt with no overrides -> back to factory
  if (menu_layer != L0_CLOCK) menu_flash("DONE");   // a destructive action must not return to an identical screen
}
