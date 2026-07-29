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
// Camera size verification is an OPTIONAL, on-demand feature, not part of
// the mixing loop (see scheduler.h) — the operator can trigger it from the
// set-target or done screens (encoder long-press) to have the RPi capture
// one frame, run CV once, and report median/IQR back here for display. It
// does not run continuously and does not drive the mixing schedule.
//
// Button mapping (see config.h for timing constants):
//   EC11 rotary       — adjust target particle size while idle
//   EC11 push-switch  — short press = confirm/start a run (set-target
//                        screen) or dismiss and return to set-target (done /
//                        verify screens)
//                     — held >= EC11_SW_LONGPRESS_MS = request an optional
//                        camera size verification (set-target or done screen)
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

// Opens the settings/diagnostics menu on top of whatever screen is
// currently showing (it returns there afterwards on "Back"). Currently
// only called from the BLE debug "MENU" command (see ble_debug.cpp) as a
// bench hook — wire a real physical trigger once one is chosen; see
// src/ui/screens/settings_menu.h for the menu itself.
void ui_open_settings_menu();
