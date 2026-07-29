#pragma once

#include <stdint.h>
#include "LGFX_Config.h"

// Shared TFT instance + small drawing helpers used by every screen, so
// adding a screen never means re-deriving "how do I center some text."

LGFX &ui_display_tft();

void ui_display_draw_centered(const char *text, int16_t y, uint16_t color, uint8_t size);

// Animated "..." busy indicator, shared by any screen that needs one
// (RunningScreen, VerifyingScreen, ...). Keeps its own timing state
// internally — just call it every update() and it redraws itself at the
// right cadence.
void ui_display_draw_spinner(int16_t cy);
