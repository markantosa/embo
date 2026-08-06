#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>

void motors_init();

// Step at given freq (Hz). Pass 0 to stop.
void motor_set_speed(uint8_t motor, uint32_t step_hz);
void motor_set_dir(uint8_t motor, bool forward);
void motor_enable(uint8_t motor, bool en);

// Time-integrated position estimate (steps since last motor_reset_position()
// or last successful homing, whichever is most recent) — see motors.cpp for
// why this is an estimate, not an interrupt-counted position, and why
// that's still good enough for bench/UI purposes. Settles up to the moment
// of the call, so it's accurate even mid-move.
int32_t motor_get_position(uint8_t motor);
void motor_reset_position(uint8_t motor);

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
