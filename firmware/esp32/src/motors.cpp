#include "motors.h"
#include "config.h"
#include "ble_debug.h"
#include <Arduino.h>
#include <TMCStepper.h>

// Half-duplex UART for both TMC2209 modules on the shared PDN line.
// Serial1 is used; TX and RX are both tied to PIN_TMC_UART via R_UART (1kΩ)
// and R_PDN_UP (10kΩ pull-up) on the main board.
static HardwareSerial _tmc_serial(1);
static TMC2209Stepper _driver_m1(&_tmc_serial, 0.11f, TMC_ADDR_M1);
static TMC2209Stepper _driver_m2(&_tmc_serial, 0.11f, TMC_ADDR_M2);

static volatile bool _limit_m1_hit = false;
static volatile bool _limit_m2_hit = false;

static bool     _homed        = false;
static uint32_t _stroke_count = 0;

void IRAM_ATTR isr_limit_m1() {
    _limit_m1_hit = true;
    // Immediately silence the step pulse so the motor stops within one step.
    // ledcWrite with duty=0 keeps the channel alive but outputs a flat LOW —
    // the driver's STEP pin sees no more edges.
    ledcWrite(LEDC_CH_STEP_M1, 0);
}

void IRAM_ATTR isr_limit_m2() {
    _limit_m2_hit = true;
    ledcWrite(LEDC_CH_STEP_M2, 0);
}

static void _tmc_init_driver(TMC2209Stepper &drv, uint8_t addr) {
    drv.begin();

    // SpreadCycle is required for StallGuard4. This module has no SPREAD pin —
    // must be set via UART at every boot. Read back to confirm before proceeding.
    drv.en_spreadCycle(true);
    bool sc = drv.en_spreadCycle();
    if (!sc) {
        ble_log("TMC addr %u: SpreadCycle write FAILED — SG data unreliable", addr);
    } else {
        ble_log("TMC addr %u: SpreadCycle OK", addr);
    }

    // Use UART-controlled current scaling, not the onboard trimpot.
    // Eliminates run-to-run variability from VREF pot position.
    drv.I_scale_analog(false);
    drv.ihold(10);   // hold current ~31% of RMS — low, motors idle between strokes
    drv.irun(20);    // run current  ~62% of RMS — adjust after load test
    drv.iholddelay(6);

    // 8 microsteps: good balance of resolution and StallGuard sensitivity.
    // StallGuard becomes less reliable above 16 microsteps at low speed.
    drv.microsteps(8);

    // Enable StallGuard across the full speed range.
    // SG_RESULT is only valid when TSTEP < TCOOLTHRS. Setting TCOOLTHRS to
    // max (0xFFFFF) means StallGuard is active at all motor speeds.
    drv.TCOOLTHRS(0xFFFFF);

    // Verify GCONF readback as a basic comms sanity check.
    uint32_t gconf = drv.GCONF();
    ble_log("TMC addr %u: GCONF=0x%08X", addr, gconf);
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

    // STEP pins: LEDC at 1 Hz, 50% duty, timer resolution 10-bit.
    // Frequency is changed by motor_set_speed(); 1 Hz just initialises the channel.
    // Duty is fixed at half the timer period so the pulse is always a clean square wave.
    ledcSetup(LEDC_CH_STEP_M1, 1, 10);
    ledcSetup(LEDC_CH_STEP_M2, 1, 10);
    ledcAttachPin(PIN_STEP_M1, LEDC_CH_STEP_M1);
    ledcAttachPin(PIN_STEP_M2, LEDC_CH_STEP_M2);
    ledcWrite(LEDC_CH_STEP_M1, 0);  // keep silent until explicitly started
    ledcWrite(LEDC_CH_STEP_M2, 0);

    // Limit switch ISRs
    pinMode(PIN_LIMIT_M1, INPUT_PULLUP);
    pinMode(PIN_LIMIT_M2, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_LIMIT_M1), isr_limit_m1, FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_LIMIT_M2), isr_limit_m2, FALLING);

    // TMC2209 UART — half-duplex: TX and RX on the same physical pin.
    // begin() with the same pin for both directs the ESP32 UART peripheral
    // into half-duplex mode.
    _tmc_serial.begin(BAUD_TMC, SERIAL_8N1, PIN_TMC_UART, PIN_TMC_UART);
    delay(50);  // allow drivers to finish power-on reset before any UART traffic

    _tmc_init_driver(_driver_m1, TMC_ADDR_M1);
    _tmc_init_driver(_driver_m2, TMC_ADDR_M2);
}

void motor_set_speed(uint8_t motor, uint32_t step_hz) {
    uint8_t ch = (motor == 1) ? LEDC_CH_STEP_M1 : LEDC_CH_STEP_M2;

    if (step_hz == 0) {
        ledcWrite(ch, 0);  // flat LOW — no steps
        return;
    }

    // Change frequency while keeping 50% duty cycle.
    // Timer resolution stays at 10 bits; duty = 512 = half of 1024.
    ledcSetup(ch, step_hz, 10);
    ledcWrite(ch, 512);
}

void motor_set_dir(uint8_t motor, bool forward) {
    if (motor == 1) digitalWrite(PIN_DIR_M1, forward ? HIGH : LOW);
    if (motor == 2) digitalWrite(PIN_DIR_M2, forward ? HIGH : LOW);
}

void motor_enable(uint8_t motor, bool en) {
    if (motor == 1) digitalWrite(PIN_EN_M1, en ? LOW : HIGH);
    if (motor == 2) digitalWrite(PIN_EN_M2, en ? LOW : HIGH);
}

bool motor_limit_hit(uint8_t motor) {
    return (motor == 1) ? _limit_m1_hit : _limit_m2_hit;
}

void motor_clear_limit(uint8_t motor) {
    if (motor == 1) _limit_m1_hit = false;
    if (motor == 2) _limit_m2_hit = false;
}

uint16_t motor_sg_result(uint8_t motor) {
    // StallGuard result — valid only with SpreadCycle enabled and motor moving.
    // Higher value = less load. Used by PID as viscosity proxy.
    return (motor == 1) ? _driver_m1.SG_RESULT() : _driver_m2.SG_RESULT();
}

// ── Homing ────────────────────────────────────────────────────────────────────

static void _home_single(uint8_t motor) {
    motor_clear_limit(motor);
    motor_set_dir(motor, HOMING_FORWARD);
    motor_enable(motor, true);
    motor_set_speed(motor, HOMING_STEP_HZ);
}

static void _backoff_single(uint8_t motor) {
    // Reverse direction and run at homing speed for exactly the backoff duration.
    // Time = steps / freq. Motor is already enabled from the approach phase.
    motor_set_speed(motor, 0);
    motor_set_dir(motor, !HOMING_FORWARD);
    uint32_t backoff_ms = (HOMING_BACKOFF_STEPS * 1000UL) / HOMING_STEP_HZ;
    motor_set_speed(motor, HOMING_STEP_HZ);
    delay(backoff_ms);
    motor_set_speed(motor, 0);
    motor_enable(motor, false);
}

bool motors_home() {
    ble_log("Homing: starting");
    _homed = false;

    // Drive both motors toward their limit switches simultaneously.
    _home_single(1);
    _home_single(2);

    uint32_t deadline = millis() + HOMING_TIMEOUT_MS;
    while (millis() < deadline) {
        bool m1_done = motor_limit_hit(1);
        bool m2_done = motor_limit_hit(2);
        if (m1_done && m2_done) break;
        delay(1);
    }

    bool m1_ok = motor_limit_hit(1);
    bool m2_ok = motor_limit_hit(2);

    // Stop any motor that timed out without tripping (safety).
    if (!m1_ok) { motor_set_speed(1, 0); motor_enable(1, false); }
    if (!m2_ok) { motor_set_speed(2, 0); motor_enable(2, false); }

    if (!m1_ok || !m2_ok) {
        ble_log("Homing: FAILED (M1=%d M2=%d) — check limit switches", m1_ok, m2_ok);
        return false;
    }

    // Both limits tripped. Back off to clear the switch contacts.
    _backoff_single(1);
    _backoff_single(2);

    _homed = true;
    _stroke_count = 0;
    ble_log("Homing: complete");
    return true;
}

bool motors_is_homed() {
    return _homed;
}

// ── Stroke counter ────────────────────────────────────────────────────────────
// A stroke is one complete forward+return syringe cycle.
// The PID layer calls motor_increment_stroke() after each full cycle.

void motor_increment_stroke() {
    _stroke_count++;
}

uint32_t motor_get_stroke_count() {
    return _stroke_count;
}

void motor_reset_stroke_count() {
    _stroke_count = 0;
}
