// main_parts/config_write.h — the clock writes its menu choices into config.txt itself.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

// ==================== config.txt written by the clock (opt-in: config_write = on) ================
// A setting changed with the buttons lives in the store, where config.txt cannot show it. With a
// computer attached, pccd writes the choice into the file ($PMCFG). This is for a clock that is not:
// it writes the lines itself, and config.txt then takes each setting back by the ordinary rule
// (menu_reconcile), so the file is the one record.
//
// WHEN. Only if no computer has enumerated since power-on. A computer that has the drive mounted
// keeps its own copy of the file and of the directory; it would not see a change made underneath it,
// and could later write its old copy back, after the store had already let the choice go.
//
// HOW. There is no file-system write code. The firmware edits config.txt's own flash sectors, each
// rewrite going through the journal in the store (ee_j_begin), so a power cut loses nothing.
//   - A value that fits where the old one stands is written there, padded with spaces.
//   - Otherwise a new line goes at the end of the text, and the old line is blanked afterwards. The
//     parser takes the last line for a key, so the file reads correctly at every step between.
//   - A setting made of several keys (TEMPCOMP, BALANCE) is one block of lines in ONE rewrite. Written
//     a line at a time, a power cut after the first would leave the file claiming the setting with
//     the other keys at their defaults, and config.txt would take that back as the truth.
//   - Brightness on AUTO is written as "brightness = -1", which the parser reads as AUTO. Blanking
//     its line instead could not be done in one step if the file has the key twice.
//   - The allocation table is never touched. The file can only grow into the unused part of its last
//     cluster (16 KB on the clock's drive: four 4 KB sectors), a sector at a time: the gap up to the
//     end of the sector is filled with spaces, which later lines replace, and the size is set to
//     match. That is the only write to the directory, at most once for each 4 KB the file gains.
//   - A line records it: "# synced 2026-11-12 08:45 by the clock", rewritten in place afterwards.
// A setting that cannot be written (no room left) stays in the store, as before.
//
// WHERE. cw_locate finds the physical sectors: the file's clusters through FATFS, and the directory
// entry by reading the root directory itself. It refuses unless the entry agrees with the file on
// size and first cluster, and every sector reads back the bytes FATFS returns for that part of the
// file. A wrong address here would write over something else, so nothing is taken on trust.
#ifndef SMALL_CODE
#define SMALL_CODE                          /* the locator built by itself (test/config_locate_test.c) */
#endif
#define CW_MAXS 8u                          // a config.txt whose clusters come to 32 KB at most
#define CW_MARK "# synced "
#define CW_MARK_LEN 38u                     // "# synced YYYY-MM-DD HH:MM by the clock"
static uint8_t  cw_buf[EE_QSPI_PGSZ];       // the sector being read, or the image being written
static uint32_t cw_sect[CW_MAXS];           // where each 4 KB of the file's clusters is on the chip, in file order
static uint8_t  cw_nsect;                   // ...all of them: the file's own sectors, then the unused rest of its last cluster
static uint32_t cw_size, cw_dir_addr;
static uint16_t cw_dir_off;
static _Bool    cw_host_seen = 0;           // a computer has enumerated since power-on: never write
enum { CW_IDLE=0, CW_ITEMS, CW_MARKER, CW_RELOAD };
static struct {
  uint8_t  st, started, changed, grow;      // grow: 1 = the gap is filled, the directory is next
  uint8_t  wait, full, ops;                 // a rewrite was begun; the store's page has no slot left; rewrites this pass
  uint32_t grow_to;                         // the size the file is growing to
  uint16_t slot;
  uint32_t seen_gen;
  uint32_t bfrom[3], bto[3];                // old lines still to be blanked, now that new ones stand after them
  uint8_t  nb;
} cw;

static uint32_t cw_le32(const uint8_t *p){ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }

static SMALL_CODE _Bool cw_locate(void){
  uint32_t sclust, dir0; uint16_t dirsecs;
#ifdef __EMSCRIPTEN__
  if (!emu_disk_attached || !emu_cw_n) return 0;
  sclust = 2u + emu_cw_chain[0]; dir0 = EMU_DISK_BASE; dirsecs = 1;
  cw_size = 0;                                              // the directory entry gives it, below
#else
  FIL f;
  uint32_t csz = USERFatFS.csize;                           // sectors a cluster: 4 on the clock's drive
  if (!csz || (USERFatFS.fs_type != FS_FAT12 && USERFatFS.fs_type != FS_FAT16)) return 0;
  if (f_open(&f, CONFIG_FILENAME, FA_READ) != FR_OK) return 0;
  cw_size = f_size(&f);
  uint32_t nd = (cw_size + EE_QSPI_PGSZ - 1u) / EE_QSPI_PGSZ;                // the sectors the file's bytes are in
  sclust = f.obj.sclust;
  _Bool ok = cw_size && nd <= CW_MAXS;
  for (uint32_t i = 0; ok && i < nd; i++) {
    uint8_t a[16], b[16]; unsigned int rc = 0;
    if (f_lseek(&f, (FSIZE_t)i * EE_QSPI_PGSZ) != FR_OK || f_read(&f, a, sizeof a, &rc) != FR_OK || !rc) { ok = 0; break; }
    // After the read, f.clust is the cluster this part of the file is in. Sector i of the file is
    // sector (i mod csz) of that cluster: clust2sect, as in settings_resolve, plus the offset.
    cw_sect[i] = ((f.clust - 2u) * csz + USERFatFS.database + (i % csz)) * EE_QSPI_PGSZ;
    if (cw_sect[i] + EE_QSPI_PGSZ > EE_FLASH_END) { ok = 0; break; }
    ee_rd(cw_sect[i], b, sizeof b);                         // the chip must hold there what FATFS has just read
    if (ee_rd_fault || memcmp(a, b, rc)) ok = 0;
  }
  f_close(&f);
  if (!ok) { ee_rd_fault = 0; return 0; }
  while (nd % csz && nd < CW_MAXS) { cw_sect[nd] = cw_sect[nd - 1u] + EE_QSPI_PGSZ; nd++; }   // the rest of the last cluster: the file's to grow into
  cw_nsect = (uint8_t)nd;
  dir0 = USERFatFS.dirbase * EE_QSPI_PGSZ;
  dirsecs = (uint16_t)(((uint32_t)USERFatFS.n_rootdir * 32u + EE_QSPI_PGSZ - 1u) / EE_QSPI_PGSZ);
#endif
  for (uint16_t k = 0; k < dirsecs; k++) {
    ee_rd(dir0 + (uint32_t)k * EE_QSPI_PGSZ, cw_buf, EE_QSPI_PGSZ);
    if (ee_rd_fault) { ee_rd_fault = 0; return 0; }
    for (uint16_t o = 0; o < EE_QSPI_PGSZ; o += 32) {
      const uint8_t *e = cw_buf + o;
      if (!e[0]) return 0;                                  // the end of the directory
      if (memcmp(e, "CONFIG  TXT", 11) || (e[11] & 0x18u)) continue;        // not it, or a label or a folder
      if (((uint32_t)e[26] | ((uint32_t)e[27] << 8)) != (sclust & 0xFFFFu)) continue;
#ifdef __EMSCRIPTEN__
      cw_size = cw_le32(e + 28); cw_nsect = emu_cw_n;           // every sector of the file's clusters
      if (!cw_size || cw_size > (uint32_t)cw_nsect * EE_QSPI_PGSZ) return 0;
      for (uint8_t i = 0; i < cw_nsect; i++) cw_sect[i] = EMU_DISK_BASE + EE_QSPI_PGSZ * (1u + emu_cw_chain[i]);
#else
      if (cw_le32(e + 28) != cw_size) continue;
#endif
      cw_dir_addr = dir0 + (uint32_t)k * EE_QSPI_PGSZ; cw_dir_off = o;
      return 1;
    }
  }
  return 0;
}

#ifndef CW_LOCATE_ONLY     /* test/config_locate_test.c builds only what is above, against the real FATFS */
// The last line of the file that sets `key`, read the way readConfigFile reads it: spaces dropped,
// a line that starts with '#' or ';' is a comment, the key ends at '='. With raw = 1 the "key" is
// instead the literal start of a line (the sync comment). Offsets are into the file.
typedef struct { uint8_t found, lead; uint32_t ls, vs, le; char val[32]; } CwSpan;   // line start, after the '=', line end (its line ending excluded); lead: a space follows the '='
static SMALL_CODE void cw_find(const char *key, _Bool raw, CwSpan *sp){
  char k[32], v[32], head[12]; uint8_t kn = 0, vn = 0, hn = 0, col = 0, eq = 0, comment = 0, lead = 0;
  uint32_t ls = 0, vs = 0;
  size_t klen = strlen(key);
  sp->found = 0;
  for (uint32_t off = 0; off <= cw_size; off++) {
    if (off < cw_size && !(off & (EE_QSPI_PGSZ - 1u))) ee_rd(cw_sect[off / EE_QSPI_PGSZ], cw_buf, EE_QSPI_PGSZ);
    char c = off < cw_size ? (char)cw_buf[off & (EE_QSPI_PGSZ - 1u)] : '\n';   // the end of the file ends the last line
    if (c == '\r' || c == '\n') {
      k[kn] = 0; v[vn] = 0;
      _Bool hit = raw ? (hn >= klen && !memcmp(head, key, klen)) : (eq && !comment && !strcasecmp(k, key));
      if (hit && off > ls) { sp->found = 1; sp->lead = lead; sp->ls = ls; sp->vs = vs; sp->le = off; strcpy(sp->val, v); }
      ls = off + 1; kn = vn = hn = col = eq = comment = lead = 0;
      continue;
    }
    if (hn < sizeof head) head[hn++] = c;
    if (eq && off == vs && c == ' ') lead = 1;
    if (c == ' ') continue;
    if (!col && (c == '#' || c == ';')) comment = 1;
    col = 1;
    if (eq) { if (vn < sizeof v - 1) v[vn++] = c; }
    else if (c == '=') { eq = 1; vs = off + 1; }
    else if (kn < sizeof k - 1) k[kn++] = c;
  }
}
// Where the text ends: the file's size, less any spaces at the very end (the gap filled when the file
// grew). *last is the character before that point, 0 for an empty file.
static SMALL_CODE uint32_t cw_text_end(char *last){
  uint32_t off = cw_size;
  *last = 0;
  while (off) {
    if (!(off & (EE_QSPI_PGSZ - 1u)) || off == cw_size) ee_rd(cw_sect[(off - 1u) / EE_QSPI_PGSZ], cw_buf, EE_QSPI_PGSZ);
    *last = (char)cw_buf[(off - 1u) & (EE_QSPI_PGSZ - 1u)];
    if (*last != ' ') break;
    off--;
  }
  return off;
}
// Rewrite part of ONE sector of the file: `n` bytes of text at file offset `off`, then spaces up to
// `fill_to`. Starts the journal; the caller comes back when it has finished.
static SMALL_CODE _Bool cw_patch(uint32_t off, const char *text, uint32_t n, uint32_t fill_to){
  uint32_t s = off / EE_QSPI_PGSZ, o = off & (EE_QSPI_PGSZ - 1u), end = fill_to - s * EE_QSPI_PGSZ;
  if (s >= cw_nsect || end > EE_QSPI_PGSZ || o + n > end) return 0;
  ee_rd(cw_sect[s], cw_buf, EE_QSPI_PGSZ);
  if (ee_rd_fault) { ee_fault_check(); return 0; }
  memcpy(cw_buf + o, text, n);
  memset(cw_buf + o + n, ' ', end - o - n);
  if (!ee_j_begin(cw_sect[s], cw_buf)) { cw.full = (ee_next >= EE_SLOTS); return 0; }
  cw.wait = 1; cw.ops++;
  return 1;
}
// A line to put at the end of the text: at `te`, or at the start of the next sector if it would
// otherwise cross into it (a line is never written in two halves: the first half alone could read
// as a different value). Returns 0 if there is no room inside the file's size.
static SMALL_CODE _Bool cw_append(const char *line, uint32_t n){
  char last, l2[72];
  uint32_t te = cw_text_end(&last);
  if (te && last != '\n') { l2[0] = '\r'; l2[1] = '\n'; memcpy(l2 + 2, line, n); line = l2; n += 2; }   // the text does not end its last line
  if ((te / EE_QSPI_PGSZ) != ((te + n - 1u) / EE_QSPI_PGSZ)) te = (te / EE_QSPI_PGSZ + 1u) * EE_QSPI_PGSZ;
  if (te + n > cw_size) return 0;
  return cw_patch(te, line, n, te + n);
}
static SMALL_CODE void cw_step(void){
  if (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED) cw_host_seen = 1;
  if (jx.st) { ee_j_step(); return; }       // a rewrite that has begun is always finished, one piece a pass
  if (cw.full) {                            // the store's page has no slot for the journal: saving the store swaps pages,
    cw.full = 0; cw.wait = 0; cw.st = CW_IDLE; menu_dirty = 1;   // and that save starts this again
    return;
  }
  if (cw.wait) {                            // a rewrite has just ended
    cw.wait = 0;
    if (!jx.ok || cw.ops > 100u) {          // it did not take (the next boot finishes it), or this is going nowhere
      cw.grow = 0; cw.nb = 0; cw.st = (cw.changed && cw.st != CW_IDLE) ? CW_RELOAD : CW_IDLE;
      return;
    }
  }
  if (cw.st == CW_IDLE) {
    if (!config_write || cw_host_seen || !ee_avail || menu_layer != L0_CLOCK || menu_dirty || uwTick < 10000u) return;
    if (cw.started && cw.seen_gen == ee_gen) return;        // nothing saved since the last look
    cw.started = 1; cw.seen_gen = ee_gen;
    if (!ovr.valid || !(ovr.simple_mask || ovr.modes_mask)) return;
    fatfs_busy = 1;                         // cw_locate reads through FATFS, which is not reentrant
    _Bool here = cw_locate();
    fatfs_busy = 0;
    if (!here) return;
    cw.slot = 0; cw.changed = 0; cw.grow = 0; cw.nb = 0; cw.ops = 0; cw.st = CW_ITEMS;
    return;
  }
  if (cw_host_seen || !ee_avail) { cw.st = CW_IDLE; return; }   // stop here: every state between rewrites is a good file
  if (cw.grow == 1) {                       // the gap is filled with spaces: now the directory entry
    ee_rd(cw_dir_addr, cw_buf, EE_QSPI_PGSZ);
    if (ee_rd_fault) { ee_fault_check(); cw.st = CW_IDLE; return; }
    uint8_t *e = cw_buf + cw_dir_off;
    uint32_t sz = cw.grow_to;
    time_t lt = currentTime + currentOffset; struct tm *t = gmtime(&lt);
    if (t->tm_year >= 80) {                 // a clock that knows the date stamps the file
      uint16_t ft = (uint16_t)((t->tm_hour << 11) | (t->tm_min << 5) | (t->tm_sec >> 1));
      uint16_t fd = (uint16_t)(((t->tm_year - 80) << 9) | ((t->tm_mon + 1) << 5) | t->tm_mday);
      memcpy(e + 22, &ft, 2); memcpy(e + 24, &fd, 2);
    }
    memcpy(e + 28, &sz, 4);
    if (ee_j_begin(cw_dir_addr, cw_buf)) { cw.grow = 2; cw.wait = 1; cw.ops++; }
    else { cw.grow = 0; cw.full = (ee_next >= EE_SLOTS); cw.st = CW_IDLE; }
    return;
  }
  if (cw.grow == 2) { cw.grow = 0; cw_size = cw.grow_to; }   // the directory now says so
  if (cw.nb) {                              // an old line, a sector at a time
    uint32_t from = cw.bfrom[cw.nb - 1u], to = cw.bto[cw.nb - 1u];
    uint32_t end = (from / EE_QSPI_PGSZ + 1u) * EE_QSPI_PGSZ;
    if (end >= to) { end = to; cw.nb--; } else cw.bfrom[cw.nb - 1u] = end;
    if (!cw_patch(from, "", 0, end)) cw.nb = 0;
    return;
  }
  char line[128]; int n = 0;
  if (cw.st == CW_ITEMS) {
    char val[12]; const char *key = NULL;
    while (cw.slot < MX_SIMPLE + NUM_DISPLAY_MODES && !menu_export_item(cw.slot, val)) cw.slot++;
    if (cw.slot >= MX_SIMPLE + NUM_DISPLAY_MODES) { cw.st = cw.changed ? CW_MARKER : CW_IDLE; return; }
    // The keys of one setting: tc_learn, tc_apply, tc_persist (8..10) and seg_balance, colon_balance (11, 12).
    uint16_t last = cw.slot == 8u ? 10u : (cw.slot == 11u ? 12u : cw.slot);
    uint32_t of[3], ot[3]; uint8_t no = 0;
    for (uint16_t sl = cw.slot; sl <= last; sl++) {
      if (!(key = menu_export_item(sl, val))) continue;
      if (!val[0]) strcpy(val, "-1");       // brightness on AUTO
      uint32_t vl = (uint32_t)strlen(val);
      CwSpan sp; cw_find(key, 0, &sp);
      if (ee_rd_fault) { ee_fault_check(); cw.st = CW_IDLE; return; }
      if (sp.found && !strcmp(sp.val, val)) continue;                          // the file already says it
      if (last == cw.slot && sp.found && sp.le - sp.vs >= vl + sp.lead && sp.vs / EE_QSPI_PGSZ == (sp.le - 1u) / EE_QSPI_PGSZ) {
        n = snprintf(line, sizeof line, "%s%s", sp.lead ? " " : "", val);      // where the old value stands, spaced as it was
        if (cw_patch(sp.vs, line, (uint32_t)n, sp.le)) cw.changed = 1; else cw.slot++;
        return;                             // the slot is looked at again: it will find the file says it
      }
      n += snprintf(line + n, sizeof line - (size_t)n, "%s = %s\r\n", key, val);
      if (sp.found) { of[no] = sp.ls; ot[no] = sp.le; no++; }
    }
    if (!n) { cw.slot = (uint16_t)(last + 1u); return; }
    if (cw_append(line, (uint32_t)n)) {     // the whole setting, in one rewrite
      cw.changed = 1;
      memcpy(cw.bfrom, of, sizeof of); memcpy(cw.bto, ot, sizeof ot); cw.nb = no;   // the old lines go once the new ones are in
    } else if (!cw.full && cw_size < (uint32_t)cw_nsect * EE_QSPI_PGSZ &&
               cw_patch(cw_size, "", 0, (cw_size / EE_QSPI_PGSZ + 1u) * EE_QSPI_PGSZ)) {
      cw.grow = 1; cw.grow_to = (cw_size / EE_QSPI_PGSZ + 1u) * EE_QSPI_PGSZ;   // no room: fill to the end of the sector, then make it the file's
    } else cw.slot = (uint16_t)(last + 1u); // no room at all: this one stays in the store
    return;
  }
  if (cw.st == CW_MARKER) {
    time_t lt = currentTime + currentOffset; struct tm *t = gmtime(&lt);
    n = snprintf(line, sizeof line, CW_MARK "%04d-%02d-%02d %02d:%02d by the clock\r\n",
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min);
    CwSpan sp; cw_find(CW_MARK, 1, &sp);
    if (sp.found && sp.le - sp.ls == CW_MARK_LEN && sp.ls / EE_QSPI_PGSZ == (sp.le - 1u) / EE_QSPI_PGSZ) cw_patch(sp.ls, line, CW_MARK_LEN, sp.le);
    else cw_append(line, (uint32_t)n);      // no room for it is no reason to stop
    cw.st = CW_RELOAD;
    return;
  }
  config.fdate = 0; config.ftime = 0;       // CW_RELOAD: read the file again, whatever its timestamp says
  delayedReadConfigFile = 1;
  cw.st = CW_IDLE;
}
#endif
