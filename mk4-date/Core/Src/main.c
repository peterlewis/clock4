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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <math.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

#define byteswap32(x) \
   ( ((x & 0xff000000) >> 24) | ((x & 0x00ff0000) >> 8) \
   | ((x & 0x0000ff00) <<  8) | ((x & 0x000000ff) << 24))

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */


const uint8_t lut_7seg[] = {
  0,
  64, // !
  0b00100010,// "
  64, // #
  64, // $
  64, // %
  64, // &
  0b00000010,// '
  0b00111001,// (
  0b00001111,// )
  64, // *
  64, // +
  64, // ,
  0b01000000,// -
  64, // .
  0b01010010,// /
  0b00111111,// 0
  0b00000110,// 1
  0b01011011,// 2
  0b01001111,// 3
  0b01100110,// 4
  0b01101101,// 5
  0b01111101,// 6
  0b00000111,// 7
  0b01111111,// 8
  0b01101111,// 9
  64, // :
  64, // ;
  64, // <
  64, // =
  64, // >
  64, // ?
  64, // @
  0b01110111,// A
  0b01111100,// B
  0b00111001,// C
  0b01011110,// D
  0b01111001,// E
  0b01110001,// F
  0b00111101,// G
  0b01110100,// H
  0b00000110,// I
  0b00011110,// J
  0b01110101,// K
  0b00111000,// L
  0b00010101,// M
  0b01010100,// N
  0b00111111,// O
  0b01110011,// P
  0b01100111,// Q
  0b01010000,// R
  0b01101101,// S
  0b01111000,// T
  0b00111110,// U
  0b01100010,// V
  0b00101010,// W
  0b01110110,// X
  0b01101110,// Y
  0b01011011,// Z
  0b00111001,// [
  0b01100100,// \ //
  0b00001111,// ]
  0b00100011,// ^
  0b00001000,// _
  0b00100000,// `
  0b01110111,// a
  0b01111100,// b
  0b01011000,// c
  0b01011110,// d
  0b01111001,// e
  0b01110001,// f
  0b00111101,// g
  0b01110100,// h
  0b00000100,// i
  0b00011110,// j
  0b01110101,// k
  0b00111000,// l
  0b01010101,// m
  0b01010100,// n
  0b01011100,// o
  0b01110011,// p
  0b01100111,// q
  0b01010000,// r
  0b01101101,// s
  0b01111000,// t
  0b00011100,// u
  0b01100010,// v
  0b01101010,// w
  0b01110110,// x
  0b01101110,// y
  0b01011011,// z
  0b00111001,// {
  64, // |
  0b00001111,// }
  64, // ~
};

const uint8_t lut_7seg_inv[] = {
  0,
  64, // !
  0b00010100,// "
  64, // #
  64, // $
  64, // %
  64, // &
  0b00010000,// '
  0b00001111,// (
  0b00111001,// )
  64, // *
  64, // +
  64, // ,
  0b01000000,// -
  64, // .
  0b01010010,// /
  0b00111111,// 0
  0b00110000,// 1
  0b01011011,// 2
  0b01111001,// 3
  0b01110100,// 4
  0b01101101,// 5
  0b01101111,// 6
  0b00111000,// 7
  0b01111111,// 8
  0b01111101,// 9
  64, // :
  64, // ;
  64, // <
  64, // =
  64, // >
  64, // ?
  64, // @
  0b01111110,// A
  0b01100111,// B
  0b00001111,// C
  0b01110011,// D
  0b01001111,// E
  0b01001110,// F
  0b00101111,// G
  0b01100110,// H
  0b00110000,// I
  0b00110011,// J
  0b01101110,// K
  0b00000111,// L
  0b00101010,// M
  0b01100010,// N
  0b00111111,// O
  0b01011110,// P
  0b01111100,// Q
  0b01000010,// R
  0b01101101,// S
  0b01000111,// T
  0b00110111,// U
  0b01010100,// V
  0b00010101,// W
  0b01110110,// X
  0b01110101,// Y
  0b01011011,// Z
  0b00001111,// [
  0b01100100,// \ //
  0b00111001,// ]
  0b00011100,// ^
  0b00000001,// _
  0b00000100,// `
  0b01111110,// a
  0b01100111,// b
  0b01000011,// c
  0b01110011,// d
  0b01001111,// e
  0b01001110,// f
  0b00101111,// g
  0b01100110,// h
  0b00100000,// i
  0b00110011,// j
  0b01101110,// k
  0b00000111,// l
  0b01101010,// m
  0b01100010,// n
  0b01100011,// o
  0b01011110,// p
  0b01111100,// q
  0b01000010,// r
  0b01101101,// s
  0b01000111,// t
  0b00100011,// u
  0b01010100,// v
  0b01010101,// w
  0b01110110,// x
  0b01110101,// y
  0b01011011,// z
  0b00001111,// {
  64, // |
  0b00111001,// }
  64, // ~
};

#define CMD_LOAD_TEXT          0x90
#define CMD_SET_FREQUENCY      0x91
#define CMD_RELOAD_TEXT        0x92
#define CMD_SET_SCROLL_SPEED   0x93
#define CMD_GEM                0x94   // +1 data byte: 0 = off, 1 = larson, 2 = dim-only
#define CMD_COL_LEVELS         0x95   // +10 data bytes: column brightness 0..15, viewer left->right

#define CMD_SHOW_CRC           0x9D
#define CMD_REPORT_CRC         0x9E
#define CMD_START_BOOTLOADER   0x9F

#define CMD_SET_FREQUENCY_B2   0xA1
#define CMD_SET_FREQUENCY_B3   0xA2

// 32e6/5/50 = 128000 Hz
// on -O0, ARR_MIN 66 => 95522.388Hz
#define ARR_MIN                49
#define ARR_MAX                6399
// 32e6/5/6400 = 1000Hz

uint16_t pre_buffer_a[5] ={0};
uint16_t pre_buffer_b[5] ={0};
uint8_t status = 0;

uint16_t buffer_a[5] ={0};
uint16_t buffer_b[5] ={0};
uint8_t buffer_idx=0;

#define MAX_TEXT_LEN 32
uint8_t text[MAX_TEXT_LEN] ={0};
uint8_t text_idx=0;
uint8_t dp_pos=0;


uint32_t target_freq=0;

const uint16_t cathodes_a[5]={
    0b1001100000000010,
    0b1001100000000001,
    0b1001000000000011,
    0b1000100000000011,
    0b0001100000000011
};
const uint16_t cathodes_b[5]={
    0b1111000000000000,
    0b1110100000000000,
    0b1101100000000000,
    0b1011100000000000,
    0b0111100000000000
};

uint8_t inverted=0;
uint8_t b1_held =0;
uint8_t b2_held =0;

// --- display gems (opt-in effects, commanded by the time board; DISPLAY_GEMS.md A1/B1) ---
// Per-digit brightness by sigma-delta dithering in the matrix scan: each slot's segments
// are blanked or shown per scan pass so a 0..15 level becomes a duty cycle. At the default
// 20 kHz slot rate each digit refreshes at 4 kHz, so the 16-level pattern repeats at 250 Hz.
#define GEM_OFF     0
#define GEM_LARSON  1
#define GEM_DIM     2
volatile uint8_t gem_mode = GEM_OFF;
volatile uint8_t dimming  = 0;                        // fast gate for the scan ISR
volatile uint8_t dim_a[5] = {15,15,15,15,15};         // per-slot level, a-half
volatile uint8_t dim_b[5] = {15,15,15,15,15};         // per-slot level, b-half
uint8_t dim_acc_a[5], dim_acc_b[5];                   // sigma-delta accumulators (TIM2 ISR only)
uint8_t levels_idx = 0;                               // CMD_COL_LEVELS payload cursor
volatile uint8_t gem_alive = 0;                       // TIM21 ticks since the last gem command

// The dither adds ~120 cycles to the worst-case scan slot; above ~50 kHz MATRIX_FREQUENCY
// that saturates the CPU and starves the polled UART RX (including the gem-off command).
// While dimming, floor ARR at ~26 kHz matrix rate (~50% CPU worst case); restored on gem off.
#define GEM_ARR_MIN 239

// segment + DP bits per half (everything that isn't cathode select)
#define SEGMASK_A ((uint16_t)((0x7F<<4) | (1<<14)))
#define SEGMASK_B ((uint16_t)((0x7F<<4) | 1))

static void gemOff(void);   // defined after latchDisplay/setFrequency

// map a viewer column (0 = left) to its dim slot, same indexing as setDigitPre
static void setColLevel(uint8_t col, uint8_t lvl){
  if (lvl > 15) lvl = 15;
  if (inverted) {
    if (col >= 5) dim_b[9-col] = lvl; else dim_a[4-col] = lvl;
  } else {
    if (col >= 5) dim_a[col-5] = lvl; else dim_b[col] = lvl;
  }
}

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM2_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM21_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

__attribute__((naked,noreturn))
void triggerBootloader(void){


  LL_TIM_DisableIT_UPDATE(TIM2);
  LL_TIM_DisableCounter(TIM2);
  LL_TIM_DeInit(TIM2);

  LL_TIM_DisableIT_UPDATE(TIM21);
  LL_TIM_DisableCounter(TIM21);
  LL_TIM_DeInit(TIM21);

  LL_USART_Disable(USART2);
  LL_USART_DeInit(USART2);

  GPIOA->ODR=0;
  GPIOB->ODR=0;

  LL_GPIO_DeInit(GPIOA);
  LL_GPIO_DeInit(GPIOB);

  LL_APB2_GRP1_ForceReset(LL_APB2_GRP1_PERIPH_SYSCFG);
  LL_APB1_GRP1_ForceReset(LL_APB1_GRP1_PERIPH_ALL);
  LL_APB2_GRP1_ReleaseReset(LL_APB2_GRP1_PERIPH_SYSCFG);
  LL_APB1_GRP1_ReleaseReset(LL_APB1_GRP1_PERIPH_ALL);

  LL_RCC_DeInit();
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL = 0;

#define SYSMEM 0x1FF00000

  __set_MSP(*(volatile uint32_t*) SYSMEM);
  ((void (*)(void)) (*((volatile uint32_t *)(SYSMEM + 4))))();

  __builtin_unreachable();
}


void setDigitPre(uint8_t digit, uint8_t val){
  if (val<32 || val>127) val=32;

  if (inverted == 1) {
    if (digit>=5) {
      pre_buffer_b[9-digit] = (lut_7seg_inv[val-32]<<4) | cathodes_b[9-digit];
    } else {
      pre_buffer_a[9-digit-5] = (lut_7seg_inv[val-32]<<4) | cathodes_a[9-digit-5];
    }
  } else {
    if (digit>=5) {
      pre_buffer_a[digit-5] = (lut_7seg[val-32]<<4) | cathodes_a[digit-5];
    } else {
      pre_buffer_b[digit] = (lut_7seg[val-32]<<4) | cathodes_b[digit];
    }
  }
}
void setDigitDirect(uint8_t digit, uint8_t val){
  if (val<32 || val>127) val=32;

  if (inverted == 1) {
    if (digit>=5) {
      buffer_b[9-digit] = (lut_7seg_inv[val-32]<<4) | cathodes_b[9-digit];
    } else {
      buffer_a[9-digit-5] = (lut_7seg_inv[val-32]<<4) | cathodes_a[9-digit-5];
    }
  } else {
    if (digit>=5) {
      buffer_a[digit-5] = (lut_7seg[val-32]<<4) | cathodes_a[digit-5];
    } else {
      buffer_b[digit] = (lut_7seg[val-32]<<4) | cathodes_b[digit];
    }
  }
}

// Main display matrix routine
void TIM2_IRQHandler(void)
{
  if (TIM2->SR & TIM_SR_UIF){

    uint16_t a = buffer_a[buffer_idx];
    uint16_t b = buffer_b[buffer_idx];
    if (dimming){
      // sigma-delta duty per slot; level 15 short-circuits to always-on (true full)
      uint8_t d = dim_a[buffer_idx];
      if (d < 15){
        uint8_t acc = dim_acc_a[buffer_idx] + d;
        dim_acc_a[buffer_idx] = acc & 15;
        if (acc < 16) a &= (uint16_t)~SEGMASK_A;
      }
      d = dim_b[buffer_idx];
      if (d < 15){
        uint8_t acc = dim_acc_b[buffer_idx] + d;
        dim_acc_b[buffer_idx] = acc & 15;
        if (acc < 16) b &= (uint16_t)~SEGMASK_B;
      }
    }
    GPIOA->ODR = a;
    GPIOB->ODR = b;

    buffer_idx ++;
    if (buffer_idx>=5) buffer_idx=0;

    TIM2->SR = ~TIM_DIER_UIE;
    return;
  }
}

void TIM21_IRQHandler(void){
  if (TIM21->SR & TIM_SR_UIF){

    uint8_t i = (LL_GPIO_ReadInputPort(GPIOC) & 1)?1:0;

    //if (i!=inverted)...

    inverted=i;

    TIM21->SR = ~TIM_DIER_UIE;

   //if (!latched) return; //don't intervene while waiting for latch
    //if (inverted)...

#define btn_debounce 2
#define btn_delay 42
#define btn_repeat 10


    if ((LL_GPIO_ReadInputPort(GPIOB) & LL_GPIO_PIN_3)==0) {
      if (++b1_held == btn_debounce || b1_held == btn_delay) {
          if ( (USART2->ISR & USART_ISR_TXE) && b2_held==0 ) {
            USART2->TDR = 0x91 + inverted;
          }
          if (b1_held==btn_delay) b1_held -= btn_repeat;
      }
    } else b1_held=0;
    if ((LL_GPIO_ReadInputPort(GPIOC) & LL_GPIO_PIN_13)==0) {
      if (++b2_held == btn_debounce || b2_held == btn_delay) {
          if ( (USART2->ISR & USART_ISR_TXE) && b1_held==0) {
            USART2->TDR = 0x92 - inverted;
          }
          if (b2_held==btn_delay) b2_held -= btn_repeat;
      }
    } else b2_held=0;

    if (b1_held > btn_delay-btn_repeat && b2_held > btn_delay-btn_repeat){
      if ( (USART2->ISR & USART_ISR_TXE)) {
        // If triggering a reset the bootloader expects the line to be empty
        // Don't resend the command until buttons are released
        if (b1_held<btn_delay) {
          USART2->TDR = 0x93;
          // Wipe our display too
          buffer_b[0] = 0;
          buffer_b[1] = 0;
          buffer_b[2] = 0;
          buffer_b[3] = 0;
          buffer_b[4] = 0;
          buffer_a[0] = 0;
          buffer_a[1] = 0;
          buffer_a[2] = 0;
          buffer_a[3] = 0;
          buffer_a[4] = 0;
        }
        b1_held = b2_held = btn_delay+1;
      }
    }

    // Gem watchdog: the time board re-asserts the active gem at 1 Hz (CMD_GEM for the
    // larson, the levels frame for the terminator). If ~3 s pass with no re-assert —
    // time-board reboot, a truncated gem-off frame, line noise — fail safe back to text.
    // The OFF transition is otherwise a one-shot and must never be able to wedge.
    if (gem_mode != GEM_OFF && ++gem_alive >= 150){
      gemOff();
    }

    // Larson scanner (GEM_LARSON), animated locally at this timer's 50 Hz: a pip sweeps
    // the ten middle segments, sharp leading edge, stepped decay tail behind its
    // direction of travel. 70 ticks per leg = 1.4 s per sweep. Runs entirely on this
    // board — the time board only switches it on and off.
    if (gem_mode == GEM_LARSON){
      static uint8_t gtick = 0;
      if (++gtick >= 140) gtick = 0;
      uint8_t leg = gtick / 70;                       // 0 = left->right, 1 = right->left
      uint8_t x10 = (uint8_t)(((gtick % 70) * 90u) / 69u);  // pip position, tenths of a column
      if (leg) x10 = 90 - x10;
      for (uint8_t c = 0; c < 10; c++){
        int16_t d10 = leg ? ((int16_t)(c*10) - (int16_t)x10)
                          : ((int16_t)x10 - (int16_t)(c*10)); // >0 = behind the pip
        uint8_t lvl;
        if      (d10 < -5)  lvl = 0;                  // ahead of the pip
        else if (d10 <= 5)  lvl = 15;                 // the pip
        else if (d10 <= 15) lvl = 7;                  // decay tail
        else if (d10 <= 25) lvl = 3;
        else if (d10 <= 40) lvl = 1;
        else                lvl = 0;
        setDigitDirect(c, '-');                       // seg g only; also repaints over any latch
        setColLevel(c, lvl);
      }
    }
  }
}

static inline void setFrequency(void){

  if (target_freq<1 || target_freq>100000) return;

  uint32_t arr = round(6400000.0 / (float)target_freq) -1.0;
  if (arr > ARR_MAX) arr = ARR_MAX;
  if (arr < ARR_MIN) arr = ARR_MIN;
  if (dimming && arr < GEM_ARR_MIN) arr = GEM_ARR_MIN;  // keep the dithered scan ISR sane
  TIM2->ARR= arr;
}

static inline void latchDisplay(void);
// leave any gem cleanly: restore levels, scan rate and the latched text.
// gem_mode clears FIRST so the latch gate passes. Called from parseByte (thread)
// and from the TIM21 watchdog (ISR) — both write the same source data.
static void gemOff(void){
  gem_mode = GEM_OFF;
  dimming = 0;
  for (uint8_t i = 0; i < 5; i++){ dim_a[i] = 15; dim_b[i] = 15; }
  setFrequency();      // undo the GEM_ARR_MIN floor (no-op if never configured)
  latchDisplay();      // text returns immediately, not at the next 1 Hz repaint
}

static inline void latchDisplay(void){
  // While the Larson gem owns the display, keep honouring the latch handshake but skip
  // the copy — pre_buffers stay current, so leaving the gem restores the text instantly.
  if (gem_mode == GEM_LARSON) return;
  buffer_b[0] = pre_buffer_b[0];
  buffer_b[1] = pre_buffer_b[1];
  buffer_b[2] = pre_buffer_b[2];
  buffer_b[3] = pre_buffer_b[3];
  buffer_b[4] = pre_buffer_b[4];
  buffer_a[0] = pre_buffer_a[0];
  buffer_a[1] = pre_buffer_a[1];
  buffer_a[2] = pre_buffer_a[2];
  buffer_a[3] = pre_buffer_a[3];
  buffer_a[4] = pre_buffer_a[4];

  // dp_pos is the 1-based digit the decimal point attaches to (0 = none). It comes from
  // text_idx, which the sender can advance past the 10-digit display, and it indexes the
  // 5-entry buffer_a/buffer_b below. Clamp to each orientation's valid range so a stray or
  // garbled '.' in the UART stream can't drive a negative / out-of-range index into RAM.
  if (!dp_pos) return;
  if (inverted) { if (dp_pos > 9) return; }   // inverted: 9-dp_pos goes negative at 10
  else          { if (dp_pos > 10) return; }  // non-inverted: dp_pos-6 tops out at [4]

  if (inverted){
    if (dp_pos>=5) {
      buffer_b[9-dp_pos] |=1 | cathodes_b[9-dp_pos];
    } else {
      buffer_a[9-dp_pos-5]  |=(1<<14) | cathodes_a[9-dp_pos-5];
    }
  } else {
    if (dp_pos>5) {
      buffer_a[dp_pos-6] |=1<<14;
    } else {
      buffer_b[dp_pos-1] |=1;
    }
  }
}
static inline uint8_t waitForByte(void){
  while( !( USART2->ISR & USART_ISR_RXNE ) ) {};
  return USART2->RDR;
}

void transmitBlocking(uint8_t * c, size_t n){
  while (n--){
    while( !( USART2->ISR & USART_ISR_TXE ) ) {};
    USART2->TDR = *c++;
  }
}

static inline void waitForLatch(void){
  LL_USART_DisableDirectionRx(USART2);
  LL_GPIO_SetPinMode( GPIOA, LL_GPIO_PIN_3, LL_GPIO_MODE_INPUT );

  while (GPIOA->IDR & LL_GPIO_PIN_3) {}

  latchDisplay();

  // The latch byte is 0xFE with even parity, so as soon as the line returns high we can re-enable uart
  while (!(GPIOA->IDR & LL_GPIO_PIN_3)) {}

  LL_GPIO_SetPinMode( GPIOA, LL_GPIO_PIN_3, LL_GPIO_MODE_ALTERNATE );
  LL_USART_EnableDirectionRx(USART2);
}

static inline void parseByte(uint8_t x){

  if (x & 0x80) { // command byte
    status = x;
    switch (x) {
      case CMD_SHOW_CRC:
      case CMD_LOAD_TEXT:
        text_idx=0;
        dp_pos=0;
        memset(text, 0, MAX_TEXT_LEN);

        pre_buffer_b[0]=0;
        pre_buffer_b[1]=0;
        pre_buffer_b[2]=0;
        pre_buffer_b[3]=0;
        pre_buffer_b[4]=0;
        pre_buffer_a[0]=0;
        pre_buffer_a[1]=0;
        pre_buffer_a[2]=0;
        pre_buffer_a[3]=0;
        pre_buffer_a[4]=0;

        break;
      case CMD_RELOAD_TEXT:
        latchDisplay();
        break;
      case CMD_SET_SCROLL_SPEED:
        break;

      case CMD_GEM:
        break;

      case CMD_COL_LEVELS:
        levels_idx=0;
        break;

      case CMD_SET_FREQUENCY:
      case CMD_SET_FREQUENCY_B2:
      case CMD_SET_FREQUENCY_B3:
        target_freq=0;
        break;

      case CMD_REPORT_CRC:
        transmitBlocking( (uint8_t*)0x8007ffc, 4);
        break;

      case CMD_START_BOOTLOADER:
        triggerBootloader();
        break;

      default:
        status=0;
    }

    if (x==CMD_SHOW_CRC) {
      uint32_t* crc = (uint32_t*)0x8007ffc;
      sprintf(text, "d %08lx", byteswap32(crc[0]));
      for (text_idx=0; text_idx<10; text_idx++)
        setDigitPre(text_idx, text[text_idx]);
    }

    return;
  }

  // Process data
  switch(status){

  case CMD_SHOW_CRC:
  case CMD_LOAD_TEXT:
    if (x=='\n' || x==0) {
      waitForLatch();
      return;
    }
    if (x=='.') {
      dp_pos = text_idx;
      return;
    }
    if(text_idx >= MAX_TEXT_LEN) return;  // was '>': at text_idx==MAX_TEXT_LEN this wrote text[32], 1 byte past the buffer

    if (text_idx < 10) setDigitPre(text_idx, x);
    text[text_idx++] = x;
    return;

  case CMD_SET_SCROLL_SPEED:
    return;

  case CMD_GEM:
    gem_alive = 0;
    if (x == GEM_OFF || x > GEM_DIM){
      gemOff();
    } else {
      gem_mode = x;
      dimming = 1;
      if (TIM2->ARR < GEM_ARR_MIN) TIM2->ARR = GEM_ARR_MIN;
    }
    status = 0;
    return;

  case CMD_COL_LEVELS:
    // ten data bytes, viewer left->right; a complete frame is authoritative for dim mode
    // (so a lost larson-kill still converges when the terminator levels arrive)
    setColLevel(levels_idx, x & 0x0F);
    if (++levels_idx >= 10){
      levels_idx = 0;
      status = 0;
      gem_alive = 0;
      if (gem_mode != GEM_DIM) gem_mode = GEM_DIM;
      dimming = 1;
      if (TIM2->ARR < GEM_ARR_MIN) TIM2->ARR = GEM_ARR_MIN;
    }
    return;

  case CMD_SET_FREQUENCY:
    status=CMD_SET_FREQUENCY_B2;
    target_freq |= x<<14;
    return;
  case CMD_SET_FREQUENCY_B2:
    status=CMD_SET_FREQUENCY_B3;
    target_freq |= x<<7;
    return;
  case CMD_SET_FREQUENCY_B3:
    status=0;
    target_freq |= x;
    setFrequency();
    return;
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

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */

  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);

  /* System interrupt init*/

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM2_Init();
  MX_USART2_UART_Init();
  MX_TIM21_Init();
  /* USER CODE BEGIN 2 */


  //  (HAL_TIM_Base_Start_IT(&htim2)


  LL_TIM_EnableIT_UPDATE(TIM2); //TIM2->DIER |= TIM_DIER_UIE;
  LL_TIM_EnableCounter(TIM2); //TIM2->CR1 |= TIM_CR1_CEN;

  LL_TIM_EnableIT_UPDATE(TIM21);
  LL_TIM_EnableCounter(TIM21);

//  setDigitDirect(0, 'l');
//  setDigitDirect(1, 'o');
//  setDigitDirect(2, 'l');
//  setDigitDirect(3, 'o');
//  setDigitDirect(4, 'l');
//  setDigitDirect(5, 'o');
//  setDigitDirect(6, 'l');
//  setDigitDirect(7, 'o');
//  setDigitDirect(8, 'l');
//  setDigitDirect(9, 'o');


  //buffer_a[2] |= 1<<14;
  //buffer_b[2] |= 1;


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

    uint8_t x = waitForByte();
    parseByte(x);


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
  LL_FLASH_SetLatency(LL_FLASH_LATENCY_1);

  if(LL_FLASH_GetLatency() != LL_FLASH_LATENCY_1)
  {
  Error_Handler();
  }
  LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE1);
  LL_RCC_HSI_Enable();

   /* Wait till HSI is ready */
  while(LL_RCC_HSI_IsReady() != 1)
  {

  }
  LL_RCC_HSI_SetCalibTrimming(16);
  LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSI, LL_RCC_PLL_MUL_4, LL_RCC_PLL_DIV_2);
  LL_RCC_PLL_Enable();

   /* Wait till PLL is ready */
  while(LL_RCC_PLL_IsReady() != 1)
  {

  }
  LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
  LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);
  LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_1);
  LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);

   /* Wait till System clock is ready */
  while(LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL)
  {

  }

  LL_Init1msTick(32000000);

  LL_SetSystemCoreClock(32000000);
  LL_RCC_SetUSARTClockSource(LL_RCC_USART2_CLKSOURCE_PCLK1);
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

  LL_TIM_InitTypeDef TIM_InitStruct = {0};

  /* Peripheral clock enable */
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM2);

  /* TIM2 interrupt Init */
  NVIC_SetPriority(TIM2_IRQn, 0);
  NVIC_EnableIRQ(TIM2_IRQn);

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  TIM_InitStruct.Prescaler = 0;
  TIM_InitStruct.CounterMode = LL_TIM_COUNTERMODE_UP;
  TIM_InitStruct.Autoreload = 319;
  TIM_InitStruct.ClockDivision = LL_TIM_CLOCKDIVISION_DIV1;
  LL_TIM_Init(TIM2, &TIM_InitStruct);
  LL_TIM_EnableARRPreload(TIM2);
  LL_TIM_SetClockSource(TIM2, LL_TIM_CLOCKSOURCE_INTERNAL);
  LL_TIM_SetTriggerOutput(TIM2, LL_TIM_TRGO_RESET);
  LL_TIM_DisableMasterSlaveMode(TIM2);
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief TIM21 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM21_Init(void)
{

  /* USER CODE BEGIN TIM21_Init 0 */

  /* USER CODE END TIM21_Init 0 */

  LL_TIM_InitTypeDef TIM_InitStruct = {0};

  /* Peripheral clock enable */
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_TIM21);

  /* TIM21 interrupt Init */
  NVIC_SetPriority(TIM21_IRQn, 0);
  NVIC_EnableIRQ(TIM21_IRQn);

  /* USER CODE BEGIN TIM21_Init 1 */

  /* USER CODE END TIM21_Init 1 */
  TIM_InitStruct.Prescaler = 31;
  TIM_InitStruct.CounterMode = LL_TIM_COUNTERMODE_UP;
  TIM_InitStruct.Autoreload = 19999;
  TIM_InitStruct.ClockDivision = LL_TIM_CLOCKDIVISION_DIV1;
  LL_TIM_Init(TIM21, &TIM_InitStruct);
  LL_TIM_DisableARRPreload(TIM21);
  LL_TIM_SetClockSource(TIM21, LL_TIM_CLOCKSOURCE_INTERNAL);
  LL_TIM_SetTriggerOutput(TIM21, LL_TIM_TRGO_RESET);
  LL_TIM_DisableMasterSlaveMode(TIM21);
  /* USER CODE BEGIN TIM21_Init 2 */

  /* USER CODE END TIM21_Init 2 */

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

  LL_USART_InitTypeDef USART_InitStruct = {0};

  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* Peripheral clock enable */
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART2);

  LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
  /**USART2 GPIO Configuration
  PA2   ------> USART2_TX
  PA3   ------> USART2_RX
  */
  GPIO_InitStruct.Pin = LL_GPIO_PIN_2;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_4;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LL_GPIO_PIN_3;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_4;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN USART2_Init 1 */

  // Disable overrun detection, for two reasons
  // 1. The command structure should always sort itself out
  // 2. It makes interactive debugging the assembled clock a lot easier
  USART2->CR3 = USART_CR3_OVRDIS_Msk;

  /* USER CODE END USART2_Init 1 */
  USART_InitStruct.BaudRate = 115200;
  USART_InitStruct.DataWidth = LL_USART_DATAWIDTH_9B;
  USART_InitStruct.StopBits = LL_USART_STOPBITS_1;
  USART_InitStruct.Parity = LL_USART_PARITY_EVEN;
  USART_InitStruct.TransferDirection = LL_USART_DIRECTION_TX_RX;
  USART_InitStruct.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
  USART_InitStruct.OverSampling = LL_USART_OVERSAMPLING_16;
  LL_USART_Init(USART2, &USART_InitStruct);
  LL_USART_ConfigAsyncMode(USART2);
  LL_USART_Enable(USART2);
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOC);
  LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
  LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOB);

  /**/
  LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_0);

  /**/
  LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_1);

  /**/
  LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_4);

  /**/
  LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_5);

  /**/
  LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_6);

  /**/
  LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_7);

  /**/
  LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_0);

  /**/
  LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_1);

  /**/
  LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_10);

  /**/
  LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_11);

  /**/
  LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_12);

  /**/
  LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_13);

  /**/
  LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_14);

  /**/
  LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_15);

  /**/
  LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_8);

  /**/
  LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_9);

  /**/
  LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_10);

  /**/
  LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_11);

  /**/
  LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_12);

  /**/
  LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_15);

  /**/
  LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_4);

  /**/
  LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_5);

  /**/
  LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_6);

  /**/
  LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_7);

  /**/
  LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_8);

  /**/
  LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_9);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_0;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_UP;
  LL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_13;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_UP;
  LL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_0;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_1;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_4;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_5;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_6;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_7;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_0;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_1;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_10;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_11;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_12;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_13;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_14;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_15;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_8;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_9;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_10;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_11;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_12;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_15;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_3;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_UP;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_4;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_5;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_6;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_7;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_8;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /**/
  GPIO_InitStruct.Pin = LL_GPIO_PIN_9;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

#define DISABLE_SWCLK

#ifdef DISABLE_SWCLK

  if ( (LL_GPIO_ReadInputPort(GPIOC) & LL_GPIO_PIN_13)!=0 && ((LL_GPIO_ReadInputPort(GPIOB) & LL_GPIO_PIN_3)!=0) ){
    GPIO_InitStruct.Pin = LL_GPIO_PIN_14;
    GPIO_InitStruct.Mode = LL_GPIO_MODE_OUTPUT;
    GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
    LL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  }
#endif
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
