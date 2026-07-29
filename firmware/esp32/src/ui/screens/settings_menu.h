#pragma once

#include "menu_screen.h"

// A first concrete menu, built entirely on the generic MenuScreen widget —
// use this as the template for any future menu. It's currently reachable
// only via the BLE debug "MENU" command (see ble_debug.cpp) as a bench
// hook, since neither idle screen has a spare physical input free for it
// yet (BTN1 is deliberately single-purpose, see ui.h) — wire a real
// trigger (e.g. a dedicated button, or a reclaimed long-press) once one is
// chosen; nothing else needs to change.
MenuScreen &settings_menu();
