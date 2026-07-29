#pragma once

#include <stdint.h>

// Shared hardware input layer for the TFT UI: the EC11 rotary + its own
// push-switch, and BTN1. One ISR/debounce implementation lives here and
// every screen reads it through these functions — screens never touch
// pins or interrupts directly, so adding a screen never means touching
// input code.

void ui_input_init();

// Net encoder detents (CW positive, CCW negative) since the last call.
// Typically -1, 0, or +1 per poll, but can be larger if polled slowly.
int ui_input_read_encoder_step();

enum class ButtonEvent { NONE, SHORT_PRESS, LONG_PRESS };

// EC11's own push-switch. Short press = confirm/select (screen-dependent),
// long press = request an optional camera size verification (see ui.h).
ButtonEvent ui_input_poll_enc_sw();

// BTN1 — the dedicated stop button. Short press = graceful stop, long
// press = emergency stop. This is deliberately the one button with one
// job everywhere in the UI (see ui.h's rationale) — screens should call
// this to know if it was pressed, not repurpose it for navigation.
ButtonEvent ui_input_poll_btn1();
