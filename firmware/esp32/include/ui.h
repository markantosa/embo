#pragma once

void ui_init();
void ui_update();   // call every loop()

// BTN1 = start (only takes effect when idle and homed).
// BTN2 = stop / e-stop — always live, kills motors immediately regardless
// of PID state. Both are polled+debounced inside ui_update().
