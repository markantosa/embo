#pragma once

// TFT UI — LovyanGFX, encoder + one button, no touch.
//
// Deliberately minimal per project decision: the screen shows only a
// loading screen while a run is in progress and a final result once done —
// it is NOT a live sensor readout. Raw sensor data / diagnostics live on the
// BLE dashboard (testing/PCB_Test_Firmware_v3_4/web) instead; that BLE link
// stays operational at all times for exactly that purpose, independent of
// whatever this screen is showing.
//
// Button mapping (see config.h for timing constants):
//   EC11 rotary       — adjust target particle size while idle; confirm/
//                        start a run when pressed (short press)
//   BTN1               — dedicated stop button, no start function:
//                          short press = graceful stop (finish current
//                          stroke, then hold)
//                          held >= BTN1_LONGPRESS_MS = emergency stop
//                          (kills motor power immediately)

void ui_init();
void ui_update();   // call every loop()

// Switches to a persistent error screen (e.g. homing failed at boot).
// There is no way back from this screen short of a reboot — a hardware
// fault serious enough to fail homing needs investigation, not a retry.
void ui_show_error(const char *msg);
