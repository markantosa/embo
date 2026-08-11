#pragma once

#include <cstdint>

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
//   EC11 rotary       — adjust target particle size (Mixing Menu)
//   EC11 push-switch  — context-dependent: home/continue, confirm/start,
//                        menu-select, toggle — see each screen. On the
//                        Mixing Running screen specifically: ANY press
//                        (short or long) = emergency stop, same as BTN1.
//                     — held >= EC11_SW_LONGPRESS_MS = request an optional
//                        camera size verification (Mixing Menu or End screen)
//   BTN1               — two roles, never both at once because they're on
//                        different screens:
//                          On the Mixing Running screen ONLY: dedicated
//                          stop button, no other function — ANY press
//                          (short or long) = emergency stop (kills motor
//                          power immediately). No separate graceful-stop
//                          behavior on this screen anymore — see
//                          mixing_running_screen.h.
//                          On every other (non-running) screen: short
//                          press = Back — same as that screen's touch Back
//                          button / encoder long-press fallback. This is
//                          deliberately scoped to screens where nothing is
//                          moving, so there's never a moment where BTN1's
//                          meaning is ambiguous or could be mistaken for
//                          the stop function — see mixing_running_screen.h.

void ui_init();
void ui_update();   // call every loop()

// Plays a brief tone via the shared buzzer, IF Settings > Sound is
// currently on — callers don't need to check sound_is_enabled()
// themselves, this gates internally so every caller behaves consistently.
// Non-blocking (BuzzerDriver's own tone(), auto-stops after durationMs) —
// safe to call from anywhere, including right before a blocking call like
// motors_home(), without delaying it.
void ui_chirp(uint32_t frequencyHz, uint32_t durationMs);

// Switches to a persistent error screen (e.g. a failed homing attempt).
// There is no way back from this screen short of a reboot — a hardware
// fault serious enough to fail homing needs investigation, not a retry.
void ui_show_error(const char *msg);

// Opens the bench/engineering diagnostics menu (recalibration, fit reset)
// on top of whatever screen is currently showing — NOT the operator-facing
// Settings screen (Start Menu > Settings), which is reached normally
// through the menu tree. Only called from the BLE debug "MENU" command
// (see ble_debug.cpp) as a bench hook; see
// src/ui/screens/bench_diagnostics_menu.h for the menu itself.
void ui_open_bench_diagnostics_menu();
