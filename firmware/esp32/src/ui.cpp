#include "ui.h"
#include "config.h"
#include "ui_screen_manager.h"
#include "ui_input.h"
#include "ui_display.h"
#include "ui/screens/set_target_screen.h"
#include "ui/screens/running_screen.h"
#include "ui/screens/done_screen.h"
#include "ui/screens/verifying_screen.h"
#include "ui/screens/error_screen.h"
#include "ui/screens/settings_menu.h"
#include "ui/boot_logo.h"
#include "hal/buzzer_driver.h"
#include "ble_debug.h"
#include <Arduino.h>

// This file is intentionally thin: it wires the screens together once at
// boot and forwards ui_init()/ui_update() to the ScreenManager. To add a
// new screen:
//   1. Give it its own Screen subclass under src/ui/screens/ (see
//      menu_screen.h for a reusable data-driven list, or any of the
//      existing screens for a bespoke one).
//   2. Instantiate it below and add one line of navigation wiring
//      (a `.wire(...)` call, or a `mgr.push()`/`goTo()` from whichever
//      existing screen should be able to reach it).
// No other file needs to know it exists.

static BuzzerDriver _buzzer(PIN_BUZ_PWM);

static SetTargetScreen _setTargetScreen;
static RunningScreen   _runningScreen;
static DoneScreen      _doneScreen;
static VerifyingScreen _verifyingScreen;
static ErrorScreen     _errorScreen;

static ScreenManager _screenManager;

void ui_init() {
    ui_input_init();

    LGFX &tft = ui_display_tft();
    tft.init();
    tft.setRotation(3);

    _buzzer.begin();

    // Boot splash — bitmap logo instead of a plain text splash, validated on
    // the bench (testing/display_ui_testing) before folding in here.
    int16_t x = (tft.width()  - LOGO_WIDTH)  / 2;
    int16_t y = (tft.height() - LOGO_HEIGHT) / 2;
    tft.pushImage(x, y, LOGO_WIDTH, LOGO_HEIGHT, epd_bitmap_embo_logoembologo320240);
    _buzzer.tone(523, 150);
    delay(1500);  // brief splash hold — not the full 5s bench-test delay, boot has real init work to do after

    // Wire navigation between screens — see each screen's .wire() for what
    // it needs. This is the one place that has to know the whole screen
    // graph; every screen itself only knows the handful it can reach.
    _setTargetScreen.wire(_runningScreen, _verifyingScreen);
    _runningScreen.wire(_doneScreen);
    _doneScreen.wire(_setTargetScreen, _verifyingScreen);

    _screenManager.begin(&_setTargetScreen);
}

void ui_update() {
    _buzzer.update();  // services the boot chirp's auto-stop timing
    _screenManager.update();
}

void ui_show_error(const char *msg) {
    _errorScreen.setMessage(msg);
    _screenManager.goTo(&_errorScreen);
    ble_log("UI: HARDWARE FAULT - %s", msg);
}

void ui_open_settings_menu() {
    _screenManager.push(&settings_menu());
}
