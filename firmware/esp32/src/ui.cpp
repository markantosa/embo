#include "ui.h"
#include "config.h"
#include <Arduino.h>
// TFT_eSPI: copy User_Setup.h below into the library config, or set via build flags
#include <TFT_eSPI.h>

// TFT_eSPI pins are set via build_flags in platformio.ini (see User_Setup_Select.h).
// See include/tft_user_setup.h for the pin defines pushed via -D flags.
static TFT_eSPI _tft;

static void encoder_init();
static void buttons_init();
static void buzzer_init();

void ui_init() {
    encoder_init();
    buttons_init();
    buzzer_init();

    _tft.init();
    _tft.setRotation(1);
    _tft.fillScreen(TFT_BLACK);
    // TODO: draw splash / home screen
}

void ui_update() {
    // TODO: handle encoder delta, button presses, redraw changed regions
}

static void encoder_init() {
    pinMode(PIN_EC11_A, INPUT_PULLUP);
    pinMode(PIN_EC11_B, INPUT_PULLUP);
    pinMode(PIN_EC11_SW, INPUT_PULLUP);
    // TODO: attach EXTI interrupts on both edges for quadrature decode
}

static void buttons_init() {
    pinMode(PIN_BTN1, INPUT_PULLUP);
    pinMode(PIN_BTN2, INPUT_PULLUP);
}

static void buzzer_init() {
    // LEDC channel LEDC_CH_BUZ on PIN_BUZ_PWM
    ledcSetup(LEDC_CH_BUZ, 1000, 8);
    ledcAttachPin(PIN_BUZ_PWM, LEDC_CH_BUZ);
    ledcWrite(LEDC_CH_BUZ, 0);   // silent
}
