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

// Draws a filled, labeled touch button at the given rect — pair with a
// ui_input::TouchButton of the same rect so the drawn button and the
// tappable region always match. Centers the label within the rect (unlike
// ui_display_draw_centered, which centers across the whole screen width).
void ui_display_draw_touch_button(int16_t x, int16_t y, int16_t w, int16_t h,
                                   const char *label, uint16_t bgColor, uint16_t textColor);
