#pragma once

#include "menu_screen.h"

// Bench/diagnostics menu — recalibration + fit-reset actions for engineers
// on the bench, NOT the operator-facing Settings screen (see
// settings_menu_items.cpp / ui.cpp's real "Settings" MenuScreen, reached
// from the Start Menu). This one is only reachable via the BLE debug
// "MENU" command (see ble_debug.cpp), since neither idle screen has a
// spare physical input free for it — the operator-facing Settings screen
// occupies that territory now.
MenuScreen &bench_diagnostics_menu();
