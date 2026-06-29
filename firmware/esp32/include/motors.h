#pragma once
#include <stdint.h>

void motors_init();

// Step at given freq (Hz). Pass 0 to stop.
void motor_set_speed(uint8_t motor, uint32_t step_hz);
void motor_set_dir(uint8_t motor, bool forward);
void motor_enable(uint8_t motor, bool en);

// Limit switch ISRs — wired in motors_init()
void IRAM_ATTR isr_limit_m1();
void IRAM_ATTR isr_limit_m2();
