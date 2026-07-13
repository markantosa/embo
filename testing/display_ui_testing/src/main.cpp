#include <config.h>
#include <Adafruit_ILI9341.h>
#include "hal/button_driver.h"
#include "hal/encoder_driver.h"
#include "backend/menu_logic.h"
#include "backend/percentage_logic.h"
#include "ui/screen_main_menu.cpp"
#include "ui/screen_percentage.cpp"
#include "ui/boot_logo.h"
#include "ui/app_state_machine.h"

ButtonDriver btnNext;      // button 1 — currently unused
ButtonDriver btnSelect;    // button 2 — global reset
ButtonDriver encoderBtn;   // encoder push — confirm action
EncoderDriver encoder;
PercentageLogic percentage;
ScreenPercentage percentScreen;
MenuLogic menu;
ScreenMenu menuScreen;
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCK, TFT_RST, TFT_MISO);
AppStateMachine appState;

void showBootLogo() {
    tft.fillScreen(ILI9341_BLACK);
    int16_t x = (tft.width()  - LOGO_WIDTH)  / 2;
    int16_t y = (tft.height() - LOGO_HEIGHT) / 2;
    tft.drawRGBBitmap(x, y, epd_bitmap_embo_logo, LOGO_WIDTH, LOGO_HEIGHT);
    delay(2000);
}

void setup() {
    SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
    tft.begin(20000000);
    tft.setRotation(1);

    btnNext.begin(BTN_NEXT_PIN);
    btnSelect.begin(BTN_SELECT_PIN);
    encoderBtn.begin(ENC_SW);
    encoder.begin(ENC_CLK, ENC_DT);
    percentage.begin(0);

    showBootLogo();

    menu.begin({
        {"Start Mixing", 1},
        {"Calibrate",      2},
        {"Settings",       3}
    });

    appState.begin(tft, btnNext, btnSelect, encoder, encoderBtn, percentage, menu, menuScreen, percentScreen);
}

// void loop() {
//     appState.update();
// }
// Target Framework: 30 FPS -> (1000ms / 30) ≈ 33ms per frame
// Target Framework: 60 FPS -> (1000ms / 60) ≈ 16ms per frame
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