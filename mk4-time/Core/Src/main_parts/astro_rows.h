// main_parts/astro_rows.h — the sun, moon, grid and star-transit rows: what the main loop computes for them, and ZONE 2 offsets.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

// --- Astro pack helpers ----------------------------------------------------
// A usable position is held in latitude/longitude from either a GPS fix or the
// configured fake_latitude/fake_longitude; both sit at the -9999 sentinel until
// a position is known, so a simple range check is the "have we got a fix" test.
static _Bool astro_pos_ok(float lat, float lon){
  return lat >= -90.0f && lat <= 90.0f && lon >= -180.0f && lon <= 180.0f;
}
// Sub-screen dwell (ms) for every mode that pages its date row. Unset -> 5500 ms, a
// subjectively-tuned cadence found by feel. Floored at 250 ms so a tiny value can't
// flood the date-board UART.
static uint32_t page_ms(void){ uint32_t m = config.page_ms; return m == 0 ? 5500 : (m < 250 ? 250 : m); }
// Decimal UTC hour (sun_times may return <0 or >24) -> local minutes-of-day [0,1440).
static int astro_local_minutes(double utc_h){
  double h = fmod(utc_h + currentOffset / 3600.0, 24.0);
  if (h < 0) h += 24.0;
  int m = (int)(h * 60.0 + 0.5);
  if (m >= 1440) m -= 1440;
  return m;
}
// Recompute the astro cache (called from the main loop, never the ISR). The
// double soft-float maths runs here, then the small result struct is swapped in
// under a brief IRQ mask so sendDate() always reads a consistent snapshot.
static void astro_update(void){
  if (astro.epoch == (uint32_t)currentTime) return;     // at most once a second
  struct astro_cache_s c = {0};
  c.epoch = (uint32_t)currentTime;
  double ph = moon_phase((double)currentTime);          // moon needs no fix
  c.moon_idx = moon_phase_index(ph);
  c.moon_pct = (uint8_t)(moon_illuminated_fraction(ph) * 100.0 + 0.5);
  float lat = latitude, lon = longitude;                // one consistent snapshot of the fix
  c.have_pos = astro_pos_ok(lat, lon);
  if (c.have_pos) {
    c.lat_show = lat;
    c.lon_show = lon;
    double az, el, rise = 0, set = 0, noon = 0, civd = 0, naud = 0, astd = 0;
    sun_az_el(lat, lon, (double)currentTime, &az, &el);
    int ia = (int)(az + 0.5); if (ia >= 360) ia -= 360;
    c.az = (int16_t)ia;
    c.el = (int16_t)(el < 0 ? el - 0.5 : el + 0.5);
    c.civ_dusk_min = c.nau_dusk_min = c.ast_dusk_min = c.ast_dawn_min = -1;   // MODE_DARK: n/a unless computed below
    c.sun_up_today = (sun_times(lat, lon, (double)currentTime,
                                &rise, &set, &noon, &civd, &naud, 0, &astd) == 0);
    c.noon_min = (int16_t)astro_local_minutes(noon);     // noon is valid even at the poles
    if (c.sun_up_today) {
      c.rise_min = (int16_t)astro_local_minutes(rise);
      c.set_min  = (int16_t)astro_local_minutes(set);
      c.civ_dusk_min = (int16_t)astro_local_minutes(civd);
      c.nau_dusk_min = (int16_t)astro_local_minutes(naud);
      if (!isnan(astd)) {                                 // astronomical dark occurs -> dusk + symmetric dawn
        c.ast_dusk_min = (int16_t)astro_local_minutes(astd);
        c.ast_dawn_min = (int16_t)astro_local_minutes(2.0 * noon - astd);   // dawn = mirror of dusk about solar noon
        c.dark_tonight = 1;
      }
    } else {
      c.always_dark = (c.el < -18);                       // polar night: sun deep below the horizon now
    }
    maidenhead(lat, lon, c.grid);
  } else {
    strcpy(c.grid, "----");
  }
  __disable_irq();
  astro = c;
  __enable_irq();
}

// ---- Bright-star meridian-transit predictor (MODE_STAR) ----------------------------------------
// A star crosses the local meridian (upper culmination — its highest point in the sky) exactly when
// the Local Sidereal Time equals the star's right ascension. From the GPS fix + local_sidereal_time
// we compute, for each catalogue star, the seconds until its next transit and the altitude it will
// reach (90 - |latitude - declination|), then cache the soonest few for a paged "<name> <h:mm>"
// countdown on the date row. J2000 catalogue positions are precessed to date (first-order IAU) so
// the timing stays good to the shown minute for decades. Compute is main-loop only (double trig).
//
// The catalogue is loaded at boot from /STARS.BIN on the CLOCK drive (the QSPI flash volume;
// generate-stars.py, HYG v4). There is deliberately NO baked-in fallback: without a valid file the
// mode honestly shows nothing ("STAr ----") rather than quietly substituting lookalike data. The
// transit *maths* is verified in the emulator.
#define STAR_MAX   128u            // RAM cap on the loaded catalogue (the STARS.BIN file is clamped to this)
#define STAR_SHOW  8u              // cache the soonest 8 upcoming transits
#define STAR_SIDSEC_PER_HR 3590.1704   // solar seconds the meridian takes to sweep one hour of RA
typedef struct { char nm[4]; uint32_t epoch; int8_t alt; char dir; } star_entry_t;   // dir: culminates due (S)outh / (N)orth
static star_entry_t star_cache[STAR_SHOW];
static volatile uint8_t star_ncache;

// The live catalogue in RAM: J2000 RA hours / Dec degrees + proper motion (mas/yr; mu_alpha* incl.
// cos-dec), plus the cached APPARENT place of date (ra_now/dec_now, refreshed daily) the transit
// math consumes. Loaded from /STARS.BIN (the mode requires it; no baked default).
static struct { char nm[4]; float ra; float dec; int16_t pmra, pmdec; float ra_now, dec_now; } star_buf[STAR_MAX];
static uint16_t star_count = 0;
static uint32_t star_apparent_at = 0;  // currentTime of the last apparent-place refresh (0 = never)
volatile float star_max_mag = 6.0f;   // config "star_max_mag": only load stars brighter than this (file is mag-sorted -> early-stop). Default 6 = the whole file.

// Load /STARS.BIN into star_buf, filtered to star_max_mag. The file is magnitude-sorted, so we stop at
// the first star past the cut. Hardened like loadRules (PR#7): validate magic + record length, clamp
// the count BEFORE writing, byte-check every read, sanity-check RA/Dec, scrub name bytes. No file (or
// an invalid one) leaves the catalogue EMPTY — MODE_STAR requires STARS.BIN on the CLOCK drive.
static void loadStars(void){
  star_count = 0; star_apparent_at = 0;
  FIL file;
  _Bool file_ok = 0;
  if (f_open(&file, STARS_FILENAME, FA_READ) == FR_OK){
    unsigned int rc; uint8_t hdr[16];
    uint16_t rl = 0;
    if (f_read(&file, hdr, 16, &rc) == FR_OK && rc == 16 && memcmp(hdr, "MST1", 4) == 0
        && ((rl = (uint16_t)(hdr[6] | (hdr[7]<<8))) == 10u || rl == 14u)  // v1 (no PM) or v2 (+pmra/pmdec i16)
        && (uint16_t)(hdr[8] | (hdr[9]<<8)) == 100u){             // mag_scale we decode against (mag*100); reject a file written to a different scale
      file_ok = 1;
      uint16_t count  = (uint16_t)(hdr[4] | (hdr[5]<<8));
      float    mc = star_max_mag * 100.0f;                       // saturate: a huge star_max_mag means "load all", never wrap negative (out-of-range float->int16 is UB)
      int16_t  magcut = mc > 32767.0f ? 32767 : (mc < -32768.0f ? -32768 : (int16_t)mc);
      for (uint16_t k = 0; k < count && star_count < STAR_MAX; k++){
        uint8_t rec[14];
        if (f_read(&file, rec, rl, &rc) != FR_OK || rc != rl) break;      // torn read -> keep what we have
        int16_t mag = (int16_t)(rec[4] | (rec[5]<<8));
        if (mag > magcut) continue;                                       // filter per-record (don't trust the file to be mag-sorted); loop still bounded by EOF + STAR_MAX
        float ra  = (float)(uint16_t)(rec[0] | (rec[1]<<8)) / 65536.0f * 24.0f;
        float dec = (float)( int16_t)(rec[2] | (rec[3]<<8)) / 100.0f;
        if (ra < 0.0f || ra >= 24.0f || dec < -90.0f || dec > 90.0f) continue;   // reject garbage
        for (int b = 0; b < 4; b++){                                      // sanitize: name bytes go raw to the date-board UART — never forward control/high-bit bytes from a hostile file
          uint8_t c = rec[6 + b];
          star_buf[star_count].nm[b] = (c < 0x20 || c > 0x7E) ? ' ' : (char)c;
        }
        star_buf[star_count].ra = ra; star_buf[star_count].dec = dec;
        star_buf[star_count].pmra  = (rl == 14u) ? (int16_t)(rec[10] | (rec[11]<<8)) : 0;
        star_buf[star_count].pmdec = (rl == 14u) ? (int16_t)(rec[12] | (rec[13]<<8)) : 0;
        star_count++;
      }
    }
    f_close(&file);
  }
  (void)file_ok;   // no fallback by design: absent/invalid file -> star_count 0 -> "STAr ----"
}

// J2000 -> apparent place of date: linear proper motion, then RIGOROUS IAU-1976 precession (the
// zeta/z/theta rotation). The previous first-order formula carried a tan(dec) term that diverges
// near the pole — Polaris's transit countdown was ~5 minutes wrong by 2026 and growing. The exact
// rotation has no singularity; it costs ~6 double-trig per star, so it runs at LOW cadence (daily —
// precession moves ~0.14 arcsec/day) and the per-second star_update just consumes the cache.
static void star_refresh_apparent(void){
  const double D2R = 0.017453292519943295;
  double yrs = ((double)currentTime - 946728000.0) / 31557600.0;   // Julian years since J2000.0
  double T = yrs / 100.0;                                          // Julian centuries
  double zeta  = (2306.2181*T + 0.30188*T*T + 0.017998*T*T*T) * (D2R / 3600.0);
  double zz    = (2306.2181*T + 1.09468*T*T + 0.018203*T*T*T) * (D2R / 3600.0);
  double theta = (2004.3109*T - 0.42665*T*T - 0.041833*T*T*T) * (D2R / 3600.0);
  double st = sin(theta), ct = cos(theta);
  for (uint16_t s = 0; s < star_count; s++){
    double d0 = (double)star_buf[s].dec;
    double cd = cos(d0 * D2R); if (cd < 1e-6) cd = 1e-6;           // pole guard for the mu/cos(dec) term
    double a0 = (double)star_buf[s].ra
              + ((double)star_buf[s].pmra / cd) * yrs / (3600000.0 * 15.0);  // mas/yr (mu_alpha*) -> hours
    d0       +=  (double)star_buf[s].pmdec       * yrs /  3600000.0;         // mas/yr -> degrees
    double ar = a0 * 15.0 * D2R + zeta, dr = d0 * D2R;
    double ca = cos(ar), sa = sin(ar), cdd = cos(dr), sd = sin(dr);
    double A = cdd * sa;
    double B = ct * cdd * ca - st * sd;
    double C = st * cdd * ca + ct * sd;
    double a_now = (atan2(A, B) + zz) / (D2R * 15.0);              // hours
    a_now = fmod(a_now, 24.0); if (a_now < 0.0) a_now += 24.0;
    if (C >  1.0) C =  1.0;
    if (C < -1.0) C = -1.0;
    star_buf[s].ra_now  = (float)a_now;
    star_buf[s].dec_now = (float)(asin(C) / D2R);
  }
  star_apparent_at = (uint32_t)currentTime;
}

static void star_update(void){
  float lat = latitude, lon = longitude;                       // one snapshot of the fix
  if (!astro_pos_ok(lat, lon)) { star_ncache = 0; return; }
  if (star_apparent_at == 0 || (uint32_t)((uint32_t)currentTime - star_apparent_at) > 86400u)
    star_refresh_apparent();                                   // daily is plenty (~0.14 arcsec/day)
  double lst = local_sidereal_time((double)currentTime, (double)lon);      // hours [0,24)

  // Single pass: keep the soonest STAR_SHOW visible stars in a small array sorted ascending by
  // sidereal-hours-to-transit. No per-star scratch (star_count can be the whole SD catalogue), so the
  // stack stays bounded regardless of catalogue size.
  struct { float dt; int8_t alt; char nm[4]; char dir; } top[STAR_SHOW]; uint8_t n = 0;
  for (uint16_t s = 0; s < star_count; s++) {
    double a_now = (double)star_buf[s].ra_now, d_now = (double)star_buf[s].dec_now;  // apparent of date (cached)
    double diff = (double)lat - d_now;                                     // sign = which horizon it culminates over
    double alt = 90.0 - fabs(diff);                                        // upper-transit altitude (geometric)
    if (alt <= -0.57) continue;                                            // horizon gate WITH refraction: ~34' lifts a grazer into view
    double dt = fmod(a_now - lst, 24.0); if (dt < 0.0) dt += 24.0;         // sidereal hours to transit
    float dtf = (float)dt;
    if (n < STAR_SHOW || dtf < top[n-1].dt) {                             // insertion-sort into the top-N
      uint8_t pos = (n < STAR_SHOW) ? n : (uint8_t)(STAR_SHOW - 1);
      if (n < STAR_SHOW) n++;
      while (pos > 0 && top[pos-1].dt > dtf) { top[pos] = top[pos-1]; pos--; }
      top[pos].dt = dtf; memcpy(top[pos].nm, star_buf[s].nm, 4);
      top[pos].alt = (int8_t)(alt >= 0.0 ? alt + 0.5 : 0.0);              // refraction-band grazers read alt 0
      top[pos].dir = (diff >= 0.0) ? 'S' : 'N';                           // dec below latitude -> due south, else due north
    }
  }
  __disable_irq();
  for (uint8_t i = 0; i < n; i++) {
    memcpy(star_cache[i].nm, top[i].nm, 4);
    star_cache[i].epoch = (uint32_t)currentTime + (uint32_t)((double)top[i].dt * STAR_SIDSEC_PER_HR + 0.5);
    star_cache[i].alt = top[i].alt;
    star_cache[i].dir = top[i].dir;
  }
  star_ncache = n;
  __enable_irq();
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
