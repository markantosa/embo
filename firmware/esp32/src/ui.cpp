#include "ui.h"
#include "config.h"
#include "ui_screen_manager.h"
#include "ui_input.h"
#include "ui_display.h"
#include "ui/screens/placeholder_screen.h"
#include "ui/screens/menu_screen.h"
#include "ui/screens/telemetry_screen.h"
#include "ui/screens/uas_debug_toggle_screen.h"
#include "mixing_options.h"
#include "ui/screens/mixing_menu_screen.h"
#include "ui/screens/viscosity_menu_screen.h"
#include "ui/screens/warning_screen.h"
#include "ui/screens/mixing_running_screen.h"
#include "ui/screens/end_screen.h"
#include "ui/screens/verifying_screen.h"
#include "ui/screens/error_screen.h"
#include "ui/screens/bench_diagnostics_menu.h"
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
//                                -> [Settings] -> [Motion] -> [Home Motors]
//                                              -> [Developer mode] -> [UAS debug mode] (BLE toggle)
//                                -> [Camera feature] -> Verifying (camera size check)
//   End screen and Mixing/Warning screens can also reach Verifying (camera
//   size check) via encoder long-press, same as before this redesign.

static BuzzerDriver _buzzer(PIN_BUZ_PWM, 2);  // channel 2 — motors explicitly own 0 and 1, see motors.cpp

// ── Screens with no constructor-time cross-references (all wired via
// post-construction methods in ui_init(), so declaration order among these
// doesn't matter) ────────────────────────────────────────────────────────────
static VerifyingScreen       _verifyingScreen;
static ErrorScreen           _errorScreen;
static UasDebugToggleScreen  _uasDebugToggleScreen;
static TelemetryScreen       _telemetryScreen;
static MixingMenuScreen      _mixingMenuScreen;
static ViscosityMenuScreen   _viscosityMenuScreen;
static WarningScreen         _warningScreen;
static MixingRunningScreen   _mixingRunningScreen;
static EndScreen             _endScreen;
static PlaceholderScreen     _insertSyringeScreen;
static StrokeTestScreen      _strokeTestScreen;

// ── Move Left/Right Motor — Settings > Motion > Move Left/Right Motor.
// Discrete 10-step nudge per press, not continuous jogging. Up = CCW
// (anticlockwise), Down = CW (clockwise) — same DIR-pin convention
// documented in motors.cpp/config.h (HOMING_FORWARD=false is
// anticlockwise). Defined before Motion Menu since it references these by
// address (same ordering rule as the rest of this file). ──────────────────
#define MOVE_MOTOR_STEP_COUNT 1000

static void _moveMotorSteps(uint8_t motor, bool clockwise) {
    if (scheduler_is_running()) {
        ble_log("Motion > Move Motor: refused - a run is in progress");
        return;
    }
    // Blocking for the duration of the move — same tradeoff already
    // accepted for every other Motion menu action (Home Motors, Stroke
    // Testing). At MOVE_MOTOR_STEP_COUNT=1000 and MOTOR_JOG_HZ this is
    // ~half a second per press, not the ~5ms it was at the old 10-step
    // count — noticeable but still a normal, bounded UI-blocking duration,
    // not a concern like a homing-length wait would be.
    motor_set_dir(motor, clockwise);
    motor_enable(motor, true);
    motor_set_speed(motor, MOTOR_JOG_HZ);
    uint32_t moveMs = (MOVE_MOTOR_STEP_COUNT * 1000UL) / MOTOR_JOG_HZ;
    delay(moveMs);
    motor_set_speed(motor, 0);
    motor_enable(motor, false);
    ble_log("Motion > Move Motor: motor %u %s %d steps (position=%ld)",
            motor, clockwise ? "CW" : "CCW", MOVE_MOTOR_STEP_COUNT, (long)motor_get_position(motor));
}

static void _moveLeftUp(ScreenManager &mgr)    { _moveMotorSteps(1, false); }  // CCW
static void _moveLeftDown(ScreenManager &mgr)  { _moveMotorSteps(1, true); }   // CW
static const MenuItem kMoveLeftItems[] = {
    { "Up",   _moveLeftUp },
    { "Down", _moveLeftDown },
};
static MenuScreen _moveLeftMenuScreen("Move Left Motor", kMoveLeftItems,
                                       sizeof(kMoveLeftItems) / sizeof(kMoveLeftItems[0]));

static void _moveRightUp(ScreenManager &mgr)   { _moveMotorSteps(2, false); }  // CCW
static void _moveRightDown(ScreenManager &mgr) { _moveMotorSteps(2, true); }   // CW
static const MenuItem kMoveRightItems[] = {
    { "Up",   _moveRightUp },
    { "Down", _moveRightDown },
};
static MenuScreen _moveRightMenuScreen("Move Right Motor", kMoveRightItems,
                                        sizeof(kMoveRightItems) / sizeof(kMoveRightItems[0]));

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
static void _motionMoveLeft(ScreenManager &mgr)  { mgr.push(&_moveLeftMenuScreen); }
static void _motionMoveRight(ScreenManager &mgr) { mgr.push(&_moveRightMenuScreen); }

// "Left"/"right" = Motor 1/Motor 2 — a naming choice used throughout this
// test; nothing in firmware itself distinguishes left/right beyond this.
static void _motionTestBoth(ScreenManager &mgr) { mgr.push(&_strokeTestScreen); }

static const MenuItem kMotionItems[] = {
    { "Home Motors",      _motionHome },
    { "Move Left Motor",  _motionMoveLeft },
    { "Move Right Motor", _motionMoveRight },
    { "Stroke Testing",   _motionTestBoth },
};
static MenuScreen _motionMenuScreen("Motion", kMotionItems,
                                     sizeof(kMotionItems) / sizeof(kMotionItems[0]));

// ── Developer Mode submenu — Settings > Developer mode. Defined before
// Settings Menu since it references this by address (same ordering rule
// as the rest of this file). ────────────────────────────────────────────
static void _devGoTelemetry(ScreenManager &mgr) { mgr.push(&_telemetryScreen); }
static void _devGoUasDebug(ScreenManager &mgr)  { mgr.push(&_uasDebugToggleScreen); }

static const MenuItem kDeveloperModeItems[] = {
    { "Telemetry",     _devGoTelemetry },
    { "UAS Debug Mode", _devGoUasDebug },
};
static MenuScreen _developerModeMenuScreen("Developer Mode", kDeveloperModeItems,
                                            sizeof(kDeveloperModeItems) / sizeof(kDeveloperModeItems[0]));

// ── Settings Menu — defined before Start Menu since Start Menu's "Settings"
// item needs to reference it by address (see file header for why this
// order matters: MenuItem callback bodies need referenced statics already
// declared, unlike .wire() calls in ui_init() which run after everything
// exists regardless of order). ────────────────────────────────────────────
static void _settingsGoMotion(ScreenManager &mgr)    { mgr.push(&_motionMenuScreen); }
static void _settingsGoDeveloper(ScreenManager &mgr) { mgr.push(&_developerModeMenuScreen); }

static const MenuItem kSettingsItems[] = {
    { "Motion",         _settingsGoMotion },
    { "Developer mode", _settingsGoDeveloper },
};
static MenuScreen _settingsMenuScreen("Settings v" FIRMWARE_VERSION, kSettingsItems,
                                       sizeof(kSettingsItems) / sizeof(kSettingsItems[0]));

// ── Target Type — Start > Agent selection > here > Mixing Menu (Size) or
// Viscosity Menu (Viscosity). Defined before Agent Selection since Agent
// Selection's items need to reference it (same ordering rule as Settings
// Menu above). ────────────────────────────────────────────────────────────
static void _targetTypeGoSize(ScreenManager &mgr) {
    mixing_options_set_target_type(TargetType::SIZE);
    mgr.push(&_mixingMenuScreen);
}
static void _targetTypeGoViscosity(ScreenManager &mgr) {
    mixing_options_set_target_type(TargetType::VISCOSITY);
    mgr.push(&_viscosityMenuScreen);
}

// Which target type is selectable depends on the agent chosen earlier
// (Agent Selection, upstream of here) — Gelfoam -> Size only, Lyostypt ->
// Viscosity only. Both items always stay visible in the list; the
// disallowed one is just greyed out and can't be confirmed (see
// menu_screen.h's isEnabled feature).
static bool _targetTypeSizeEnabled() {
    return mixing_options_get_agent() == SyringeAgent::Gelfoam;
}
static bool _targetTypeViscosityEnabled() {
    return mixing_options_get_agent() == SyringeAgent::Lyostypt;
}

static const MenuItem kTargetTypeItems[] = {
    { "Size",      _targetTypeGoSize,      _targetTypeSizeEnabled },
    { "Viscosity", _targetTypeGoViscosity, _targetTypeViscosityEnabled },
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
static void _startGoCamera(ScreenManager &mgr)   { mgr.push(&_verifyingScreen); }

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
    _buzzer.tone(523, 150);
    delay(150); //brief splash hold
    _buzzer.stop();
    delay(1350); //remainder of the splash hold
    Serial.println("UI_INIT: splash hold done, wiring screens");

    // Developer Mode submenu needs no wiring — its two items push
    // _telemetryScreen and _uasDebugToggleScreen directly, both already
    // existing static screens (see kDeveloperModeItems above).
    _mixingMenuScreen.wire(_warningScreen, _verifyingScreen);
    _viscosityMenuScreen.wire(_warningScreen, _verifyingScreen);
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
