// main_parts/gps.h — the NMEA sentences the clock reads: RMC for time and position, GSV for satellites in view.
// A section of main.c, included there once and in order (the list is in main.c). It is not a header:
// it defines code and state, relies on everything main.c declares above it, and must not be included
// anywhere else. main_parts/README.md says why this is one translation unit.

uint8_t nmea[NMEA_BUF_SIZE];     // the sentence being received from the GPS module
uint8_t satview[SV_COUNT];       // satellites in view, from GSV (MODE_SATVIEW)
uint8_t satview_stale = 0;
uint8_t nmea_cdc_level=0;        // config "nmea": how much of the GPS stream is copied to USB serial (NMEA_NONE / NMEA_RMC / NMEA_ALL)

void decodeRMC(void){

  // do checksum
  uint8_t *c = &nmea[1], *end = &nmea[sizeof(nmea)];
  uint8_t sum=0;

  bcdStamp_t rmcBcd;
  struct tm rmcTm;

  while (*c !='*') {
    sum ^= *c;
    if (*c==',') *c=0;
    c++;
    if(c==end) return; //checksum not found
  }

  sprintf((char*)nmea, "%02X", sum);
  if (nmea[0] != c[1] || nmea[1]!=c[2]) return; //checksum error

#define nextField() while (*c && c!=end) c++; c++;

  c=&nmea[7]; // Time

  if (*c==0) return; // time not present

  rmcBcd.tenHours   = *c++ -'0';
  rmcBcd.hours      = *c++ -'0';
  rmcBcd.tenMinutes = *c++ -'0';
  rmcBcd.minutes    = *c++ -'0';
  rmcBcd.tenSeconds = *c++ -'0';
  rmcBcd.seconds    = *c++ -'0';

  if (*c++ =='.') { // subseconds not always present
    //if (*c!='0') printf("subseconds non-zero: %s\n", c);
  }
  nextField() // Navigation receiver warning
  data_valid = (*c=='A'?1:0);

  float tempLatitude=-9999, tempLongitude=-9999;

  nextField() // Latitude deg
  if (*c){
    tempLatitude =  (float)(*c++ -'0')*10.0;
    tempLatitude += (float)(*c++ -'0');
    tempLatitude += (float)atof((char*)c) / 60.0;
  }
  nextField() // Latitude N/S
  if (*c =='S') tempLatitude =-tempLatitude;

  nextField() // Longitude  deg
  if (*c){
    tempLongitude =  (float)(*c++ -'0')*100.0;
    tempLongitude += (float)(*c++ -'0')*10.0;
    tempLongitude += (float)(*c++ -'0');
    tempLongitude += (float)atof((char*)c) / 60.0;
  }
  nextField() // Longitude  E/W
  if (*c == 'W') tempLongitude =-tempLongitude;

  if (!config.fake_long && !config.fake_lat) {
    longitude = tempLongitude;
    latitude = tempLatitude;
    new_position=1;
  }

  nextField() // Speed over ground, Knots
  nextField() // Course Made Good, True
  nextField() // Date

  if (*c==0) return; // date not present

  rmcBcd.tenDays    = *c++ -'0';
  rmcBcd.days       = *c++ -'0';
  rmcBcd.tenMonths  = *c++ -'0';
  rmcBcd.months     = *c++ -'0';
  rmcBcd.tenYears   = *c++ -'0';
  rmcBcd.years      = *c++ -'0';


  // Immediately after power-up, the GPS module does not know the GPS time/UTC leapsecond offset, and makes a guess
  // Even if it gets a fix and starts outputting PPS, the time can be off by a few seconds (usually 2 or 3 fast)
  // Only make use of this invalid data if there is nothing else to go on
  if ( data_valid || (!had_pps && !rtc_good) ) {
    currentTime = bcdToTm( &rmcBcd, &rmcTm );

    if (decisec >= 9) {
      currentTime++;
      // check we're not <2ms away from rollover
      if (centisec==9 && millisec>7) return;

      // Under normal conditions, we should only be parsing nmea at around .300 to .400
      // USART1 preemption priority is currently 1, so we could be interrupted by systick here
      setNextTimestamp( currentTime );
      // In the alternate time-row modes the civil digits just staged must not reach the
      // display: restore the alt overlay so the boundary latch stays honest.
      if (countMode == COUNT_ALT) alt_render_next7seg(alt_hh, alt_mm, alt_ss);
      sendDate(0);
    }
  }

}

void decodeGSV(uint8_t rec){
  unsigned int sv = (nmea[11]-'0')*10 + (nmea[12]-'0');
  uint8_t constellation = nmea[2];
  uint8_t signal_id;

  // signal ID is not always present in GSV (on M8Q)

  unsigned int num_fields = 0, r=0;
  while (++r<rec) if (nmea[r]==',') num_fields++;

  if (num_fields % 4 != 0) {
    signal_id = '0';
  } else {
    signal_id = nmea[rec-6];
  }

  if (constellation == 'P') {
      if (signal_id == '0') {
        satview[SV_GPS_UNKNOWN] = sv;
      } else {
        satview[SV_GPS_L1] = sv;
      }
      satview_stale = 0;
  } else if (constellation == 'L') {
    if (signal_id == '0') {
      satview[SV_GLONASS_UNKNOWN] = sv;
    } else {
      satview[SV_GLONASS_L1] = sv;
    }
  } else if (constellation == 'A') {
    if (signal_id == '0') {
      satview[SV_GALILEO_UNKNOWN] = sv;
    } else {
      satview[SV_GALILEO_E1] = sv;
    }
  } else if (constellation == 'B') {
    if (signal_id == '0') {
      satview[SV_BEIDOU_UNKNOWN] = sv;
    } else {
      satview[SV_BEIDOU_B1] = sv;
    }
  }
}
