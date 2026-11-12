// main_parts/zones.h — timezone rules from TZRULES.BIN, and the second zone's offset.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

struct tzrule {
  uint32_t t;
  int32_t offset;
} rules[162], rules2[162];             // rules2 = ZONE 2's independent DST table
#define MAX_RULES  (sizeof rules  / sizeof rules[0])
#define MAX_RULES2 (sizeof rules2 / sizeof rules2[0])

char loadedRulesString[32];
char preloadRulesString[32];
_Bool delayedLoadRules = 0;
// ZONE 2 (MODE_ZONE2): a second civil timezone, named by the `zone2` config key.
int32_t offset2 = 0;                    // ZONE 2's resolved UTC->local offset (seconds)
char loadedZone2[32];                   // active ZONE 2 label ("" = unset/unresolved -> dashes)
char preloadZone2[32];                  // pending name from config, resolved in checkDelayedLoadRules
_Bool delayedLoadZone2 = 0;
_Bool zone2_fixed = 0;                  // 1 = offset2 is a literal (UTC / +HH:MM), no DST table walk

uint8_t f_getzcmp(FIL* fp, char * str){
  unsigned int rc;
  char * a = str;
  char b[1] = {1};
  uint8_t ret = 0;

  // TZRULES.BIN is host-writable, so it can be truncated (an interrupted copy). f_read at end-of-file
  // returns FR_OK with rc == 0 and leaves b untouched: without the rc check this loop never ended and
  // walked `a` off the end of RAM (HardFault, on every boot). `a` also stops at str's terminator.
  while (b[0]!=0) {
    if (f_read(fp, &b, 1, &rc) != FR_OK || rc != 1) return (uint8_t)-1;   // truncated: no match
    if (b[0] != *a) ret=-1;
    if (*a) a++;
  }
  return ret;
}
uint8_t findField( FIL* fp, char* str, uint8_t count, uint8_t padding ) {
  char buf[4];
  unsigned int rc;
  for (uint8_t i=0; i<count; i++) {
    if (f_getzcmp( fp, str ) ==0) return 1;
    f_read(fp, &buf, padding, &rc);
  }
  return 0;
}
SMALL_CODE uint8_t loadRules( char* cat, char* zo, struct tzrule* dest, uint32_t maxrules ) {
  FIL file;

  if (f_open(&file, RULES_FILENAME, FA_READ) != FR_OK) {
    return RULES_NO_FILE;
  }

  // Every field below comes from a host-writable file that may be TRUNCATED: a short read leaves
  // its buffer untouched, so each one is zero-initialised (never stack garbage) and the byte count
  // is checked. Any short read is a header error; nothing is written to dest[] until the whole
  // rule table is known to lie inside the file.
#define RULES_RD(p, n) (f_read(&file, (p), (n), &rc) == FR_OK && rc == (n))
#define RULES_FAIL(code) do { f_close(&file); return (code); } while (0)

  unsigned int rc;
  char buf[4] = {0};

  if (!RULES_RD(&buf, 4) || memcmp(&buf, "MTZ", 3)) RULES_FAIL(RULES_HEADER_ERR);
  if (buf[3]!=1) RULES_FAIL(RULES_VERSION_UNKNOWN);

  uint8_t rowLength = 0;
  if (!RULES_RD(&rowLength, 1)) RULES_FAIL(RULES_HEADER_ERR);

  uint8_t numCats = 0;
  if (!RULES_RD(&numCats, 1)) RULES_FAIL(RULES_HEADER_ERR);

  if (findField( &file, cat, numCats, 3 ) ==0) RULES_FAIL(RULES_CATEGORY_UNKNOWN);

  uint16_t catAddr = 0;
  if (!RULES_RD(&catAddr, 2)) RULES_FAIL(RULES_HEADER_ERR);

  uint8_t numZones = 0;
  if (!RULES_RD(&numZones, 1)) RULES_FAIL(RULES_HEADER_ERR);

  f_lseek(&file, catAddr);

  if (findField( &file, zo, numZones, 4 ) ==0) RULES_FAIL(RULES_ZONE_UNKNOWN);

  uint32_t zoAddr = 0;
  if (!RULES_RD(&zoAddr, 3)) RULES_FAIL(RULES_HEADER_ERR);

  uint8_t numEntries = 0;
  if (!RULES_RD(&numEntries, 1)) RULES_FAIL(RULES_HEADER_ERR);

  // TZRULES.BIN is host-writable over the USB mass-storage volume, so its length
  // fields are untrusted: a rowLength larger than one rule slot, or numEntries larger
  // than the array, would overrun rules[] (global RAM corruption / HardFault). Reject.
  // Likewise a rule table that runs past the end of the file: reject BEFORE the first
  // write, so a truncated file can never leave dest[] half old zone, half new.
  if (rowLength > sizeof dest[0] || numEntries > maxrules
      || (uint64_t)zoAddr + (uint32_t)numEntries * rowLength > (uint64_t)f_size(&file)) {
    RULES_FAIL(RULES_HEADER_ERR);
  }

  f_lseek(&file, zoAddr);

  int i;
  for (i=0;i<numEntries;i++) {
    f_read(&file, &dest[i], rowLength, &rc);
  }
  while (i< (int)maxrules ) {
    dest[i++].t=-1;
  }

  f_close(&file);

  return RULES_OK;
#undef RULES_RD
#undef RULES_FAIL
}

// loadRulesSingle modifies the input string, can't be used with const str
SMALL_CODE uint8_t loadRulesSingle(char * str){
  char * zo = str;
  while (*zo && *zo != '/') zo++;
  if (*zo!='/') return RULES_STR_ERR;
  *zo=0; zo++;
  uint8_t err = loadRules( str, zo, rules, MAX_RULES );
  if (!err) {
    zo--;*zo='/';
    strcpy( loadedRulesString, str );
  }
  return err;
}

// ZONE 2 counterpart of loadRulesSingle: resolve an IANA "Cat/Zone" into rules2[].
uint8_t loadZone2Single(char * str){
  char * zo = str;
  while (*zo && *zo != '/') zo++;
  if (*zo!='/') return RULES_STR_ERR;
  *zo=0; zo++;
  uint8_t err = loadRules( str, zo, rules2, MAX_RULES2 );
  if (!err) {
    zo--;*zo='/';
    strncpy( loadedZone2, str, sizeof loadedZone2 - 1 );
    loadedZone2[sizeof loadedZone2 - 1] = 0;
  }
  return err;
}

SMALL_CODE void checkDelayedLoadRules(){
  if (delayedLoadRules) {
    config.zone_override = 1;
    if (loadRulesSingle(preloadRulesString) !=RULES_OK) {
      config.zone_override = 0;
      if (data_valid) new_position=1;
    }
  }
  delayedLoadRules=0;

  if (delayedLoadZone2) {                                     // IANA-name path only (literals resolve in the config parse)
    delayedLoadZone2 = 0;
    zone2_fixed = 0;
    if (loadZone2Single(preloadZone2) != RULES_OK) loadedZone2[0] = 0;   // unresolved -> dashes
  }
}

// ZONE 2 offset resolver — mirrors setNextTimestamp()'s DST walk but on rules2[].
static int32_t zone2_offset_at(time_t t){
  if (zone2_fixed) return offset2;      // literal offset: no table walk
  int32_t off = 0;
  for (uint8_t i=0; i<MAX_RULES2; i++){
    if (rules2[i].t <= (uint32_t)t) off = rules2[i].offset;   // sentinel t==-1 (0xFFFFFFFF) never matches -> break
    else break;
  }
  return off;
}
// Parse a ZONE 2 fixed-offset literal ("UTC"/"GMT" or [+-]HH[:MM]) into seconds. Returns 0 on no match.
static _Bool zone2_parse_fixed(const char* s, int32_t* out){
  if (!strcasecmp(s,"UTC") || !strcasecmp(s,"GMT")) { *out = 0; return 1; }
  if (s[0] != '+' && s[0] != '-') return 0;
  int sign = (s[0]=='-') ? -1 : 1;
  const char* p = s + 1;
  if (*p < '0' || *p > '9') return 0;
  int hh = 0; while (*p>='0' && *p<='9'){ hh = hh*10 + (*p-'0'); p++; if (hh > 14) return 0; }
  int mm = 0;
  if (*p == ':'){ p++; if (*p<'0'||*p>'9') return 0; while (*p>='0'&&*p<='9'){ mm = mm*10 + (*p-'0'); p++; } }
  if (*p) return 0;                     // trailing garbage -> not a literal
  if (mm > 59) return 0;
  *out = sign * (hh*3600 + mm*60);
  return 1;
}
