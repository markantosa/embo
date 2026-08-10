#pragma once

#include <stdint.h>
#include "LGFX_Config.h"

// App color palette — RGB565 values as given, matching TFT_* library
// constants' format exactly, so they drop into any existing color argument.
// TFT_WHITE/TFT_BLACK (library-provided) already equal COLOR_WHITE/
// COLOR_BLACK exactly, so those two are left as TFT_WHITE/TFT_BLACK
// throughout rather than duplicated under a new name.
#define COLOR_WHITE          0xFFFF  // #FFFFFF
#define COLOR_BLACK          0x0000  // #000000
#define COLOR_CRYSTAL_BELL   0xEF7D  // #EFEFEF
#define COLOR_LUNAR_ROCK     0xC618  // #C5C4C5 — light grey, was for light-text-on-black; now mainly a light button background (e.g. secondary/back buttons)
#define COLOR_WAITING        0x9CF3  // #9C9D9E — medium grey, same light-on-black origin as above
#define COLOR_ASH             0x632C  // #666666 — genuinely dark grey, for secondary/dim TEXT on the white screen backgrounds (COLOR_WAITING/COLOR_LUNAR_ROCK are both too light for that, verified by actual complaint)
#define COLOR_GUILLIMAN_BLUE 0x64DC  // #639AE9
#define COLOR_CLOWN_NOSE     0xE24B  // #EA4758 — danger/error/stop, replaces TFT_RED
#define COLOR_BRIGHT_BLUE    0x0B3E  // #0564F5 — positive/on/selected, replaces TFT_GREEN

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
