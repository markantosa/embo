#include "settings_menu.h"
#include "ui_screen_manager.h"
#include "uas.h"
#include "calibration.h"
#include "ble_debug.h"

// Each action is a plain function taking the ScreenManager — that's the
// whole contract a menu item needs to satisfy.

static void _recalUasBaseline(ScreenManager &mgr) {
    uas_calibrate_baseline();
    ble_log("Settings: UAS baseline recalibrated");
    mgr.pop();
}

static void _resetBreakageFit(ScreenManager &mgr) {
    calib_breakage_reset();
    ble_log("Settings: breakage fit reset");
    mgr.pop();
}

static void _back(ScreenManager &mgr) {
    mgr.pop();
}

static const MenuItem kSettingsItems[] = {
    { "Recal. UAS baseline", _recalUasBaseline },
    { "Reset breakage fit",  _resetBreakageFit },
    { "Back",                _back },
};

MenuScreen &settings_menu() {
    static MenuScreen menu("Settings", kSettingsItems,
                           sizeof(kSettingsItems) / sizeof(kSettingsItems[0]));
    return menu;
}
