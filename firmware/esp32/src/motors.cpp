#include "motors.h"
#include "config.h"
#include "ble_debug.h"
#include "force_sensor.h"
#include <Arduino.h>
#include <TMCStepper.h>
#include <esp_timer.h>
#include <cstdint>

// Half-duplex UART for both TMC2209 modules on the shared PDN line.
// Serial1 is used; TX (GPIO4) and RX (GPIO44) are genuinely separate ESP32
// pins as of v3.4, both wired to the same physical PDN_UART bus node via
// R_UART (1kΩ) + R_PDN_UP (10kΩ pull-up) on the main board — a same-pin
// shared TX/RX config was tried first and proved unreliable, see
// docs/EMBO_PCB_Design_Brief_v3_4.txt §7.3.
static HardwareSerial _tmc_serial(1);
static TMC2209Stepper _driver_m1(&_tmc_serial, TMC_R_SENSE, TMC_ADDR_M1);
static TMC2209Stepper _driver_m2(&_tmc_serial, TMC_R_SENSE, TMC_ADDR_M2);

static volatile bool _limit_m1_hit = false;
static volatile bool _limit_m2_hit = false;

static bool     _homed        = false;
static uint32_t _stroke_count = 0;

// ── Step generation: hardware timer (esp_timer), ISR-ticked, one pulse per
// callback — ported from a confirmed-working bench firmware
// (PCB_Test_Firmware_v3_4) after the previous LEDC PWM-based driver here
// could not move the motors at all. Root cause turned out to be upstream
// in the TMC2209 init sequence (missing toff/pdn_disable/senddelay, see
// _tmc_init_driver below), not the step-generation mechanism itself — but
// this port keeps the whole confirmed-working sequence together rather
// than mixing a validated init with an unvalidated pulse generator.
// Bonus: position is now an exact ISR-incremented step count, not a
// time-integrated estimate — see motor_get_position()/motor_reset_position().
struct StepAxis {
    int stepPin, dirPin, enPin;
    volatile int8_t  dir      = 0;  // -1 / 0 / +1 — 0 means stopped
    volatile int32_t position = 0;  // exact, ISR-incremented step count
    volatile int32_t softMin  = INT32_MIN;  // disabled by default — see motor_set_soft_limits()
    volatile int32_t softMax  = INT32_MAX;
    esp_timer_handle_t timer  = nullptr;
};
static StepAxis _axis1{PIN_STEP_M1, PIN_DIR_M1, PIN_EN_M1};
static StepAxis _axis2{PIN_STEP_M2, PIN_DIR_M2, PIN_EN_M2};

static StepAxis &_axis(uint8_t motor) { return (motor == 1) ? _axis1 : _axis2; }

static void IRAM_ATTR _stepPulse(void *arg) {
    StepAxis *ax = static_cast<StepAxis *>(arg);
    if (ax->dir == 0) return;  // defensive — esp_timer_stop() is what actually prevents firing
    int32_t nextPos = ax->position + ax->dir;
    if (nextPos < ax->softMin || nextPos > ax->softMax) {
        // Would cross a configured soft limit — stop right here, don't
        // take this step. Reversing direction and re-issuing speed moves
        // back away from the limit normally; this only blocks the
        // direction that would go further past it.
        esp_timer_stop(ax->timer);
        ax->dir = 0;
        return;
    }
    digitalWrite(ax->stepPin, HIGH);
    delayMicroseconds(2);
    digitalWrite(ax->stepPin, LOW);
    ax->position = nextPos;
}

void IRAM_ATTR isr_limit_m1() {
    _limit_m1_hit = true;
    // Immediately silence stepping so the motor stops within one step.
    esp_timer_stop(_axis1.timer);  // safe even if not currently running
}

void IRAM_ATTR isr_limit_m2() {
    _limit_m2_hit = true;
    esp_timer_stop(_axis2.timer);
}

static void _tmc_init_driver(TMC2209Stepper &drv, uint8_t addr) {
    drv.begin();

    // TOFF=0 explicitly disables the TMC2209's driver output stage — a
    // hardware fact about this chip, not a library default to trust.
    // Without this, STEP pulses can arrive with EN correctly asserted and
    // the motor still never produces torque. This was the actual root
    // cause of "no movement at all" — confirmed by comparing against
    // PCB_Test_Firmware_v3_4, a bench firmware that moves these same
    // modules correctly and always sets this.
    drv.toff(4);

    // Real calibrated current via TMCStepper's own rms_current() (which
    // does the IHOLD/IRUN register math from actual mA figures using
    // TMC_R_SENSE), not hand-picked raw register values.
    drv.rms_current(TMC_RUN_CURRENT_MA, (float)TMC_HOLD_CURRENT_MA / (float)TMC_RUN_CURRENT_MA);

    // PDN_UART is a multiplexed pin on this chip — without this, it can be
    // misread as an external power-down signal instead of pure UART
    // traffic. Matters more than usual here since both modules share one
    // PDN_UART bus (multi-drop, see EMBO_PCB_Design_Brief_v3_4.txt §7.3).
    drv.pdn_disable(true);

    // Use UART-controlled current scaling, not the onboard trimpot.
    drv.I_scale_analog(false);

    // SpreadCycle is required for StallGuard4. This module has no SPREAD pin —
    // must be set via UART at every boot. Read back to confirm before proceeding.
    drv.en_spreadCycle(true);
    bool sc = drv.en_spreadCycle();
    if (!sc) {
        ble_log("TMC addr %u: SpreadCycle write FAILED — SG data unreliable", addr);
    } else {
        ble_log("TMC addr %u: SpreadCycle OK", addr);
    }

    // Multi-drop UART bus timing — the TMC2209 datasheet specifically
    // calls for SENDDELAY >= 2 "in a multiple node system... to ensure
    // clean bus transitions," and that's exactly this board's topology
    // (2 drivers, 1 shared PDN_UART bus).
    drv.senddelay(4);

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

    // TMCStepper's own dedicated UART link-check — 0 = good connection,
    // non-zero = bad. More definitive than inferring from SpreadCycle
    // readback alone, since a corrupted read could coincidentally still
    // look like it "worked."
    uint8_t conn = drv.test_connection();
    ble_log("TMC addr %u: test_connection() = %u (%s)", addr, conn, conn == 0 ? "OK" : "BAD LINK");
}

void motors_init() {
    // Direction + enable + step pins
    pinMode(PIN_DIR_M1, OUTPUT);
    pinMode(PIN_EN_M1, OUTPUT);
    pinMode(PIN_STEP_M1, OUTPUT);
    pinMode(PIN_DIR_M2, OUTPUT);
    pinMode(PIN_EN_M2, OUTPUT);
    pinMode(PIN_STEP_M2, OUTPUT);
    digitalWrite(PIN_STEP_M1, LOW);
    digitalWrite(PIN_DIR_M1, LOW);
    digitalWrite(PIN_STEP_M2, LOW);
    digitalWrite(PIN_DIR_M2, LOW);

    // Start disabled (EN active LOW — pulled HIGH = disabled)
    digitalWrite(PIN_EN_M1, HIGH);
    digitalWrite(PIN_EN_M2, HIGH);

    // One hardware timer per axis, one pulse generated per callback.
    esp_timer_create_args_t args1 = {};
    args1.callback = &_stepPulse;
    args1.arg = &_axis1;
    args1.name = "m1step";
    esp_timer_create(&args1, &_axis1.timer);

    esp_timer_create_args_t args2 = {};
    args2.callback = &_stepPulse;
    args2.arg = &_axis2;
    args2.name = "m2step";
    esp_timer_create(&args2, &_axis2.timer);

    // Limit switch ISRs
    pinMode(PIN_LIMIT_M1, INPUT_PULLUP);
    pinMode(PIN_LIMIT_M2, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_LIMIT_M1), isr_limit_m1, FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_LIMIT_M2), isr_limit_m2, FALLING);

    // TMC2209 UART — separate TX/RX GPIOs, same shared bus node (see above).
    _tmc_serial.begin(BAUD_TMC, SERIAL_8N1, PIN_TMC_UART_RX, PIN_TMC_UART_TX);
    delay(50);  // allow drivers to finish power-on reset before any UART traffic

    _tmc_init_driver(_driver_m1, TMC_ADDR_M1);
    _tmc_init_driver(_driver_m2, TMC_ADDR_M2);

    // Applied from boot, before any homing — position at startup is
    // unknown/arbitrary (not yet zeroed against a real reference), but the
    // configured range is what should be in effect by default regardless.
    // motors_home() itself temporarily disables these during the actual
    // homing approach (position isn't meaningful yet at that point either)
    // and restores them once homing establishes a known zero — see there.
    motor_set_soft_limits(1, MOTOR1_SOFT_LIMIT_MIN, MOTOR1_SOFT_LIMIT_MAX);
    motor_set_soft_limits(2, MOTOR2_SOFT_LIMIT_MIN, MOTOR2_SOFT_LIMIT_MAX);
}

void motor_set_speed(uint8_t motor, uint32_t step_hz) {
    StepAxis &ax = _axis(motor);
    if (step_hz == 0) {
        esp_timer_stop(ax.timer);  // safe even if not currently running
        return;
    }
    uint64_t periodUs = 1000000ULL / step_hz;
    esp_timer_stop(ax.timer);
    esp_timer_start_periodic(ax.timer, periodUs);
}

void motor_set_dir(uint8_t motor, bool forward) {
    StepAxis &ax = _axis(motor);
    ax.dir = forward ? 1 : -1;
    digitalWrite(ax.dirPin, forward ? HIGH : LOW);
}

void motor_enable(uint8_t motor, bool en) {
    StepAxis &ax = _axis(motor);
    digitalWrite(ax.enPin, en ? LOW : HIGH);
    if (!en) {
        esp_timer_stop(ax.timer);
        ax.dir = 0;
    }
}

int32_t motor_get_position(uint8_t motor) {
    return _axis(motor).position;  // exact ISR-incremented count, no settling needed
}

void motor_reset_position(uint8_t motor) {
    _axis(motor).position = 0;
}

void motor_set_soft_limits(uint8_t motor, int32_t minPos, int32_t maxPos) {
    StepAxis &ax = _axis(motor);
    ax.softMin = minPos;
    ax.softMax = maxPos;
}

bool motor_at_soft_limit(uint8_t motor) {
    StepAxis &ax = _axis(motor);
    return ax.position <= ax.softMin || ax.position >= ax.softMax;
}

bool motor_limit_hit(uint8_t motor) {
    return (motor == 1) ? _limit_m1_hit : _limit_m2_hit;
}

void motor_clear_limit(uint8_t motor) {
    if (motor == 1) _limit_m1_hit = false;
    if (motor == 2) _limit_m2_hit = false;
}

// A tripped limit flag might be electrical noise from the stepper's own
// step pulses inducing a spurious edge on the limit line — this can
// happen almost immediately after the motor starts moving, well before
// it's anywhere near the physical switch. A genuine closure stays LOW;
// noise doesn't. Returns true only if the switch is still actually
// reading LOW right now. If the flag was set but the pin has since gone
// HIGH again, this clears the flag (treating it as noise) and returns
// false — but note the ISR already stopped the step timer unconditionally
// when the edge first fired (see isr_limit_m1/m2), so a caller that gets
// false back MUST re-issue motor_set_speed() to actually resume motion;
// this function only clears the logical flag, it doesn't restart stepping.
bool motor_limit_hit_debounced(uint8_t motor) {
    if (!motor_limit_hit(motor)) return false;
    uint8_t pin = (motor == 1) ? PIN_LIMIT_M1 : PIN_LIMIT_M2;
    if (digitalRead(pin) == HIGH) {
        motor_clear_limit(motor);
        return false;  // was noise, not a genuine closure
    }
    return true;
}

uint16_t motor_sg_result(uint8_t motor) {
    // StallGuard result — valid only with SpreadCycle enabled and motor moving.
    // Higher value = less load. Currently diagnostic-only (read via BLE
    // debug's MOVE/SG output, see ble_debug.cpp) — not consumed by the
    // scheduler, which deliberately isn't a PID (see scheduler.h).
    return (motor == 1) ? _driver_m1.SG_RESULT() : _driver_m2.SG_RESULT();
}

// ── Homing ────────────────────────────────────────────────────────────────────

static void _home_single(uint8_t motor) {
    motor_clear_limit(motor);
    motor_set_dir(motor, HOMING_FORWARD);
    motor_enable(motor, true);
    motor_set_speed(motor, HOMING_STEP_HZ);
}

// Brief clockwise nudge before the real anticlockwise approach — see
// HOMING_PRE_NUDGE_STEPS (config.h) for why. Both motors nudge
// CONCURRENTLY (started together, one shared delay), matching how the
// real approach also drives both simultaneously rather than one after
// the other.
static void _preNudgeBoth() {
    motor_set_dir(1, !HOMING_FORWARD);  // clockwise
    motor_enable(1, true);
    motor_set_speed(1, HOMING_STEP_HZ);
    motor_set_dir(2, !HOMING_FORWARD);
    motor_enable(2, true);
    motor_set_speed(2, HOMING_STEP_HZ);

    uint32_t nudgeMs = (HOMING_PRE_NUDGE_STEPS * 1000UL) / HOMING_STEP_HZ;
    delay(nudgeMs);

    motor_set_speed(1, 0);
    motor_set_speed(2, 0);
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
#ifdef BENCH_NO_HOMING
    // BENCH-ONLY (embo_bench env, platformio.ini): skips real homing so the
    // UI/BLE console can be exercised with nothing connected. This must
    // never be true in a build that goes anywhere near real hardware — see
    // the banner this drives on the Mixing Menu screen (mixing_menu_screen.cpp)
    // and the loud log line below.
    ble_log("Homing: SKIPPED — BENCH_NO_HOMING build, motors/limit switches not required");
    ble_log("Homing: *** THIS IS A BENCH-ONLY BUILD — DO NOT USE WITH REAL HARDWARE ***");
    motor_reset_position(1);
    motor_reset_position(2);
    force_sensor_tare();  // auto-tare after every home — see the real path below for why
    _homed = true;
    _stroke_count = 0;
    return true;
#else
    ble_log("Homing: starting");
    _homed = false;

    // Position is unknown/arbitrary before a home reference exists, so a
    // configured soft-limit range (which is relative to a homed zero
    // point) can't be trusted to actually include the real limit switch —
    // disable soft limits for the duration of the approach itself.
    // Restored below once homing actually establishes position 0, on
    // every exit path (success or failure), so this never leaves limits
    // silently disabled if homing doesn't complete.
    motor_set_soft_limits(1, INT32_MIN, INT32_MAX);
    motor_set_soft_limits(2, INT32_MIN, INT32_MAX);

    // Drive both motors toward their limit switches simultaneously.
    ble_log("Homing: clockwise pre-nudge (%d steps)", HOMING_PRE_NUDGE_STEPS);
    _preNudgeBoth();
    _home_single(1);
    _home_single(2);

    uint32_t deadline = millis() + HOMING_TIMEOUT_MS;
    while (millis() < deadline) {
        // If a flag tripped but wasn't a genuine closure (noise), resume
        // driving toward the limit — the ISR already stopped stepping, so
        // this has to explicitly restart it.
        if (motor_limit_hit(1) && !motor_limit_hit_debounced(1)) {
            motor_set_speed(1, HOMING_STEP_HZ);
        }
        if (motor_limit_hit(2) && !motor_limit_hit_debounced(2)) {
            motor_set_speed(2, HOMING_STEP_HZ);
        }

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
        // Restore configured limits even on failure — position is still
        // not meaningful (never got zeroed), but there's no reason to
        // leave the board with soft limits silently disabled indefinitely
        // just because this attempt didn't complete.
        motor_set_soft_limits(1, MOTOR1_SOFT_LIMIT_MIN, MOTOR1_SOFT_LIMIT_MAX);
        motor_set_soft_limits(2, MOTOR2_SOFT_LIMIT_MIN, MOTOR2_SOFT_LIMIT_MAX);
        return false;
    }

    // Both limits tripped. Back off to clear the switch contacts.
    _backoff_single(1);
    _backoff_single(2);

    motor_reset_position(1);
    motor_reset_position(2);
    // Position is now a known, trustworthy zero — safe to re-enable the
    // real configured limits.
    motor_set_soft_limits(1, MOTOR1_SOFT_LIMIT_MIN, MOTOR1_SOFT_LIMIT_MAX);
    motor_set_soft_limits(2, MOTOR2_SOFT_LIMIT_MIN, MOTOR2_SOFT_LIMIT_MAX);
    // Auto-tare after every home — the plunger mechanism is now at a
    // known, repeatable reference position (freshly homed), which is
    // exactly the "unloaded, at rest" condition force_sensor_tare()
    // assumes (see its own docs in force_sensor.h). Re-taring here means
    // load cell readings stay correctly zeroed relative to THIS run's
    // actual homed position, not just whatever the boot-time tare
    // happened to capture.
    force_sensor_tare();
    _homed = true;
    _stroke_count = 0;
    ble_log("Homing: complete");
    return true;
#endif
}

bool motors_is_homed() {
    return _homed;
}

// ── Stroke counter ────────────────────────────────────────────────────────────
// A stroke is one complete forward+return syringe cycle.
// The scheduler calls motor_increment_stroke() after each full cycle.

void motor_increment_stroke() {
    _stroke_count++;
}

uint32_t motor_get_stroke_count() {
    return _stroke_count;
}

void motor_reset_stroke_count() {
    _stroke_count = 0;
}
