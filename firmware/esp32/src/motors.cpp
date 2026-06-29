#include "motors.h"
#include "config.h"
#include <Arduino.h>

// TODO: TMCStepper HardwareSerial instance for half-duplex UART on PIN_TMC_UART

static volatile bool _limit_m1_hit = false;
static volatile bool _limit_m2_hit = false;

void IRAM_ATTR isr_limit_m1() {
    _limit_m1_hit = true;
    // TODO: immediately kill M1 LEDC step output
}

void IRAM_ATTR isr_limit_m2() {
    _limit_m2_hit = true;
    // TODO: immediately kill M2 LEDC step output
}

void motors_init() {
    // Direction + enable pins
    pinMode(PIN_DIR_M1, OUTPUT);
    pinMode(PIN_EN_M1, OUTPUT);
    pinMode(PIN_DIR_M2, OUTPUT);
    pinMode(PIN_EN_M2, OUTPUT);

    // Start disabled (EN active LOW — pulled HIGH = disabled)
    digitalWrite(PIN_EN_M1, HIGH);
    digitalWrite(PIN_EN_M2, HIGH);

    // Limit switch ISRs
    pinMode(PIN_LIMIT_M1, INPUT_PULLUP);
    pinMode(PIN_LIMIT_M2, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_LIMIT_M1), isr_limit_m1, FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_LIMIT_M2), isr_limit_m2, FALLING);

    // TODO: configure LEDC channels for STEP_M1 and STEP_M2
    // TODO: init TMCStepper UART, configure microstepping + current
}

void motor_set_speed(uint8_t motor, uint32_t step_hz) {
    // TODO: set LEDC frequency for the given motor's channel
    (void)motor; (void)step_hz;
}

void motor_set_dir(uint8_t motor, bool forward) {
    if (motor == 1) digitalWrite(PIN_DIR_M1, forward ? HIGH : LOW);
    if (motor == 2) digitalWrite(PIN_DIR_M2, forward ? HIGH : LOW);
}

void motor_enable(uint8_t motor, bool en) {
    if (motor == 1) digitalWrite(PIN_EN_M1, en ? LOW : HIGH);
    if (motor == 2) digitalWrite(PIN_EN_M2, en ? LOW : HIGH);
}
