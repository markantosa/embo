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
#define BTN1    0
#define BTN2    14