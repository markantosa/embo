#pragma once
#include <Adafruit_ILI9341.h>
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
    void begin(Adafruit_ILI9341& tftRef, ButtonDriver& next, ButtonDriver& select,
               EncoderDriver& enc, PercentageLogic& percent,
               MenuLogic& menuRef, ScreenMenu& menuScreenRef, ScreenPercentage& percentScreenRef) {
        tft = &tftRef;
        btnNext = &next;
        btnSelect = &select;
        encoder = &enc;
        percentage = &percent;
        menu = &menuRef;
        menuScreen = &menuScreenRef;
        percentScreen = &percentScreenRef;

        enterState(STATE_MENU);
    }

    void update() {
        switch (currentState) {
            case STATE_MENU:      updateMenu();       break;
            case STATE_PERCENTAGE: updatePercentage(); break;
            case STATE_MIXING:    updateMixing();     break;
            case STATE_DONE:      updateDone();       break;
        }
    }

private:
    Adafruit_ILI9341* tft;
    ButtonDriver* btnNext;
    ButtonDriver* btnSelect;
    EncoderDriver* encoder;
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

    void updateMenu() {
        if (btnNext->wasPressed()) {
            menu->next();
            needsRedraw = true;
        }

        if (btnSelect->wasPressed()) {
            int action = menu->select();
            if (action == 1) { // "Start Mixing"
                enterState(STATE_PERCENTAGE);
                return;
            }
            // handle action 2/3 (Calibrate/Settings) later
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

        if (btnSelect->wasPressed()) {
            enterState(STATE_MIXING);
        }
    }

    void updateMixing() {
        if (needsRedraw) {
            tft->fillScreen(ILI9341_BLACK);
            tft->setTextColor(ILI9341_WHITE);
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
            tft->fillScreen(ILI9341_BLACK);
            tft->setTextColor(ILI9341_GREEN);
            tft->setTextSize(3);
            tft->setCursor(20, 130);
            tft->print("Mixing Complete");
            needsRedraw = false;
        }

        if (btnSelect->wasPressed() || btnNext->wasPressed()) {
            enterState(STATE_MENU);
        }
    }
};