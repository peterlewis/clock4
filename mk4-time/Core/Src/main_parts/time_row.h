// main_parts/time_row.h — the big digits: the next second, countdown, the sidereal / solar / second-zone timebases, the RTC write.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

#define rtc_last_write RTC->BKP30R   // when the RTC was last written (a backup register: it survives a reset)

void setNextTimestamp(time_t nextTime){

  int32_t offset = 0;
  for (uint8_t i=0; i< MAX_RULES; i++) {
    if (rules[i].t <= nextTime) offset=rules[i].offset;
    else break;
  }
  // in case of the remote chance that we're interrupted while calculating,
  // don't assign to currentOffset until the end of the loop
  currentOffset = offset;
  nextTime += offset;

  struct tm * nextTm = gmtime( &nextTime );
  tmToBcd( nextTm, &nextBcd );
  tm_yday = nextTm->tm_yday;
  tm_wday = nextTm->tm_wday;

  if (displayMode == MODE_ISO_WEEK){
    iso_wday = (nextTm->tm_wday + 6) % 7;
    nextTm->tm_mday -= iso_wday -3;
    mktime(nextTm);
    iso_year = nextTm->tm_year + 1900;
    iso_week = nextTm->tm_yday/7 + 1;
  }

  next7seg.c = cLut[nextBcd.seconds];

  next7seg.b[0] = bCat0 | cLut[nextBcd.tenHours]<<2;
  next7seg.b[1] = bCat1 | cLut[nextBcd.hours]<<2;
  next7seg.b[2] = bCat2 | cLut[nextBcd.tenMinutes]<<2;
  next7seg.b[3] = bCat3 | cLut[nextBcd.minutes]<<2;
  next7seg.b[4] = bCat4 | cLut[nextBcd.tenSeconds]<<2;

}

void setNextCountdown(time_t nextTime){

  int64_t remaining;
  if (config.countdown_to < nextTime) {
    remaining = 0;
    SetPPS( &PPS_NoUpdate ); // don't show 999 at the next pulse

  } else remaining = config.countdown_to - nextTime;

  uint64_t seconds = remaining % 60;
  uint64_t minutes = remaining / 60;
  uint64_t hours =   minutes / 60;
  minutes %= 60;
  countdown_days = hours / 24;
  hours %= 24;

  next7seg.b[0] = bCat0 | cLut[hours / 10]<<2;
  next7seg.b[1] = bCat1 | cLut[hours % 10]<<2;
  next7seg.b[2] = bCat2 | cLut[minutes / 10]<<2;
  next7seg.b[3] = bCat3 | cLut[minutes % 10]<<2;
  next7seg.b[4] = bCat4 | cLut[seconds / 10]<<2;
  next7seg.c = cLut[seconds % 10];
}

// --- Alternate timebase (MODE_LST / MODE_SOLAR / MODE_ZONE2) -----------------------------
// The TIME ROW ticks Local Sidereal Time, apparent solar time, or the ZONE 2 civil time. The
// math runs in THREAD context once per second (alt_update), staging the reading for the
// coming civil boundary; the SysTick_Alt_* handlers latch it at the .900 prep mark. The
// display is quantized to civil second boundaries — value = floor(alt time at the boundary),
// reseeded every second — so GPS discipline and holdover honesty are inherited from
// currentTime for free. Sidereal runs 1.00273791x civil: the seconds display double-steps
// once every ~6 min 5 s. That skip is the authentic signature of a true sidereal clock.
static volatile struct {
  uint8_t hh, mm, ss;
  uint32_t for_time;            // civil epoch this reading is the floor of; 0 = invalid
} alt_stage;
static uint8_t alt_hh, alt_mm, alt_ss;   // ISR-owned: what the row currently shows
static volatile _Bool alt_have_pos = 0;     // the timebase is computable: a position (LST/SOLAR), a resolved zone (ZONE 2)
static volatile _Bool alt_seed_pending = 0;  // mode entered: thread must seed the row
static volatile uint8_t alt_gen = 0;         // bumped on mode entry; cancels in-flight staging

// Overlay an alternate HH:MM:SS onto the next7seg staging buffer. The stock
// setNextTimestamp() has just run (keeping nextBcd / DST / date-row bookkeeping fresh);
// only the six time-row digit patterns are replaced.
#define alt_render_next7seg(hh, mm, ss) do { \
    next7seg.c    = cLut[(ss) % 10]; \
    next7seg.b[0] = bCat0 | cLut[(hh) / 10] << 2; \
    next7seg.b[1] = bCat1 | cLut[(hh) % 10] << 2; \
    next7seg.b[2] = bCat2 | cLut[(mm) / 10] << 2; \
    next7seg.b[3] = bCat3 | cLut[(mm) % 10] << 2; \
    next7seg.b[4] = bCat4 | cLut[(ss) / 10] << 2; \
  } while (0)

// The .900 prep for the alternate modes: stock next-second bookkeeping first, then latch
// the staged reading — or, if the main loop was starved past the boundary, advance the last
// shown reading by one second. LST's fallback runs SLOW (2.74 ms/s; the reseed snap is
// always forward), SOLAR's runs fast by at most ~0.35 ms/s at the EoT extremes — a
// visible backwards reseed would need ~48+ minutes of continuous main-loop starvation.
#define alt_prep_next() do { \
    currentTime++; \
    setNextTimestamp( currentTime ); \
    if (alt_stage.for_time == (uint32_t)currentTime) { \
      alt_hh = alt_stage.hh; alt_mm = alt_stage.mm; alt_ss = alt_stage.ss; \
    } else if (++alt_ss >= 60) { \
      alt_ss = 0; \
      if (++alt_mm >= 60) { alt_mm = 0; if (++alt_hh >= 24) alt_hh = 0; } \
    } \
    alt_render_next7seg(alt_hh, alt_mm, alt_ss); \
    sendDate(0); \
  } while (0)

// Compute floor-HH:MM:SS of the alternate time at `when` (thread context only: doubles).
static _Bool alt_compute(uint32_t when, uint8_t *hh, uint8_t *mm, uint8_t *ss){
  if (displayMode == MODE_ZONE2) {         // whole-second offset from civil: integer, needs no position
    if (!loadedZone2[0]) return 0;         // unset / unresolved -> dashed until the zone loads
    long t = (long)when + zone2_offset_at((time_t)when);
    long sod = ((t % 86400L) + 86400L) % 86400L;
    *hh = (uint8_t)(sod / 3600); *mm = (uint8_t)((sod / 60) % 60); *ss = (uint8_t)(sod % 60);
    return 1;
  }
  float lat = latitude, lon = longitude;   // one consistent snapshot (astro_update pattern)
  if (!astro_pos_ok(lat, lon)) return 0;
  double hours = (displayMode == MODE_LST)
               ? local_sidereal_time((double)when, (double)lon)
               : local_solar_time((double)when, (double)lon);
  if (!(hours >= 0.0) || hours >= 24.0) hours = 0.0;  // NaN / float-residue guard
  int h2 = (int)hours;
  double fm = (hours - h2) * 60.0;
  int m2 = (int)fm;
  int s2 = (int)((fm - m2) * 60.0);
  if (h2 > 23) h2 = 23;
  if (m2 > 59) m2 = 59;
  if (s2 > 59) s2 = 59;
  *hh = (uint8_t)h2; *mm = (uint8_t)m2; *ss = (uint8_t)s2;
  return 1;
}

// Main-loop staging (thread context — ALL the double math for these modes lives here).
// Two jobs: (a) SEED after mode entry or position go-live — render + latch the current
// reading immediately and install the live handlers, so a PPS latch can never show civil
// digits under the alternate colon; (b) STAGE the reading for the coming civil boundary.
// A generation counter cancels any in-flight computation when the mode flips mid-pass, so
// a stale timebase can never be stamped as valid.
void alt_update(void){
  if (!is_alt_mode(displayMode)) return;

  uint8_t gen = alt_gen;                 // snapshot: mode flips abort the publish below

  if (alt_seed_pending || !alt_have_pos) {
    uint8_t hh, mm, ss;
    if (!alt_compute((uint32_t)currentTime, &hh, &mm, &ss)) {
      alt_have_pos = 0;                  // stay dashed; retried every pass
      return;
    }
    __disable_irq();
    if (gen == alt_gen) {
      alt_hh = hh; alt_mm = mm; alt_ss = ss;
      alt_render_next7seg(alt_hh, alt_mm, alt_ss);   // alt digits now staged: any latch is honest
      latchSegments()                                 // and shown immediately (countdown precedent)
      alt_have_pos = 1;
      alt_seed_pending = 0;
    }
    __enable_irq();
    if (gen == alt_gen) setPrecision();  // install Alt_Px/PPS now — don't wait for PendSV,
                                         // or the NoUpdate .900 prep could stage civil digits
    return;                              // stage the coming boundary on the next pass
  }

  uint32_t target = (uint32_t)currentTime + 1;
  if (alt_stage.for_time == target) return;
  uint8_t hh, mm, ss;
  if (!alt_compute(target, &hh, &mm, &ss)) {
    alt_have_pos = 0;                    // position lost: setPrecision dashes it this second
    alt_stage.for_time = 0;
    return;
  }
  __disable_irq();
  if (gen == alt_gen) {                  // publish only if no mode flip happened mid-compute
    alt_stage.hh = hh; alt_stage.mm = mm; alt_stage.ss = ss;
    alt_stage.for_time = target;         // IRQs masked: fields and stamp are one atomic unit
  }
  __enable_irq();
}

// Store UTC on RTC
// need to also write zone into backup registers
// Only called at the start of a second, don't attempt to write subseconds.
void write_rtc(void){

  RTC_DateTypeDef sdatestructure;
  RTC_TimeTypeDef stimestructure;
  bcdStamp_t cBcd;
  struct tm * cTm = gmtime( &currentTime );

  tmToBcd( cTm, &cBcd );

  sdatestructure.Year    = (cBcd.tenYears<<4)  | cBcd.years;
  sdatestructure.Month   = (cBcd.tenMonths<<4) | cBcd.months;
  sdatestructure.Date    = (cBcd.tenDays<<4)   | cBcd.days;
  sdatestructure.WeekDay = RTC_WEEKDAY_MONDAY;

  HAL_RTC_SetDate(&hrtc,&sdatestructure,RTC_FORMAT_BCD);

  stimestructure.Hours          = (cBcd.tenHours<<4)  | cBcd.hours;
  stimestructure.Minutes        = (cBcd.tenMinutes<<4)  | cBcd.minutes;
  stimestructure.Seconds        = (cBcd.tenSeconds<<4)  | cBcd.seconds;
  stimestructure.SubSeconds     = 0x00;
  stimestructure.TimeFormat     = RTC_HOURFORMAT12_AM;
  stimestructure.DayLightSaving = RTC_DAYLIGHTSAVING_NONE ;
  stimestructure.StoreOperation = RTC_STOREOPERATION_RESET;

  HAL_RTC_SetTime(&hrtc,&stimestructure,RTC_FORMAT_BCD);

  // Write zone info to backup registers
  // There are 32 words of memory, 128 bytes
  // First 8 words are the zone string including separator and null byte (always less than 32 bytes)
  // Next 22 words is a chunk of the ruleset in use, i.e. 11 years
  // Last two words are time of write, and time of last calibration

  uint8_t i;
  for (i=0; i< MAX_RULES; i++) {
    if (rules[i].t > currentTime) break;
  }
  if (i==0) return; //something has gone wrong, data invalid
  i--; //include currently active rule

  char numRulesToStore = (i+11>=MAX_RULES-1)? (MAX_RULES-i)*2 : 22;

  memcpyword( (uint32_t*)&(RTC->BKP0R), (uint32_t*)loadedRulesString, 8 );
  memcpyword( (uint32_t*)&(RTC->BKP8R), (uint32_t*)&rules[i], numRulesToStore );

  rtc_last_write = (uint32_t)currentTime;
}

time_t bcdToTm(bcdStamp_t *in, struct tm *out ) {
  out->tm_isdst = 0;
  out->tm_sec = in->seconds + in->tenSeconds*10;
  out->tm_min = in->minutes + in->tenMinutes*10;
  out->tm_hour = in->hours + in->tenHours*10;
  out->tm_mday = in->days + in->tenDays*10;
  out->tm_mon = in->months + in->tenMonths*10 -1;
  out->tm_year = in->years + in->tenYears*10 + 100; //Years since 1900

  return mktime(out);
}
void tmToBcd(struct tm *in, bcdStamp_t *out ) {
  out->tenYears   = (in->tm_year-100) / 10;
  out->years      = (in->tm_year-100) % 10;
  out->tenMonths  = (in->tm_mon+1) / 10;
  out->months     = (in->tm_mon+1) % 10;
  out->tenDays    = in->tm_mday / 10;
  out->days       = in->tm_mday % 10;
  out->tenHours   = in->tm_hour / 10;
  out->hours      = in->tm_hour % 10;
  out->tenMinutes = in->tm_min / 10;
  out->minutes    = in->tm_min % 10;
  out->tenSeconds = in->tm_sec / 10;
  out->seconds    = in->tm_sec % 10;
}
