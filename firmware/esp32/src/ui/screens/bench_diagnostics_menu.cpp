#include "bench_diagnostics_menu.h"
#include "ui_screen_manager.h"
#include "uas.h"
#include "calibration.h"
#include "ble_debug.h"

// Each action is a plain function taking the ScreenManager — that's the
// whole contract a menu item needs to satisfy.

static void _recalUasBaseline(ScreenManager &mgr) {
    uas_calibrate_baseline();
    ble_log("Bench diagnostics: UAS baseline recalibrated");
    mgr.pop();
}

static void _resetBreakageFit(ScreenManager &mgr) {
    calib_breakage_reset();
    ble_log("Bench diagnostics: breakage fit reset");
    mgr.pop();
}

static void _back(ScreenManager &mgr) {
    mgr.pop();
}

static const MenuItem kBenchItems[] = {
    { "Recal. UAS baseline", _recalUasBaseline },
    { "Reset breakage fit",  _resetBreakageFit },
    { "Back",                _back },
};

MenuScreen &bench_diagnostics_menu() {
    static MenuScreen menu("Bench Diagnostics", kBenchItems,
                           sizeof(kBenchItems) / sizeof(kBenchItems[0]));
    return menu;
}
