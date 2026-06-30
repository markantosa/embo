#pragma once
#include <stdint.h>
#include <stdbool.h>

void motors_init();

// Step at given freq (Hz). Pass 0 to stop.
void motor_set_speed(uint8_t motor, uint32_t step_hz);
void motor_set_dir(uint8_t motor, bool forward);
void motor_enable(uint8_t motor, bool en);

// Returns true if the limit switch for this motor has tripped since last clear.
bool motor_limit_hit(uint8_t motor);
void motor_clear_limit(uint8_t motor);

// StallGuard4 result register (0–1023). Higher = less load.
// Only valid while motor is moving with SpreadCycle active.
uint16_t motor_sg_result(uint8_t motor);

// Drive both motors to their limit switches and back off.
// Blocking. Returns true on success. PID must not start until this returns true.
bool motors_home();
bool motors_is_homed();

// Stroke counter — one stroke = one complete forward+return syringe cycle.
// PID layer calls motor_increment_stroke() after each full cycle.
void     motor_increment_stroke();
uint32_t motor_get_stroke_count();
void     motor_reset_stroke_count();

// Limit switch ISRs — wired in motors_init()
void IRAM_ATTR isr_limit_m1();
void IRAM_ATTR isr_limit_m2();
