#pragma once


// ---- Display pin configuration ----
#define TFT_CS   18
#define TFT_DC   19
#define TFT_RST  20
#define TFT_SCK  6
#define TFT_MOSI 7
#define TFT_MISO 21  // now used — shared bus, needed for touch
// ---- Touch pin configuration ----
#define TOUCH_CS 22  // touch's own CS, shares SCK/MOSI/MISO with display
// Encoder, Button encoder pin config
#define ENC_CLK 1
#define ENC_DT  2
#define ENC_SW  3
#define BTN_NEXT_PIN    0
#define BTN_SELECT_PIN    14

#define BUTTON_DEBOUNCE_MS 30
#define PERCENT_MIN 0
#define PERCENT_MAX 100
#define PERCENT_STEP 1 //step change per detent