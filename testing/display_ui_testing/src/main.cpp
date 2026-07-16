#include <config.h>
#include "hal/LGFX_Config.h"          // was: #include <Adafruit_ILI9341.h>
#include "hal/button_driver.h"
#include "hal/encoder_driver.h"
#include "backend/menu_logic.h"
#include "backend/percentage_logic.h"
#include "ui/screen_main_menu.cpp"
#include "ui/screen_percentage.cpp"
#include "ui/boot_logo.h"
#include "ui/app_state_machine.h"

ButtonDriver btnNext;
ButtonDriver btnSelect;
ButtonDriver encoderBtn;
EncoderDriver encoder;
PercentageLogic percentage;
ScreenPercentage percentScreen;
MenuLogic menu;
ScreenMenu menuScreen;
LGFX tft;                              // was: Adafruit_ILI9341 tft(TFT_CS, ...)
AppStateMachine appState;

void showBootLogo() {
    int16_t x = (tft.width()  - LOGO_WIDTH)  / 2;
    int16_t y = (tft.height() - LOGO_HEIGHT) / 2;
    tft.pushImage(x, y, LOGO_WIDTH, LOGO_HEIGHT, epd_bitmap_embo_logoembologo320240); // was: drawRGBBitmap
    delay(5000);
}

void setup() {
    // SPI.begin(...) line is GONE — LGFX owns the bus internally
    tft.init();              // was: tft.begin(20000000)
    tft.setRotation(3);      // same API, no change

    btnNext.begin(BTN_NEXT_PIN);
    btnSelect.begin(BTN_SELECT_PIN);
    encoderBtn.begin(ENC_SW);
    encoder.begin(ENC_CLK, ENC_DT);
    percentage.begin(0);

    showBootLogo();

    menu.begin({
        {"Start Mixing", 1},
        {"Calibrate",    2},
        {"Settings",     3}
    });

    appState.begin(tft, btnNext, btnSelect, encoder, encoderBtn, percentage, menu, menuScreen, percentScreen);
}

const unsigned long FRAME_DELAY = 33; 
unsigned long lastFrameTime = 0;

void loop() {
  unsigned long currentMillis = millis();

  // 1. Handle background logic here (sensors, math, Wi-Fi)
  // ...

  // 2. Check if it's time to render the next frame
  if (currentMillis - lastFrameTime >= FRAME_DELAY) {
    lastFrameTime = currentMillis;

    // Execute your display update code here
    appState.update();
  }
}