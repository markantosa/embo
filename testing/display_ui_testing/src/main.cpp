#include <config.h>
#include <Adafruit_ILI9341.h>
#include "hal/button_driver.h"
#include "hal/encoder_driver.h"
#include "backend/menu_logic.h"
#include "backend/percentage_logic.h"
#include "ui/screen_main_menu.cpp"
#include "ui/screen_percentage.cpp"


ButtonDriver btnNext;
ButtonDriver btnSelect;
EncoderDriver encoder;
PercentageLogic percentage;
ScreenPercentage percentScreen;
MenuLogic menu;
ScreenMenu menuScreen;
Adafruit_ILI9341 tft(TFT_CS,TFT_DC,TFT_MOSI,TFT_SCK,TFT_RST,TFT_MISO);

bool needsRedraw = true;

void setup() {
    SPI.begin(TFT_SCK,TFT_MISO,TFT_MOSI,TFT_CS);
    tft.begin(40000000);
    tft.setRotation(1);


    btnNext.begin(BTN_NEXT_PIN);
    btnSelect.begin(BTN_SELECT_PIN);
    encoder.begin(ENC_CLK, ENC_DT);
    percentage.begin(0); //start percentage at 0

    menu.begin({
        {"Start Mixing", 1},
        {"Calibrate",      2},
        {"Settings",       3}
    });

    menuScreen.render(tft,menu);
}    

void loop() {
    if (btnNext.wasPressed()) {
        menu.next();
        needsRedraw = true;
    }

    if (btnSelect.wasPressed()) {
        int action = menu.select();
        // handle action, e.g. switch app state / screen
        needsRedraw = true;
    }

    if (needsRedraw) {
        menuScreen.render(tft, menu);
        needsRedraw = false;
    }

    int step = encoder.readStep();
    if (step != 0) {
     percentage.applyStep(step);
     needsRedraw = true;
    }

    if (needsRedraw) {
      percentScreen.render(tft,percentage);
      needsRedraw = false;
    }
}