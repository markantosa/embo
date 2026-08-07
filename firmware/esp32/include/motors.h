#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>

void motors_init();

// Step at given freq (Hz). Pass 0 to stop.
void motor_set_speed(uint8_t motor, uint32_t step_hz);
void motor_set_dir(uint8_t motor, bool forward);
void motor_enable(uint8_t motor, bool en);

// Exact, interrupt-counted position (steps since last motor_reset_position()
// or last successful homing, whichever is most recent) — incremented once
// per step pulse by the hardware-timer ISR that generates them (see
// motors.cpp). Was a time-integrated estimate in an earlier LEDC-PWM-based
// version of this file; ported to real step counting along with the rest
// of the step-generation mechanism from a confirmed-working bench firmware.
int32_t motor_get_position(uint8_t motor);
void motor_reset_position(uint8_t motor);

// Soft position limits — fixed firmware config (MOTOR1/2_SOFT_LIMIT_MIN/MAX
// in config.h), applied once by motors_init(). Not runtime-settable — this
// isn't exposed to the UI/BLE on purpose, so there's no way to loosen or
// mistakenly change a safety bound mid-session. Enforced directly in the
// step-generation ISR (motors.cpp), so ANY caller driving a motor — BLE
// jog, UI jog screens, even a mistaken call during homing — is protected
// uniformly, not just whichever one code path happens to check it. A step
// that would cross a configured bound simply isn't taken; the timer stops
// itself right there.
void motor_set_soft_limits(uint8_t motor, int32_t minPos, int32_t maxPos);
bool motor_at_soft_limit(uint8_t motor);

// Returns true if the limit switch for this motor has tripped since last clear.
bool motor_limit_hit(uint8_t motor);
void motor_clear_limit(uint8_t motor);

// True only if the limit flag is BOTH set AND the switch is still
// genuinely reading closed right now — rejects electrical noise from the
// stepper's own step pulses inducing a spurious edge on the limit line
// (common right after a motor starts moving, well before it's physically
// near the switch). If the flag was set but this determines it was noise,
// clears the flag — but the ISR already zeroed the step pulse
// unconditionally when the edge fired, so a caller getting `false` back
// after the flag was set MUST re-issue motor_set_speed() to actually
// resume motion; this only clears the logical flag. See motors_home() and
// the Jog Motor screens for the two callers.
bool motor_limit_hit_debounced(uint8_t motor);

// StallGuard4 result register (0–1023). Higher = less load.
// Only valid while motor is moving with SpreadCycle active.
uint16_t motor_sg_result(uint8_t motor);

// Drive both motors to their limit switches and back off.
// Blocking. Returns true on success. The scheduler must not start a run until this returns true.
bool motors_home();
bool motors_is_homed();

// Stroke counter — one stroke = one complete forward+return syringe cycle.
// The scheduler calls motor_increment_stroke() after each full cycle.
void     motor_increment_stroke();
uint32_t motor_get_stroke_count();
void     motor_reset_stroke_count();
