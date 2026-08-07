#include "ui.h"
#include "config.h"
#include "ui_screen_manager.h"
#include "ui_input.h"
#include "ui_display.h"
#include "ui/screens/placeholder_screen.h"
#include "ui/screens/menu_screen.h"
#include "ui/screens/developer_mode_screen.h"
#include "ui/screens/uas_debug_toggle_screen.h"
#include "ui/screens/sound_toggle_screen.h"
#include "mixing_options.h"
#include "sound_settings.h"
#include "ui/screens/mixing_menu_screen.h"
#include "ui/screens/warning_screen.h"
#include "ui/screens/mixing_running_screen.h"
#include "ui/screens/end_screen.h"
#include "ui/screens/verifying_screen.h"
#include "ui/screens/error_screen.h"
#include "ui/screens/bench_diagnostics_menu.h"
#include "ui/screens/jog_motor_screen.h"
#include "ui/screens/stroke_test_screen.h"
#include "motors.h"
#include "scheduler.h"
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
//   Insert syringe -> Start Menu -> [Start] -> Agent selection -> Target type -> Mixing Menu -> Warning
//                                                                                              -> Mixing Running -> End -> (back to Start Menu)
//                                -> [Settings] -> [Sound] (on/off toggle)
//                                              -> [Motion] -> [Home Motors]
//                                              -> [Developer mode] -> [UAS debug mode] (BLE toggle)
//                                -> [Camera feature] -> mount check -> (stub, "developing in progress")
//   End screen and Mixing/Warning screens can also reach Verifying (camera
//   size check) via encoder long-press, same as before this redesign.

static BuzzerDriver _buzzer(PIN_BUZ_PWM, 2);  // channel 2 — motors explicitly own 0 and 1, see motors.cpp

// ── Screens with no constructor-time cross-references (all wired via
// post-construction methods in ui_init(), so declaration order among these
// doesn't matter) ────────────────────────────────────────────────────────────
static VerifyingScreen       _verifyingScreen;
static ErrorScreen           _errorScreen;
static UasDebugToggleScreen  _uasDebugToggleScreen;
static SoundToggleScreen     _soundToggleScreen;
static DeveloperModeScreen   _developerModeScreen;
static MixingMenuScreen      _mixingMenuScreen;
static WarningScreen         _warningScreen;
static MixingRunningScreen   _mixingRunningScreen;
static EndScreen             _endScreen;
static PlaceholderScreen     _cameraMountScreen;
static PlaceholderScreen     _cameraStubScreen;
static PlaceholderScreen     _insertSyringeScreen;
static JogMotorScreen        _jogMotor1Screen;
static JogMotorScreen        _jogMotor2Screen;
static StrokeTestScreen      _strokeTestScreen;

// ── Motion Menu — Settings > Motion. Defined before Settings Menu since
// Settings' "Motion" item needs to reference it by address (same ordering
// rule as the rest of this file). ──────────────────────────────────────────
static void _motionHome(ScreenManager &mgr) {
    if (scheduler_is_running()) {
        ble_log("Motion > Home Motors: refused - a run is in progress");
        return;
    }
    // Blocking, same tradeoff already accepted everywhere else homing is
    // triggered (Mixing Menu, BLE HOME) — nothing can be running yet, see
    // the guard above.
    if (!motors_home()) {
        ui_show_error("HOMING FAILED - check limit switches");
        return;
    }
    mgr.pop();
}
static void _motionBack(ScreenManager &mgr) { mgr.pop(); }
static void _motionJog1(ScreenManager &mgr) { mgr.push(&_jogMotor1Screen); }
static void _motionJog2(ScreenManager &mgr) { mgr.push(&_jogMotor2Screen); }

// "Left"/"right" = Motor 1/Motor 2 — a naming choice used throughout this
// test; nothing in firmware itself distinguishes left/right beyond this.
static void _motionTestBoth(ScreenManager &mgr) { mgr.push(&_strokeTestScreen); }

static const MenuItem kMotionItems[] = {
    { "Home Motors",      _motionHome },
    { "Jog Motor 1",      _motionJog1 },
    { "Jog Motor 2",      _motionJog2 },
    { "Test Both Motors", _motionTestBoth },
    { "Back",             _motionBack },
};
static MenuScreen _motionMenuScreen("Motion", kMotionItems,
                                     sizeof(kMotionItems) / sizeof(kMotionItems[0]));

// ── Settings Menu — defined before Start Menu since Start Menu's "Settings"
// item needs to reference it by address (see file header for why this
// order matters: MenuItem callback bodies need referenced statics already
// declared, unlike .wire() calls in ui_init() which run after everything
// exists regardless of order). ────────────────────────────────────────────
static void _settingsGoSound(ScreenManager &mgr)     { mgr.push(&_soundToggleScreen); }
static void _settingsGoMotion(ScreenManager &mgr)    { mgr.push(&_motionMenuScreen); }
static void _settingsGoDeveloper(ScreenManager &mgr) { mgr.push(&_developerModeScreen); }
static void _settingsBack(ScreenManager &mgr)        { mgr.pop(); }

static const MenuItem kSettingsItems[] = {
    { "Sound",          _settingsGoSound },
    { "Motion",         _settingsGoMotion },
    { "Developer mode", _settingsGoDeveloper },
    { "Back",           _settingsBack },
};
static MenuScreen _settingsMenuScreen("Settings", kSettingsItems,
                                       sizeof(kSettingsItems) / sizeof(kSettingsItems[0]));

// ── Target Type — Start > Agent selection > here > Mixing Menu. Defined
// before Agent Selection since Agent Selection's items need to reference
// it (same ordering rule as Settings Menu above). ──────────────────────────
static void _targetTypeGoSize(ScreenManager &mgr) {
    mixing_options_set_target_type(TargetType::SIZE);
    mgr.push(&_mixingMenuScreen);
}
static void _targetTypeGoViscosity(ScreenManager &mgr) {
    mixing_options_set_target_type(TargetType::VISCOSITY);
    mgr.push(&_mixingMenuScreen);
}

static const MenuItem kTargetTypeItems[] = {
    { "Size",      _targetTypeGoSize },
    { "Viscosity", _targetTypeGoViscosity },
};
static MenuScreen _targetTypeMenuScreen("Target Type", kTargetTypeItems,
                                         sizeof(kTargetTypeItems) / sizeof(kTargetTypeItems[0]));

// --Syringe Selection - Start > Agent Selection > here > Target Type > Mixing running ----
static void _syringeGoTerumo(ScreenManager &mgr) {
    mixing_options_set_syringe_type(SyringeType::Terumo);
    mgr.push(&_targetTypeMenuScreen);
}
static void _syringeGoNipro(ScreenManager &mgr) {
    mixing_options_set_syringe_type(SyringeType::Nipro);
    mgr.push(&_targetTypeMenuScreen);
}

static const MenuItem kSyringeTypeItems[] = {
    {"Terumo", _syringeGoTerumo},
    {"Nipro", _syringeGoNipro},
};
static MenuScreen _syringeTypeMenuScreen("Select Syringe Type:", kSyringeTypeItems,
                                           sizeof(kSyringeTypeItems)/ sizeof(kSyringeTypeItems[0]));

// ── Agent Selection — Start > here > Syringe Selection > Target Type > Mixing running ────────────
static void _agentGoGelfoam(ScreenManager &mgr) {
    mixing_options_set_agent(SyringeAgent::Gelfoam);
    mgr.push(&_syringeTypeMenuScreen);
}
static void _agentGoLyostypt(ScreenManager &mgr) {
    mixing_options_set_agent(SyringeAgent::Lyostypt);
    mgr.push(&_syringeTypeMenuScreen);
}

static const MenuItem kAgentItems[] = {
    { "Gelfoam", _agentGoGelfoam}
    ,{ "Lyostypt",  _agentGoLyostypt },
};
static MenuScreen _agentSelectionMenuScreen("Select Agent", kAgentItems,
                                             sizeof(kAgentItems) / sizeof(kAgentItems[0]));


// ── Start Menu ────────────────────────────────────────────────────────────────
static void _startGoStart(ScreenManager &mgr)    { mgr.push(&_agentSelectionMenuScreen); }
static void _startGoSettings(ScreenManager &mgr) { mgr.push(&_settingsMenuScreen); }
static void _startGoCamera(ScreenManager &mgr)   { mgr.push(&_cameraMountScreen); }

static const MenuItem kStartItems[] = {
    { "Start",          _startGoStart },
    { "Settings",       _startGoSettings },
    { "Camera feature", _startGoCamera },
};
static MenuScreen _startMenuScreen("EMBO", kStartItems,
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
    if (sound_is_enabled()) {
        _buzzer.tone(523, 150);
        delay(150); //brief splash hold
        _buzzer.stop();
    } else {
        delay(150);
    }
    delay(1350); //remainder of the splash hold
    Serial.println("UI_INIT: splash hold done, wiring screens");


    _cameraMountScreen.configure("Camera Feature", "Ensure syringe is properly mounted", true);
    _cameraMountScreen.setConfirmTarget(&_cameraStubScreen, true);  // push — Back pops to Start Menu
    _cameraStubScreen.configure("Camera Feature", "Feature idea developing in progress", true);

    _jogMotor1Screen.setMotor(1);
    _jogMotor2Screen.setMotor(2);
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
    _buzzer.update();
}

void ui_show_error(const char *msg) {
    _errorScreen.setMessage(msg);
    _screenManager.goTo(&_errorScreen);
    ble_log("UI: HARDWARE FAULT - %s", msg);
}

void ui_open_bench_diagnostics_menu() {
    _screenManager.push(&bench_diagnostics_menu());
}
