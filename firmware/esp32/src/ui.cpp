#include "ui.h"
#include "config.h"
#include "ui_screen_manager.h"
#include "ui_input.h"
#include "ui_display.h"
#include "ui/screens/placeholder_screen.h"
#include "ui/screens/menu_screen.h"
#include "ui/screens/developer_mode_screen.h"
#include "ui/screens/uas_debug_toggle_screen.h"
#include "ui/screens/mixing_menu_screen.h"
#include "ui/screens/warning_screen.h"
#include "ui/screens/mixing_running_screen.h"
#include "ui/screens/end_screen.h"
#include "ui/screens/verifying_screen.h"
#include "ui/screens/error_screen.h"
#include "ui/screens/bench_diagnostics_menu.h"
#include "ui/boot_logo.h"
#include "hal/buzzer_driver.h"
#include "ble_debug.h"
#include <Arduino.h>

// This file is intentionally thin: it wires the screens together once at
// boot and forwards ui_init()/ui_update() to the ScreenManager. To add a
// new screen:
//   1. Give it its own Screen subclass under src/ui/screens/ (see
//      menu_screen.h for a reusable data-driven list, placeholder_screen.h
//      for a simple message/confirm/back screen, or any of the bespoke
//      ones below as a fuller example).
//   2. Instantiate it below and add one line of navigation wiring
//      (a `.wire()`/`configure()`/`setConfirmTarget()` call, or a
//      `mgr.push()`/`goTo()` from whichever existing screen should be able
//      to reach it).
// No other file needs to know it exists.
//
// Screen graph (see the flowchart this was built from):
//   Insert syringe -> Start Menu -> [Start] -> Mixing Menu -> Warning
//                                                            -> Mixing Running -> End -> (back to Start Menu)
//                                -> [Settings] -> [Presets] (stub)
//                                              -> [Developer mode] -> [UAS debug mode] (BLE toggle)
//                                -> [Camera feature] -> mount check -> (stub, "developing in progress")
//   End screen and Mixing/Warning screens can also reach Verifying (camera
//   size check) via encoder long-press, same as before this redesign.

static BuzzerDriver _buzzer(PIN_BUZ_PWM);

// ── Screens with no constructor-time cross-references (all wired via
// post-construction methods in ui_init(), so declaration order among these
// doesn't matter) ────────────────────────────────────────────────────────────
static VerifyingScreen       _verifyingScreen;
static ErrorScreen           _errorScreen;
static UasDebugToggleScreen  _uasDebugToggleScreen;
static DeveloperModeScreen   _developerModeScreen;
static MixingMenuScreen      _mixingMenuScreen;
static WarningScreen         _warningScreen;
static MixingRunningScreen   _mixingRunningScreen;
static EndScreen             _endScreen;
static PlaceholderScreen     _cameraMountScreen;
static PlaceholderScreen     _cameraStubScreen;
static PlaceholderScreen     _presetsStubScreen;
static PlaceholderScreen     _insertSyringeScreen;

// ── Settings Menu — defined before Start Menu since Start Menu's "Settings"
// item needs to reference it by address (see file header for why this
// order matters: MenuItem callback bodies need referenced statics already
// declared, unlike .wire() calls in ui_init() which run after everything
// exists regardless of order). ────────────────────────────────────────────
static void _settingsGoPresets(ScreenManager &mgr)   { mgr.push(&_presetsStubScreen); }
static void _settingsGoDeveloper(ScreenManager &mgr) { mgr.push(&_developerModeScreen); }
static void _settingsBack(ScreenManager &mgr)        { mgr.pop(); }

static const MenuItem kSettingsItems[] = {
    { "Presets",        _settingsGoPresets },
    { "Developer mode", _settingsGoDeveloper },
    { "Back",           _settingsBack },
};
static MenuScreen _settingsMenuScreen("Settings", kSettingsItems,
                                       sizeof(kSettingsItems) / sizeof(kSettingsItems[0]));

// ── Start Menu ────────────────────────────────────────────────────────────────
static void _startGoStart(ScreenManager &mgr)    { mgr.push(&_mixingMenuScreen); }
static void _startGoSettings(ScreenManager &mgr) { mgr.push(&_settingsMenuScreen); }
static void _startGoCamera(ScreenManager &mgr)   { mgr.push(&_cameraMountScreen); }

static const MenuItem kStartItems[] = {
    { "Start",          _startGoStart },
    { "Settings",       _startGoSettings },
    { "Camera feature", _startGoCamera },
};
static MenuScreen _startMenuScreen("Start Menu", kStartItems,
                                    sizeof(kStartItems) / sizeof(kStartItems[0]));

static ScreenManager _screenManager;

void ui_init() {
    ui_input_init();
    Serial.println("UI_INIT: input init done");

    LGFX &tft = ui_display_tft();
    bool tftOk = tft.init();
    Serial.printf("UI_INIT: tft.init() returned %s\n", tftOk ? "true (panel detected/init OK)" : "FALSE — panel init failed");
    tft.setRotation(3);
    Serial.printf("UI_INIT: after setRotation(3), tft.width()=%d tft.height()=%d (logo is %dx%d)\n",
                  tft.width(), tft.height(), LOGO_WIDTH, LOGO_HEIGHT);

    // Boot splash — bitmap logo instead of a plain text splash, validated on
    // the bench (testing/display_ui_testing) before folding in here.
    int16_t x = (tft.width()  - LOGO_WIDTH)  / 2;
    int16_t y = (tft.height() - LOGO_HEIGHT) / 2;
    Serial.printf("UI_INIT: pushing logo at x=%d y=%d\n", x, y);
    tft.pushImage(x, y, LOGO_WIDTH, LOGO_HEIGHT, epd_bitmap_embo_logoembologo320240);
    Serial.println("UI_INIT: boot splash drawn");
    _buzzer.tone(523, 150);
    delay(1500);  // brief splash hold — not the full 5s bench-test delay, boot has real init work to do after
    Serial.println("UI_INIT: splash hold done, wiring screens");


    _cameraMountScreen.configure("Camera Feature", "Ensure syringe is properly mounted", true);
    _cameraMountScreen.setConfirmTarget(&_cameraStubScreen, true);  // push — Back pops to Start Menu
    _cameraStubScreen.configure("Camera Feature", "Feature idea developing in progress", true);

    _presetsStubScreen.configure("Presets", "Feature idea developing in progress", true);

    _developerModeScreen.wire(_uasDebugToggleScreen);
    _mixingMenuScreen.wire(_warningScreen, _verifyingScreen);
    _warningScreen.wire(_mixingRunningScreen);
    _mixingRunningScreen.wire(_endScreen);
    _endScreen.wire(_startMenuScreen, _verifyingScreen);
    Serial.println("UI_INIT: screens wired, starting screen manager");

    _screenManager.begin(&_startMenuScreen);
    Serial.println("UI_INIT: screen manager started");
}

void ui_update() {
    _screenManager.update();
}

void ui_show_error(const char *msg) {
    _errorScreen.setMessage(msg);
    _screenManager.goTo(&_errorScreen);
    ble_log("UI: HARDWARE FAULT - %s", msg);
}

void ui_open_bench_diagnostics_menu() {
    _screenManager.push(&bench_diagnostics_menu());
}
