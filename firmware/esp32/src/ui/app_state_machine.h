#pragma once
#include "hal/LGFX_Config.h"    // was: #include <Adafruit_ILI9341.h>
#include "hal/button_driver.h"
#include "hal/encoder_driver.h"
#include "backend/menu_logic.h"
#include "backend/percentage_logic.h"
#include "ui/screen_main_menu.cpp"
#include "ui/screen_percentage.cpp"

enum AppState {
    STATE_MENU,
    STATE_PERCENTAGE,
    STATE_MIXING,
    STATE_DONE
};

class AppStateMachine {
public:
    void begin(LGFX& tftRef, ButtonDriver& btn1,     // was: Adafruit_ILI9341&
               EncoderDriver& enc, ButtonDriver& encBtn, PercentageLogic& percent,
               MenuLogic& menuRef, ScreenMenu& menuScreenRef, ScreenPercentage& percentScreenRef) {
        tft = &tftRef;
        btn = &btn1;
        encoder = &enc;
        encoderButton = &encBtn;
        percentage = &percent;
        menu = &menuRef;
        menuScreen = &menuScreenRef;
        percentScreen = &percentScreenRef;

        enterState(STATE_MENU);
    }

    void update() {
        if (btn->wasPressed()) {
            resetAll();
            return;
        }

        switch (currentState) {
            case STATE_MENU:       updateMenu();       break;
            case STATE_PERCENTAGE: updatePercentage(); break;
            case STATE_MIXING:     updateMixing();     break;
            case STATE_DONE:       updateDone();       break;
        }
    }

private:
    LGFX* tft;    // was: Adafruit_ILI9341*
    ButtonDriver* btn;
    EncoderDriver* encoder;
    ButtonDriver* encoderButton;
    PercentageLogic* percentage;
    MenuLogic* menu;
    ScreenMenu* menuScreen;
    ScreenPercentage* percentScreen;

    AppState currentState = STATE_MENU;
    bool needsRedraw = false;
    unsigned long stateEnteredAt = 0;

    void enterState(AppState newState) {
        currentState = newState;
        stateEnteredAt = millis();
        needsRedraw = true;
    }

    void resetAll() {
        percentage->begin(0);
        menu->reset();
        enterState(STATE_MENU);
    }

    void updateMenu() {
        int step = encoder->readStep();
        if (step != 0) {
            if (step > 0) menu->next();
            else          menu->previous();
            needsRedraw = true;
        }

        if (encoderButton->wasPressed()) {
            int action = menu->select();
            if (action == 1) {
                enterState(STATE_PERCENTAGE);
                return;
            }
        }

        if (needsRedraw) {
            menuScreen->render(*tft, *menu);
            needsRedraw = false;
        }
    }

    void updatePercentage() {
        int step = encoder->readStep();
        if (step != 0) {
            percentage->applyStep(step);
            needsRedraw = true;
        }

        if (needsRedraw) {
            percentScreen->render(*tft, *percentage);
            needsRedraw = false;
        }

        if (encoderButton->wasPressed()) {
            enterState(STATE_MIXING);
        }
    }

    void updateMixing() {
        if (needsRedraw) {
            tft->fillScreen(TFT_BLACK);      // was: ILI9341_BLACK
            tft->setTextColor(TFT_WHITE);
            tft->setTextSize(3);
            tft->setCursor(40, 130);
            tft->print("Mixing...");
            needsRedraw = false;
        }

        if (millis() - stateEnteredAt >= 5000) {
            enterState(STATE_DONE);
        }
    }

    void updateDone() {
        if (needsRedraw) {
            tft->fillScreen(TFT_BLACK);
            tft->setTextColor(TFT_GREEN);    // was: ILI9341_GREEN
            tft->setTextSize(3);
            tft->setCursor(20, 130);
            tft->print("Mixing Complete");
            needsRedraw = false;
        }

        if (encoderButton->wasPressed()) {
            enterState(STATE_MENU);
        }
    }
};