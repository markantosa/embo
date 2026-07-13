#pragma once


// ---- Display (SPI) ----
#define TFT_SCK   12
#define TFT_MOSI  11
#define TFT_MISO  13
#define TFT_CS    10
#define TFT_DC    9
#define TFT_RST   14

// ---- Touch ----
#define TOUCH_CS  21

// ---- Encoder ----
#define ENC_CLK   4
#define ENC_DT    5
#define ENC_SW    6

// ---- Buttons ----
#define BTN_NEXT_PIN   7   // currently unused
#define BTN_SELECT_PIN 15  // global reset

#define BUTTON_DEBOUNCE_MS 40
#define PERCENT_MIN 0
#define PERCENT_MAX 100
#define PERCENT_STEP 1 //step change per detent

