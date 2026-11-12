/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "fatfs.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "qspi_drv.h"
#include "zonedetect.h"
#include "chainloader.h"
#include "astro.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
// This project is compiled -O3, for the interrupt handlers and the display path. Code that runs rarely
// and never on a timing path is marked cold instead: the compiler then builds it for size, and keeps it
// out of line. That is start-up initialisation, the config parser and loader, the settings stores, the
// menu's screens, the serial reports, the config.txt writer and the once-a-second temperature
// housekeeping. At -O3 they came to about 12 KB more than they need. Not marked, on purpose: the
// SysTick, PPS, PendSV, UART, DMA and DAC handlers and everything they run, sendDate, the brightness
// and colon polling, the cuckoo, the astronomy. Two marked things do run in an interrupt, the USB one,
// which has a lower priority than those handlers and cannot delay them: the parser, when a config line
// arrives over serial, and SystemClock_Config, when the bus resumes from suspend.
#if defined(__GNUC__)
#define SMALL_CODE __attribute__((cold, noinline))
#else
#define SMALL_CODE
#endif

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
ADC_HandleTypeDef hadc3;

CRC_HandleTypeDef hcrc;

DAC_HandleTypeDef hdac1;
DMA_HandleTypeDef hdma_dac_ch1;

QSPI_HandleTypeDef hqspi;

RTC_HandleTypeDef hrtc;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim5;
TIM_HandleTypeDef htim6;
TIM_HandleTypeDef htim7;
DMA_HandleTypeDef hdma_tim1_up;
DMA_HandleTypeDef hdma_tim5_ch1;
DMA_HandleTypeDef hdma_tim5_ch2;
DMA_HandleTypeDef hdma_tim7_up;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart1_rx;
DMA_HandleTypeDef hdma_usart2_tx;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_QUADSPI_Init(void);
static void MX_TIM1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_TIM2_Init(void);
static void MX_ADC1_Init(void);
static void MX_DAC1_Init(void);
static void MX_TIM6_Init(void);
static void MX_RTC_Init(void);
static void MX_TIM7_Init(void);
static void MX_CRC_Init(void);
static void MX_LPTIM1_Init(void);
static void MX_TIM5_Init(void);
static void MX_ADC3_Init(void);
/* USER CODE BEGIN PFP */
// Start-up initialisation, run once. Declared again here to mark it: the definitions are generated.
SMALL_CODE void SystemClock_Config(void);
static SMALL_CODE void MX_GPIO_Init(void);
static SMALL_CODE void MX_DMA_Init(void);
static SMALL_CODE void MX_QUADSPI_Init(void);
static SMALL_CODE void MX_TIM1_Init(void);
static SMALL_CODE void MX_USART2_UART_Init(void);
static SMALL_CODE void MX_USART1_UART_Init(void);
static SMALL_CODE void MX_TIM2_Init(void);
static SMALL_CODE void MX_ADC1_Init(void);
static SMALL_CODE void MX_DAC1_Init(void);
static SMALL_CODE void MX_TIM6_Init(void);
static SMALL_CODE void MX_RTC_Init(void);
static SMALL_CODE void MX_TIM7_Init(void);
static SMALL_CODE void MX_CRC_Init(void);
static SMALL_CODE void MX_LPTIM1_Init(void);
static SMALL_CODE void MX_TIM5_Init(void);
static SMALL_CODE void MX_ADC3_Init(void);
void tmToBcd(struct tm *in, bcdStamp_t *out );
uint8_t loadRulesSingle(char * str);
void nextMode(_Bool);
static void menu_flash(const char *s);     // menu.h; the store calls it (a note on the date row after a factory reset)
static void cfg_release(const char *key);   // config.h; the store calls it (a bundled setting the file takes back from the menu)
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
// ---- State shared by several sections (a section's own state is declared in its own file) ------
const uint8_t cLut[]= { cSegDecode0, cSegDecode1, cSegDecode2, cSegDecode3, cSegDecode4, cSegDecode5, cSegDecode6, cSegDecode7, cSegDecode8, cSegDecode9 };

buffer_c_t buffer_c[80] = {0};

uint16_t buffer_b[80] = {0};

uint8_t uart2_tx_buffer[32];

volatile uint16_t buffer_adc[ADC_BUFFER_SIZE] = {0};
uint16_t buffer_dac[DAC_BUFFER_SIZE] = {[0 ... DAC_BUFFER_SIZE-1] = 4095};
float dac_target=4095;

time_t currentTime;
bcdStamp_t nextBcd;
int tm_yday;
int8_t tm_wday;
int iso_year;
int8_t iso_wday;
uint8_t iso_week;
uint32_t countdown_days;
int32_t currentOffset=0;

struct {
  uint8_t c;
  uint16_t b[5];
} next7seg;

uint8_t decisec=0, centisec=0, millisec=0;

float longitude=-9999, latitude=-9999;
_Bool data_valid=0, had_pps=0, rtc_good=0, new_position=1;
// Last fix the ZoneDetect timezone lookup actually ran for. The NMEA parser sets new_position on
// EVERY 1 Hz fix, but that lookup reads TZMAP.BIN off the QSPI FATFS and costs ~300 ms — so on a
// stationary clock it re-ran every second on metre-scale GPS jitter, a 300 ms main-loop stall per
// second (starves the balance mirrors, widens the date-board byte-drop window). 999 = never yet.
double zone_lat=999.0, zone_lon=999.0;

uint32_t last_pps_time = 0;

_Bool delayedReadConfigFile = 0;
_Bool delayedCheckOnEject = 0;
// Set while the main loop is inside a (non-reentrant) FATFS operation, so the USB-ISR
// firmware-eject check defers instead of corrupting FATFS state. volatile: ISR-visible.
volatile uint8_t fatfs_busy = 0;

_Bool waitingForLatch = 0;
_Bool resendDate = 0;

uint8_t displayMode = 0, countMode = 0;
// The alternate-timebase modes: the time row ticks another clock (sidereal, apparent solar, or the
// ZONE 2 civil time), the date row keeps the local civil date, and the colons take colon_alt_mode.
#define is_alt_mode(m) ((m) == MODE_LST || (m) == MODE_SOLAR || (m) == MODE_ZONE2)
uint8_t requestMode = 255;

volatile uint8_t config_write = 1;   // on by default; "config_write = off" in config.txt stops it. config_write.h (below it) acts on it


// ---- The menu's public face --------------------------------------------------------------------
// Two things other sections read: the layer the menu is on (the store commits to flash only at
// L0_CLOCK) and the text it puts on the date row (sendDate shows it). The rest of the menu's state
// is in main_parts/menu.h.
// v2 four layers: CLOCK -> SECTION ring -> ITEM ring (within a section) -> value EDITor.
typedef enum { L0_CLOCK=0, L1_SECTION, L2_ITEM, L3_EDIT } MenuLayer;
static MenuLayer menu_layer   = L0_CLOCK;
static char      menu_text[11]= {0};    // non-empty => the menu OWNS the date row (see sendDate)

#define CHECK_CONFIG_MTIME

struct {
#ifdef CHECK_CONFIG_MTIME
  unsigned short fdate;
  unsigned short ftime;
#endif
  uint32_t tolerance_1ms;
  uint32_t tolerance_10ms;
  uint32_t tolerance_100ms;
  float fake_long;
  float fake_lat;
  time_t countdown_to;
  float brightness_override;
  volatile _Bool zone_override;
  uint16_t page_ms;             // sub-screen dwell, ms, for every mode that pages its date row on uwTick / page_ms()
  _Bool modes_enabled[NUM_DISPLAY_MODES];

} config = {0};
// Sub-screen dwell (ms) for every mode that pages its date row. Unset -> 5500 ms, a
// subjectively-tuned cadence found by feel. Floored at 250 ms so a tiny value can't
// flood the date-board UART.
static uint32_t page_ms(void){ uint32_t m = config.page_ms; return m == 0 ? 5500 : (m < 250 ? 250 : m); }

struct {
  float in;
  float out;
} brightnessCurve[] = {
    // Measured VTT9812FH (R11 = 470K) response, baked in so the clock needs no config.txt BS lines.
    // These are the same five stops as "BSn = in,out" (out is inverted, 4095-out, exactly as
    // parseBrightness does); a config.txt BS line still overrides its stop at load.
    {0,    4095-0},       // BS1 = 0,0
    {131,  4095-365},     // BS2 = 131,365
    {1076, 4095-1422},    // BS3 = 1076,1422
    {2774, 4095-2665},    // BS4 = 2774,2665
    {3849, 4095-4095},    // BS5 = 3849,4095
};

// memcpy() appears to move data by bytes, which doesn't work with the word-accessed backup registers
// here we explicitly move data a word at a time
void memcpyword(volatile uint32_t *dest, volatile uint32_t *src, size_t n){
  while (n--){
    dest[n] = src[n];
  }
}

// The USB serial port, for every section that writes a sentence or a report to the host.
// usbd_cdc_if.h isn't pulled into main.c; forward-declare the one symbol we need.
extern uint8_t CDC_Copy_Transmit(uint8_t* buf, uint16_t Len);
extern USBD_HandleTypeDef hUsbDeviceFS;

// ---- The rest of the application, in sections --------------------------------------------------
// main.c used to hold all of it in one 7,500-line file. It is still ONE translation unit: each section
// is included here once, in dependency order, and may use anything declared above it. A section owns
// its own state; what several sections share is declared above this list. The few calls that go UP
// the list (a handler calling sendDate, the store putting a note on the row through the menu) use the
// prototypes in main.h and in the PFP block near the top of this file. See main_parts/README.md.
#include "main_parts/watchdog.h"            // the independent watchdog and its boot-loop guard
#include "main_parts/diag.h"                // the $PMLOOP loop-latency marks and the $PMBRIT report state
#include "main_parts/sensors.h"             // USB power, the backup cell voltage, the die temperature
#include "main_parts/rtc_cal.h"             // calibrating the 32 kHz RTC crystal against GPS
#include "main_parts/zones.h"               // timezone rules from TZRULES.BIN, and the second zone's offset
#include "main_parts/astro_rows.h"          // the sun, moon, grid and star-transit rows: what the main loop computes for them
#include "main_parts/time_row.h"            // the big digits: the next second, countdown, the sidereal / solar / second-zone timebases, the RTC write
#include "main_parts/gps.h"                 // the NMEA sentences the clock reads: RMC for time and position, GSV for satellites in view
#include "main_parts/scan.h"                // the display scan: its length, the per-digit dimming inputs, and the per-segment balance maths (seg_balance)
#include "main_parts/cuckoo.h"              // scheduled flourishes on the time row
#include "main_parts/scan_compose.h"        // the display on and off, and the balance, the fade and the cuckoo composed into the scan
#include "main_parts/matrix_and_colons.h"   // the scan rate, and the colon animations and their brightness
#include "main_parts/adev.h"                // the Allan and Hadamard deviation engine
#include "main_parts/pps.h"                 // the PPS edge: capture, the three edge handlers, and the $PMTXTS and $PMBRIT sentences
#include "main_parts/tempcomp.h"            // learning the oscillators against temperature: samples, fits, seeds, the models
#include "main_parts/freq_hold.h"           // the oscillator's measured offset and learned wander
#include "main_parts/governor.h"            // once a second: steer the timebase in holdover, trim the RTC
#include "main_parts/fade.h"                // the significance fade: the bound on the display's time error in a holdover
#include "main_parts/ticks.h"               // the 1 ms SysTick handlers: the sub-second digits and the .900 preparation of the next second
#include "main_parts/modes.h"               // which digits may be shown (the precision ladder), changing display mode, the buttons
#include "main_parts/date_row.h"            // sendDate: every display mode formatted for the date board, in its own C-library context
#include "main_parts/dumps.h"               // the serial reports: tc_dump, $PMADEV, $PMHDEV, $PMSTAR
#include "main_parts/store.h"               // SETTINGS.BIN: the menu settings store and the tempcomp store
#include "main_parts/menu.h"                // the two-button menu
#include "main_parts/config.h"              // config.txt and serial config lines: the parser, the defaults table, the file loader
#include "main_parts/config_write.h"        // the clock writes its menu choices into config.txt itself (on by default)
#include "main_parts/housekeeping.h"        // the timing subsystems' once-per-pass work in the main loop

void generateDACbuffer(uint16_t * buf) {

  static float dac_last=4095;


  if (displayMode == MODE_STANDBY) {
    dac_target = dac_target*0.7 + 1.2*4095.0*0.3;
    if (dac_target>4094.0) {
      dac_target=4095.0;
      displayOff();
    }
  } else if (config.brightness_override >=0.0) {
    dac_target = config.brightness_override;
  } else {
    float adc = (float)ADC1->DR;

    uint8_t i;
    for (i=1; i< sizeof(brightnessCurve)/sizeof(brightnessCurve[0]) -1; i++){
      if (brightnessCurve[i].in > adc) break;
    }
    float factor = (adc - brightnessCurve[i-1].in) / (brightnessCurve[i].in - brightnessCurve[i-1].in);

    float out = brightnessCurve[i-1].out*(1.0-factor) + brightnessCurve[i].out*factor;

    if (out>4095.0 || !isfinite(out)) out=4095.0;
    else if (out<0.0) out=0.0;

    dac_target = dac_target*0.5 + out*0.5;
  }


  HAL_ADC_Start(&hadc1);



  float step = (dac_target-dac_last)/(DAC_BUFFER_SIZE*0.5);
  for (size_t i=0; i<DAC_BUFFER_SIZE/2; i++) {
    buf[i]= (uint16_t)(dac_last += step);
  }
  dac_last=dac_target;

  if (displayMode == MODE_DEBUG_BRIGHTNESS && decisec!=9) {
    sendDate(1);
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  memcpy(__VECTORS_RAM, __VECTORS_FLASH, 0x188);
  SCB->VTOR = (uint32_t)&__VECTORS_RAM;

  SetSysTick( &SysTick_Dummy );


  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  // Enable the DWT cycle counter (free-running at the 80 MHz core clock, 12.5 ns/tick, wraps ~53.7 s):
  // the monotonic timebase the PPS edge and each USB SOF are both latched against for host correlation.
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
  adev_reset();   // clear the (un-zeroed) RAM2 Allan-deviation ring before the first PPS sample

  buffer_c[0].high=0b11001110;
  buffer_c[1].high=0b11001101;
  buffer_c[2].high=0b11001011;
  buffer_c[3].high=0b11000111;
  buffer_c[4].high=0b11001111;

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_QUADSPI_Init();
  MX_TIM1_Init();
  matrix_freq_hz = 16000000u / (TIM1->ARR + 1u);   // §4: reconcile the MATRIX shadow with the real timer (CubeMX default ARR) so g_matrix never reports a rate the hardware isn't running before config load
  MX_USART2_UART_Init();
  MX_FATFS_Init();
  //MX_USB_DEVICE_Init();
  MX_USART1_UART_Init();
  MX_TIM2_Init();
  MX_ADC1_Init();
  MX_DAC1_Init();
  MX_TIM6_Init();
  MX_TIM7_Init();
  MX_CRC_Init();
  MX_LPTIM1_Init();
  MX_TIM5_Init();
  /* USER CODE BEGIN 2 */


  // Configure display matrix
  if (HAL_DMA_Start(&hdma_tim7_up, (uint32_t)buffer_c, (uint32_t)&GPIOC->ODR, 5) != HAL_OK)
    Error_Handler();

  if (HAL_DMA_Start(&hdma_tim1_up, (uint32_t)buffer_b, (uint32_t)&GPIOB->ODR, 5) != HAL_OK)
    Error_Handler();

  __HAL_TIM_ENABLE_DMA(&htim1, TIM_DMA_UPDATE);
  __HAL_TIM_ENABLE(&htim1);

  __HAL_TIM_ENABLE_DMA(&htim7, TIM_DMA_UPDATE);
  __HAL_TIM_ENABLE(&htim7);


  doDateUpdate();
  MX_USB_DEVICE_Init();

  // Enable UART2 interrupt for button presses
  USART2->CR1 |= USART_CR1_RXNEIE;


  // Configure UART1 for NMEA strings from GPS module
  USART1->CR1 |= USART_CR1_CMIE ;

  USART1->CR1 &= ~(USART_CR1_UE);
  USART1->CR2 |= '\n'<<24;
  USART1->CR1 |= USART_CR1_UE;


  MX_ADC3_Init();

  // Configure ADC and DAC DMA for display brightness
  HAL_ADC_Start(&hadc1);
  HAL_TIM_Base_Start(&htim6);

  if (HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1, (uint32_t*)buffer_dac, DAC_BUFFER_SIZE, DAC_ALIGN_12B_R) !=HAL_OK)
    Error_Handler();

  // Configure Colon Separators
  TIM2->CCR1 = 0;
  TIM2->CCR2 = 0;

  //loadColonAnimation();

  __HAL_TIM_ENABLE_DMA(&htim5, TIM_DMA_CC1 | TIM_DMA_CC2);
  __HAL_TIM_ENABLE(&htim5);

  //colonAnimationStart()


  //Enable DP for subseconds
  buffer_c[0].high=0b11001110 | cSegDP;



  buffer_c[0].low=cSegDecode0;
  buffer_c[1].low=cSegDecode0;
  buffer_c[2].low=cSegDecode0;
  buffer_c[3].low=cSegDecode0;

  next7seg.c = buffer_c[0].low;

  next7seg.b[0] = buffer_b[0] = bCat0 | bSegDecode0;
  next7seg.b[1] = buffer_b[1] = bCat1 | bSegDecode0;
  next7seg.b[2] = buffer_b[2] = bCat2 | bSegDecode0;
  next7seg.b[3] = buffer_b[3] = bCat3 | bSegDecode0;
  next7seg.b[4] = buffer_b[4] = bCat4 | bSegDecode0;

  //setDisplayPWM(5);
  displayOn();

  ee_load();          // scan the emulated-EEPROM into the override store before config.txt is read
  ee2_load();         // scan the retained tempco model (separate 2-page store); readConfigFile picks it up as a seed
  readConfigFile();
  checkDelayedLoadRules();
  loadStars();          // scan /STARS.BIN into the transit catalogue (star_max_mag is known now); no file -> empty catalogue, "STAr ----"

  measure_vbat();

  if (RTC->ISR & RTC_ISR_INITS) //RTC contains non-zero data
  {
    RTC_DateTypeDef sdate;
    RTC_TimeTypeDef stime;

    if (!config.zone_override){
      char zone[32];
      memcpyword( (uint32_t*)zone,  (uint32_t*)&(RTC->BKP0R), 8 );
      zone[31]=0;

      if (loadRulesSingle(zone) != RULES_OK){ // takes ~8ms
        memcpyword( (uint32_t*)loadedRulesString,  (uint32_t*)&(RTC->BKP0R), 8 );
        loadedRulesString[31]=0;//paranoia
        memcpyword( (uint32_t*)rules, (uint32_t*)&(RTC->BKP8R), 22 );
      }
    }


    hrtc.Instance = RTC;
    HAL_RTC_GetTime(&hrtc, &stime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sdate, RTC_FORMAT_BIN);

    struct tm out;

    out.tm_isdst = 0;

    out.tm_sec = stime.Seconds;
    out.tm_min = stime.Minutes;
    out.tm_hour = stime.Hours;
    out.tm_mday = sdate.Date;
    out.tm_mon = sdate.Month -1;
    out.tm_year = sdate.Year + 100; //Years since 1900

    currentTime = mktime(&out);

    float fraction = (float)(32767 - stime.SubSeconds) / 32768.0;

    //  SysTick->VAL = SysTick->LOAD; ?
    millisec = (uint32_t)(fraction*1000) % 10;
    centisec = (uint32_t)(fraction*100) % 10;
    decisec =  (uint32_t)(fraction*10) % 10;

    if (decisec>=9) currentTime++;

    setNextTimestamp( currentTime );
    sendDate(1);
    latchSegments();

    // As the coin cell goes flat, the RTC stops ticking long before the backup registers die.
    // Powering on with a flat battery means the clock thinks no time has passed, and assumes it has good precision.
    // Explicitly stop this by checking the battery voltage.
    if (vbat > 2.70) {
      rtc_good=1;
    } else {
      // trash the calibration time to ensure lowest precision display
      if (currentTime - rtc_last_calibration < config.tolerance_100ms)
        rtc_last_calibration -= config.tolerance_100ms +1;
    }

  } else { // backup domain reset

    currentTime=946684800; // 2000-01-01T00:00:00

    // The init process blanks the subsecond registers
    MX_RTC_Init();
  }

  vbat = 0.0; // don't allow measurement to go stale

  setPrecision();
  PPS_Init();
  HAL_UART_Receive_DMA(&huart1, nmea, sizeof(nmea));

  wdt_boot_hw();   // was this boot a watchdog reset? (counts it; the watchdog itself arms in the loop)

//#define MEASURE_LOOKUP_TIME

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    wdt_poll();      // arm (if watchdog = on) and refresh the independent watchdog: the ONLY refresh
    LP_MARK(1);      // $PMLOOP section marks — see main_parts/diag.h
    if (loop_diag && (uint32_t)(uwTick - pmloop_win) >= 1000u) {
      pmloop_win = uwTick;
      char pl[40];
      int pn = sprintf(pl, "$PMLOOP,%lu,%u\r\n", (unsigned long)pmloop_max, (unsigned)pmloop_maxtag);
      __disable_irq(); CDC_Copy_Transmit((uint8_t*)pl, (uint16_t)pn); __enable_irq();   // lossy: diag
      pmloop_max = 0;
    }
    menu_poll();     // on-device 2-button menu FSM (drains the ISR event ring; idle-auto-exits)
    LP_MARK(2);
    cuckoo_poll();   // cuckoo scheduler + 10 ms tick — writes ck_bright BEFORE segbal composes it
    segbal_poll();   // per-segment brightness balance (seg_balance) — refills the mirror slots, ≤1 kHz
    colon_balance_poll();   // dim the colons with the rail (colon_balance) — reloads the anim buffer on change
    brightness_report_poll();   // $PMBRIT once a second (brightness_report) — the dimmer's operating point

    LP_MARK(3);
    // Distance gate: skip the ~300 ms FATFS/ZoneDetect lookup unless the fix has actually moved far
    // enough to plausibly change zone. 0.005° ≈ 0.5 km — ~100× the metre-scale jitter of a stationary
    // clock, yet far finer than any timezone boundary, so a moving clock still re-detects its zone
    // within ~0.5 km of a crossing while a still one looks up exactly once. (dlat²+dlon² threshold
    // 2.5e-5 = 0.005²; the cos-lat foreshortening of longitude only makes the gate MORE conservative.)
    if (new_position && !qspi_write_time && !config.zone_override
        && (data_valid || (config.fake_long && config.fake_lat))
        && latitude>=-90.0 && latitude<=90.0 && longitude>=-180.0 && longitude<=180.0
        && (zone_lat>90.0 || (latitude-zone_lat)*(latitude-zone_lat) + (longitude-zone_lon)*(longitude-zone_lon) > 2.5e-5)) {

      new_position=0;
      zone_lat=latitude; zone_lon=longitude;
      fatfs_busy=1;   // map lookup + loadRulesSingle touch FATFS; block the eject-time check
      FIL mapfile;
      if (f_open(&mapfile, MAP_FILENAME, FA_READ) == FR_OK) {
#ifdef MEASURE_LOOKUP_TIME
        uint32_t start=uwTick;
#endif
        ZoneDetect *const zdb = ZDOpenDatabase(&mapfile);

        if (!zdb) {
          // mapfile error
        } else {
          char* zone = ZDHelperSimpleLookupString(zdb, latitude, longitude);
#ifdef MEASURE_LOOKUP_TIME
          uint32_t ztime=uwTick-start;
#endif
          if (zone && !delayedLoadRules) {
#ifdef MEASURE_LOOKUP_TIME
            start=uwTick;
#endif
            loadRulesSingle(zone);
#ifdef MEASURE_LOOKUP_TIME
            sprintf(textDisplay,"d%ld L%ld",ztime, uwTick-start);
#endif
          }
          free(zone);
          ZDCloseDatabase(zdb);
          //f_close(&mapfile);
        }
      }
      // else no_map = 1
      fatfs_busy=0;
    }

    LP_MARK(4);
    if (delayedCheckOnEject) firmwareCheckOnEject();

    if (delayedPostConfigCleanup) {
      delayedPostConfigCleanup=0;
      postConfigCleanup();
      // tempcomp seeding/freeze-guard runs from tc_housekeeping (same pass), keyed by
      // tc_seed_pending / tc_seed_done — one place, serialized with tc_fit/tc_governor.
    }

    fatfs_busy=1;   // FATFS_remount + readConfigFile + checkDelayedLoadRules touch FATFS
    if (delayedReadConfigFile) {
      FATFS_remount();
      readConfigFile();
      delayedReadConfigFile=0;
    }

    checkDelayedLoadRules();
    fatfs_busy=0;

    if (delayedDisplayFreq) setDisplayFreq(delayedDisplayFreq);

    LP_MARK(5);
    monitor_vbus();

    // significance_fade is a die-temp consumer too: computeHoldoverFade charges an out-of-coverage
    // penalty from die_temp_c, which would otherwise stay at its init 0 with every other flag off.
    if (pps_ts_enabled || tc_learn || tc_apply || tc_rtc || significance_fade || displayMode == MODE_TEMPCOMP) {
      static uint32_t last_temp_read = 0;
      if ((uint32_t)currentTime - last_temp_read >= 4) {   // refresh die temp every ~4 s
        last_temp_read = (uint32_t)currentTime;
        measure_temp();
      }
    }
    LP_MARK(6);
    if (pps_ts_enabled && pps_record_pending) emitPPSTimestamp(); // emit clears pending itself on success

    tc_housekeeping();   // temp-comp learn/steer/dump; four flag checks when everything is off
    adev_dump_step();    // one-shot $PMADEV emit when adev_dump was set over serial (else 1 flag check)
    hdev_dump_step();    // one-shot $PMHDEV (Hadamard) twin
    star_dump_step();    // one-shot $PMSTAR emit when star_dump was set over serial (else 1 flag check)
    wdt_dump_step();     // one-shot watchdog report when wdt_dump was set over serial
    factory_reset_step();   // one-shot menu factory-reset when factory_reset was set over serial
    tc_persist_step();   // gated commit of the learned tempco model to its retained flash store
    tc_forget_step();    // one-shot erase of the retained model when tc_reset/tc_forget fired

    LP_MARK(7);
    if (displayMode == MODE_VBAT)
      measure_vbat();

    if (displayMode == MODE_SUN  || displayMode == MODE_SUN_AZEL || displayMode == MODE_MOON
        || displayMode == MODE_GRID || displayMode == MODE_LATLON || displayMode == MODE_DARK) {
      astro_update();
      // honour the ms page dwell: the date row otherwise only repaints at 1 Hz, so
      // repaint the moment a paged mode flips sub-screen. Only with a fix (no-fix shows
      // a page-independent "----"), and never in the last decisecond -- there the SysTick
      // ISR runs its own (non-reentrant, shared-UART) sendDate(0), so we'd race it. Same
      // decisec!=9 guard the existing main-loop sendDate(1) calls use. last_pg is left
      // unchanged when skipped, so the flip just shows on the next loop (<=100 ms later).
      LP_MARK(8);
    if ((displayMode == MODE_SUN || displayMode == MODE_LATLON || displayMode == MODE_DARK) && astro.have_pos && astro.epoch) {
        static uint32_t last_pg = 0;
        uint32_t pg = uwTick / page_ms();
        if (pg != last_pg && decisec != 9) { last_pg = pg; sendDate(1); }
      }
    }

    // MODE_TEMPCOMP pages on the same dwell: repaint on the page flip (same guard as above)
    if (displayMode == MODE_TEMPCOMP) {
      static uint32_t tc_last_pg = 0;
      uint32_t pg = uwTick / page_ms();
      if (pg != tc_last_pg && decisec != 9) { tc_last_pg = pg; sendDate(1); }
    }

    // MODE_ADEV pages the octave taus on the same dwell. Recompute the octave cache on each flip
    // (thread context, ~sub-ms) so the shown sigma tracks the growing series, then repaint. The
    // 0xFFFFFFFF sentinel forces a reduce+paint on first entry rather than waiting a full dwell.
    if (displayMode == MODE_ADEV) {
      static uint32_t adev_last_pg = 0xFFFFFFFFu;
      uint32_t pg = uwTick / page_ms();
      if (pg != adev_last_pg && decisec != 9) { adev_last_pg = pg; adev_display_update(); sendDate(1); }
    }

    // MODE_STAR: recompute the soonest-transit list once a second (the countdown ticks off the cached
    // epoch every 1 Hz sendDate; re-sorting keeps the order fresh as stars culminate), repaint on flip.
    if (displayMode == MODE_STAR) {
      static uint32_t star_last_sec = 0xFFFFFFFFu, star_last_pg = 0xFFFFFFFFu;
      if ((uint32_t)currentTime != star_last_sec) { star_last_sec = (uint32_t)currentTime; star_update(); }
      uint32_t pg = uwTick / page_ms();
      if (pg != star_last_pg && decisec != 9) { star_last_pg = pg; sendDate(1); }
    }

    // MODE_LST / MODE_SOLAR / MODE_ZONE2: stage the next civil boundary's alternate reading
    // (thread-context doubles; no-op in every other mode)
    LP_MARK(9);
    alt_update();

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);
  /** Initializes the CPU, AHB and APB busses clocks
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE|RCC_OSCILLATORTYPE_LSE
                              |RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_11;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 2;
  RCC_OscInitStruct.PLL.PLLN = 64;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB busses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC|RCC_PERIPHCLK_USART1
                              |RCC_PERIPHCLK_USART2|RCC_PERIPHCLK_LPTIM1
                              |RCC_PERIPHCLK_USB|RCC_PERIPHCLK_ADC;
  PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK2;
  PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
  PeriphClkInit.Lptim1ClockSelection = RCC_LPTIM1CLKSOURCE_LSE;
  PeriphClkInit.AdcClockSelection = RCC_ADCCLKSOURCE_SYSCLK;
  PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_MSI;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }
  /** Enable MSI Auto calibration
  */
  HAL_RCCEx_EnableMSIPLLMode();
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_MultiModeTypeDef multimode = {0};
  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */
  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.NbrOfDiscConversion = 1;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }
  /** Configure the ADC multi-mode
  */
  multimode.Mode = ADC_MODE_INDEPENDENT;
  if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK)
  {
    Error_Handler();
  }
  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_10;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_92CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief ADC3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC3_Init(void)
{

  /* USER CODE BEGIN ADC3_Init 0 */

  /* USER CODE END ADC3_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC3_Init 1 */


  /* USER CODE END ADC3_Init 1 */
  /** Common config
  */
  hadc3.Instance = ADC3;
  hadc3.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV2;
  hadc3.Init.Resolution = ADC_RESOLUTION_12B;
  hadc3.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc3.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc3.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc3.Init.LowPowerAutoWait = DISABLE;
  hadc3.Init.ContinuousConvMode = DISABLE;
  hadc3.Init.NbrOfConversion = 1;
  hadc3.Init.DiscontinuousConvMode = DISABLE;
  hadc3.Init.NbrOfDiscConversion = 1;
  hadc3.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc3.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc3.Init.DMAContinuousRequests = DISABLE;
  hadc3.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc3.Init.OversamplingMode = ENABLE;
  hadc3.Init.Oversampling.Ratio = ADC_OVERSAMPLING_RATIO_16;
  hadc3.Init.Oversampling.RightBitShift = ADC_RIGHTBITSHIFT_4;
  hadc3.Init.Oversampling.TriggeredMode = ADC_TRIGGEREDMODE_SINGLE_TRIGGER;
  hadc3.Init.Oversampling.OversamplingStopReset = ADC_REGOVERSAMPLING_RESUMED_MODE;

  if (HAL_ADC_Init(&hadc3) != HAL_OK)
  {
    Error_Handler();
  }

  HAL_ADCEx_Calibration_Start(&hadc3, ADC_SINGLE_ENDED);

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_VBAT;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_640CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc3, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC3_Init 2 */
  ADC123_COMMON->CCR &= ~ADC_CCR_VBATEN;
  /* USER CODE END ADC3_Init 2 */

}

/**
  * @brief CRC Initialization Function
  * @param None
  * @retval None
  */
static void MX_CRC_Init(void)
{

  /* USER CODE BEGIN CRC_Init 0 */

  /* USER CODE END CRC_Init 0 */

  /* USER CODE BEGIN CRC_Init 1 */

  /* USER CODE END CRC_Init 1 */
  hcrc.Instance = CRC;
  hcrc.Init.DefaultPolynomialUse = DEFAULT_POLYNOMIAL_ENABLE;
  hcrc.Init.DefaultInitValueUse = DEFAULT_INIT_VALUE_ENABLE;
  hcrc.Init.InputDataInversionMode = CRC_INPUTDATA_INVERSION_BYTE;
  hcrc.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_ENABLE;
  hcrc.InputDataFormat = CRC_INPUTDATA_FORMAT_WORDS;
  if (HAL_CRC_Init(&hcrc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CRC_Init 2 */

  /* USER CODE END CRC_Init 2 */

}

/**
  * @brief DAC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_DAC1_Init(void)
{

  /* USER CODE BEGIN DAC1_Init 0 */

  /* USER CODE END DAC1_Init 0 */

  DAC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN DAC1_Init 1 */

  /* USER CODE END DAC1_Init 1 */
  /** DAC Initialization
  */
  hdac1.Instance = DAC1;
  if (HAL_DAC_Init(&hdac1) != HAL_OK)
  {
    Error_Handler();
  }
  /** DAC channel OUT1 config
  */
  sConfig.DAC_SampleAndHold = DAC_SAMPLEANDHOLD_DISABLE;
  sConfig.DAC_Trigger = DAC_TRIGGER_T6_TRGO;
  sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
  sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_DISABLE;
  sConfig.DAC_UserTrimming = DAC_TRIMMING_FACTORY;
  if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DAC1_Init 2 */
  HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1,  DAC_ALIGN_12B_R, 4095);
  /* USER CODE END DAC1_Init 2 */

}

/**
  * @brief LPTIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_LPTIM1_Init(void)
{

  /* USER CODE BEGIN LPTIM1_Init 0 */

  /* USER CODE END LPTIM1_Init 0 */

  /* Peripheral clock enable */
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_LPTIM1);

  /* LPTIM1 interrupt Init */
  NVIC_SetPriority(LPTIM1_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(),1, 0));
  NVIC_EnableIRQ(LPTIM1_IRQn);

  /* USER CODE BEGIN LPTIM1_Init 1 */

  /* USER CODE END LPTIM1_Init 1 */
  LL_LPTIM_SetClockSource(LPTIM1, LL_LPTIM_CLK_SOURCE_INTERNAL);
  LL_LPTIM_SetPrescaler(LPTIM1, LL_LPTIM_PRESCALER_DIV1);
  LL_LPTIM_SetPolarity(LPTIM1, LL_LPTIM_OUTPUT_POLARITY_REGULAR);
  LL_LPTIM_SetUpdateMode(LPTIM1, LL_LPTIM_UPDATE_MODE_IMMEDIATE);
  LL_LPTIM_SetCounterMode(LPTIM1, LL_LPTIM_COUNTER_MODE_INTERNAL);
  LL_LPTIM_TrigSw(LPTIM1);
  LL_LPTIM_SetInput1Src(LPTIM1, LL_LPTIM_INPUT1_SRC_GPIO);
  LL_LPTIM_SetInput2Src(LPTIM1, LL_LPTIM_INPUT2_SRC_GPIO);
  /* USER CODE BEGIN LPTIM1_Init 2 */

  LL_LPTIM_Enable(LPTIM1);
  LL_LPTIM_SetAutoReload(LPTIM1, 0xFFFF);
  LL_LPTIM_EnableIT_ARRM(LPTIM1);

  /* USER CODE END LPTIM1_Init 2 */

}

/**
  * @brief QUADSPI Initialization Function
  * @param None
  * @retval None
  */
static void MX_QUADSPI_Init(void)
{

  /* USER CODE BEGIN QUADSPI_Init 0 */

  /* USER CODE END QUADSPI_Init 0 */

  /* USER CODE BEGIN QUADSPI_Init 1 */

  /* USER CODE END QUADSPI_Init 1 */
  /* QUADSPI parameter configuration*/
  hqspi.Instance = QUADSPI;
  hqspi.Init.ClockPrescaler = 0;
  hqspi.Init.FifoThreshold = 4;
  hqspi.Init.SampleShifting = QSPI_SAMPLE_SHIFTING_HALFCYCLE;
  hqspi.Init.FlashSize = 23;
  hqspi.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_1_CYCLE;
  hqspi.Init.ClockMode = QSPI_CLOCK_MODE_0;
  if (HAL_QSPI_Init(&hqspi) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN QUADSPI_Init 2 */

  /* USER CODE END QUADSPI_Init 2 */

}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */
  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 0;
  hrtc.Init.SynchPrediv = 32759;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */

  // RM page 1236
  __HAL_RTC_WRITEPROTECTION_DISABLE(&hrtc);
  RTC->CALR = 0x100; // CALM to midpoint
  __HAL_RTC_WRITEPROTECTION_ENABLE(&hrtc);

  /* USER CODE END RTC_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 0;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 256;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 8;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 10000;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_OC2REF;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM2;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */
  HAL_TIM_MspPostInit(&htim2);

}

/**
  * @brief TIM5 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM5_Init(void)
{

  /* USER CODE BEGIN TIM5_Init 0 */

  /* USER CODE END TIM5_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM5_Init 1 */

  /* USER CODE END TIM5_Init 1 */
  htim5.Instance = TIM5;
  htim5.Init.Prescaler = 7999;
  htim5.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim5.Init.Period = 99;
  htim5.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim5.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim5) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim5, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_OC_Init(&htim5) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim5, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_TIMING;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_OC_ConfigChannel(&htim5, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_OC_ConfigChannel(&htim5, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM5_Init 2 */

  /* USER CODE END TIM5_Init 2 */

}

/**
  * @brief TIM6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM6_Init(void)
{

  /* USER CODE BEGIN TIM6_Init 0 */

  /* USER CODE END TIM6_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM6_Init 1 */

  /* USER CODE END TIM6_Init 1 */
  htim6.Instance = TIM6;
  htim6.Init.Prescaler = 8000;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 100;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM6_Init 2 */

  /* USER CODE END TIM6_Init 2 */

}

/**
  * @brief TIM7 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM7_Init(void)
{

  /* USER CODE BEGIN TIM7_Init 0 */

  /* USER CODE END TIM7_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM7_Init 1 */

  /* USER CODE END TIM7_Init 1 */
  htim7.Instance = TIM7;
  htim7.Init.Prescaler = 0;
  htim7.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim7.Init.Period = 256;
  htim7.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim7) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim7, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM7_Init 2 */

  /* USER CODE END TIM7_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 9600;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_9B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_EVEN;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_RXOVERRUNDISABLE_INIT;
  huart2.AdvancedInit.OverrunDisable = UART_ADVFEATURE_OVERRUN_DISABLE;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel3_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel3_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);
  /* DMA1_Channel4_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
  /* DMA1_Channel5_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);
  /* DMA1_Channel6_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel6_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel6_IRQn);
  /* DMA1_Channel7_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel7_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel7_IRQn);
  /* DMA2_Channel4_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Channel4_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Channel4_IRQn);
  /* DMA2_Channel5_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Channel5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Channel5_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13|GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2
                          |GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6
                          |GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11
                          |GPIO_PIN_12, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2|GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14
                          |GPIO_PIN_15|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5
                          |GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9, GPIO_PIN_RESET);

  /*Configure GPIO pins : PC13 PC0 PC1 PC2
                           PC3 PC4 PC5 PC6
                           PC8 PC9 PC10 PC11
                           PC12 */
  GPIO_InitStruct.Pin = GPIO_PIN_13|GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2
                          |GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6
                          |GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11
                          |GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PB2 PB12 PB13 PB14
                           PB15 PB3 PB4 PB5
                           PB6 PB7 PB8 PB9 */
  GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14
                          |GPIO_PIN_15|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5
                          |GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : PA8 */
  GPIO_InitStruct.Pin = GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */

  __disable_irq();

  buffer_c[0].high=0b11011110;
  buffer_c[1].high=0b11011101;
  buffer_c[2].high=0b11011011;
  buffer_c[3].high=0b11010111;
  buffer_c[4].high=0b11001111;
  buffer_c[0].low=0b01010000;
  buffer_c[1].low=0b01010000;
  buffer_c[2].low=0b01011100;
  buffer_c[3].low=0b01010000;
  buffer_c[4].low=0;

  buffer_b[0] = bCat0;
  buffer_b[1] = bCat1;
  buffer_b[2] = bCat2;
  buffer_b[3] = bCat3;
  buffer_b[4] = bCat4 | 0b0111100100;

  //setDisplayPWM(5);




  while(1);
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
