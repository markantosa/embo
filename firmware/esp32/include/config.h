#pragma once

#include <cstdint>

// ── Firmware version ─────────────────────────────────────────────────────────
#define FIRMWARE_VERSION "0.6.2"

// ── GPIO assignments (EMBO v3.4) ─────────────────────────────────────────────
// Source: docs/EMBO_PCB_Design_Brief_v3_4.txt, docs/EMBO_Pinout_Cheatsheet.txt
// CHANGED FROM v2.4: GPIO21/37/42 used to be touch pins — they are now HX711
// load-cell pins (added v3.2). Touch moved to GPIO12/46 in v3.3. TMC UART is
// now two separate pins (GPIO4 TX / GPIO44 RX), not one bidirectional pin.

// UAS / ADC
#define PIN_UAS_ADC         1   // ADC1_CH0 — envelope detector output

// Status LED
#define PIN_STATUS_LED      2

// I2C turbidity bus — APDS9960 (0x39, ALS) + MAX30102 (0x57, backscatter)
#define PIN_I2C_SDA         3
#define PIN_I2C_SCL         43
#define I2C_CLOCK_HZ        400000
#define APDS9960_ADDR       0x39
#define MAX30102_ADDR       0x57
// A transient I2C glitch shouldn't permanently drop a channel out of
// fusion for the rest of a run — retry re-init at this cadence while a
// device is marked not-ok. See turbidity.cpp.
#define TURBIDITY_REINIT_INTERVAL_MS  2000

// Motor driver — shared PDN_UART bus, genuinely separate TX/RX pins
#define PIN_TMC_UART_TX     4
#define PIN_TMC_UART_RX     44

// Motor 1 (TMC2209 U5/SK1, UART addr set via MS1/MS2 jumpers)
#define PIN_STEP_M1         5
#define PIN_DIR_M1          6
#define PIN_EN_M1           7

// Motor 2 (TMC2209 U6/SK2)
#define PIN_STEP_M2         8
#define PIN_DIR_M2          9
#define PIN_EN_M2           10

// UI buttons (via IDC ribbon)
#define PIN_BTN1            11  // dedicated stop/e-stop button — see ui.h
// GPIO12 = T_CS (touch), not a button as of v3.3 — see below
#define PIN_BUZ_PWM         13  // LEDC PWM → buzzer

// Limit switches
#define PIN_LIMIT_M1        14
#define PIN_LIMIT_M2        15

// EC11 rotary encoder (via IDC ribbon)
#define PIN_EC11_A          16
#define PIN_EC11_B          17
#define PIN_EC11_SW         18

// USB (fixed PHY — do not reassign)
// GPIO19 = USB D−, GPIO20 = USB D+

// Load cells — HX711 x2, shared clock (added v3.2)
#define PIN_HX711_2_DT      21  // CHANGED v3.2: was TOUCH_IRQ pre-v3.2
#define PIN_HX711_SCK       37  // CHANGED v3.2: was SPI_MISO pre-v3.2, shared between both modules
#define PIN_HX711_1_DT      42  // CHANGED v3.2: was TOUCH_CS pre-v3.2

// SPI bus (shared: ILI9341, XPT2046 touch, AD9833) — no MISO on this bus as
// of v3.2 (touch MISO/T_DO moved to GPIO46, see below); AD9833 has no MISO.
#define PIN_SPI_MOSI        35
#define PIN_SPI_CLK         36

// SPI chip selects
#define PIN_AD9833_CS       38  // AD9833 FSYNC — SPI Mode 2
#define PIN_TFT_CS          39  // ILI9341 CS   — SPI Mode 0
#define PIN_TFT_DC          40
#define PIN_TFT_RST         41

// Touch (XPT2046), restored v3.3 on the two remaining strapping pins with
// headroom — see design brief §8.3 for the rationale. T_IRQ is NOT wired on
// the main board; touch is polled instead. A touch integration attempt was
// made and reverted after it caused a boot hang — see LGFX_Config.h's
// TOUCH_TODO comment for what to restore once that's debugged on real
// hardware. Currently unused.
#define PIN_TOUCH_CS        12  // T_CS
#define PIN_TOUCH_DO        46  // T_DO (MISO)

// RPi UART (UART1)
#define PIN_RPI_TX          47
#define PIN_RPI_RX          48

// ── SPI clock speeds ─────────────────────────────────────────────────────────
#define SPI_CLK_TFT         20000000   // 20 MHz max via IDC ribbon
#define SPI_CLK_TOUCH        2000000   // 2 MHz (XPT2046 max) — currently unused, see PIN_TOUCH_CS above
#define SPI_CLK_AD9833      10000000   // ~10 MHz — short direct trace

// ── UART speeds ───────────────────────────────────────────────────────────────
#define BAUD_RPI            921600
#define BAUD_TMC            115200

// ── TMC2209 UART addresses ────────────────────────────────────────────────────
// Field-jumpered as of v3.4 (JMP_MS1_Tn/JMP_MS2_Tn) — these values are the
// jumper positions currently populated, not hardwired straps.
#define TMC_ADDR_M1         0   // MS1=GND, MS2=GND
#define TMC_ADDR_M2         1   // MS1=3.3V via 10k, MS2=GND

// Ported from a confirmed-working bench firmware (PCB_Test_Firmware_v3_4) —
// these are real calibrated values, not the raw IHOLD/IRUN register
// guesses this used to use.
#define TMC_R_SENSE           0.11f
#define TMC_RUN_CURRENT_MA    600
#define TMC_HOLD_CURRENT_MA   300

// Soft motion limits — fixed firmware config, not runtime-settable. Steps
// from the last home/reset position; INT32_MIN/INT32_MAX means "no limit
// on that side" (the default — fill in real values once the mechanism's
// safe travel range is known). Enforced directly in the step-generation
// ISR (motors.cpp) for every motor movement path, not just one.
#define MOTOR1_SOFT_LIMIT_MIN   0
#define MOTOR1_SOFT_LIMIT_MAX   16000
#define MOTOR2_SOFT_LIMIT_MIN   0
#define MOTOR2_SOFT_LIMIT_MAX   16000

// Settings > Motion > Test Both Motors. One "stroke" for this bench test =
// left motor 0→max then (concurrently) left back to 0 while right goes
// 0→max — a test-specific concept, deliberately NOT the same counter as
// motor_increment_stroke()/MIXING_MAX_STROKES_SAFETY_CAP, which belong to
// the real mixing scheduler; this test never touches that counter.
#define STROKE_TEST_COUNT_MIN       1
#define STROKE_TEST_COUNT_MAX       50
#define STROKE_TEST_COUNT_DEFAULT   5
// Double MOTOR_JOG_HZ for this test specifically — doesn't change jog
// speed anywhere else (BLE MOVE, the Jog Motor screens still use
// MOTOR_JOG_HZ directly).
#define MOTOR_STROKE_TEST_HZ        (MOTOR_JOG_HZ * 2)
// SG_RESULT (motor_sg_result(), motors.h) is 0-1023, HIGHER = LESS load —
// a value below this is treated as a stall (either TMC torque loss or a
// genuine mechanical jam) and stops the motor(s) immediately with a
// specific diagnosis, instead of waiting for a generic timeout. Shared
// between Stroke Testing and the real mixing scheduler (scheduler.cpp) —
// same physical mechanism, same failure mode, one threshold. PLACEHOLDER —
// SG_RESULT's baseline depends on current/speed/microstepping and needs
// measuring on the actual bench (watch it during a few known-good runs,
// then set this comfortably below the lowest normal value seen) before
// this is trustworthy; not a calibrated number.
// TEMPORARILY DISABLED (0 = never trips, motor_sg_result() is unsigned so
// it can never read below 0) — restore a real threshold once tuned.
#define MOTOR_STALL_SG_THRESHOLD  0

// ── ADC ──────────────────────────────────────────────────────────────────────
// GPIO1 = ADC1_CH0. Use ADC_ATTEN_DB_12 (0–3.1V range, ~0.757 mV/LSB).
// Wait ≥500µs after each AD9833 frequency step before reading.
#define UAS_ADC_CHANNEL     ADC1_CHANNEL_0
#define UAS_SETTLE_US       500

// ── UAS multi-frequency sweep ───────────────────────────────────────────────
// Per docs/EMBO_UAS_CV_Technical_Advisory.txt (July 2026): a single 1MHz
// attenuation reading is not reliably invertible to particle size for a
// polydisperse population. The AD9833 is frequency-agile, so instead of one
// fixed tone, uas_update() sweeps this small set of discrete frequencies each
// cycle — a firmware/timing-only change, no board respin required.
//
// PLACEHOLDER SPACING: +/-10% around the nominal 1MHz transducer frequency.
// See firmware/CALIBRATION.md §2 before trusting the off-center points.
#define UAS_NUM_FREQUENCIES  1
#define UAS_FREQ_HZ_0        960000.0f   // 0.96 MHz — single-frequency production setting

// ── Homing ───────────────────────────────────────────────────────────────────
// Slow approach speed — gentle enough to avoid impact damage on limit trip.
#define HOMING_STEP_HZ       1000
// Steps to back off from the limit switch after trip (8 microsteps per full step).
// 200 steps @ 8µstep = 25 full steps. Tune to suit the lead screw pitch.
#define HOMING_BACKOFF_STEPS 200
// Brief clockwise nudge before the real anticlockwise homing approach —
// relieves any mechanical preload/backlash on the lead screw and gives a
// consistent starting condition regardless of where the motor happened to
// be sitting at boot (position is unknown pre-home). Both motors nudge
// CONCURRENTLY, same as the real approach.
#define HOMING_PRE_NUDGE_STEPS 500
// Abort homing if limit not reached within this time.
#define HOMING_TIMEOUT_MS    30000
// Direction toward the limit switch, intended to be anticlockwise —
// false = reverse (DIR pin LOW). Direction sense depends entirely on
// motor wiring/mounting, not anything in software, so this can't be
// verified from code alone: watch the actual rotation on the bench during
// a homing attempt, and if it's turning clockwise instead, flip this to
// `true` (both _home_single()'s approach and _backoff_single()'s reverse
// leg use this same constant, so flipping it here is the only change
// needed — do not hardcode a direction anywhere else).
#define HOMING_FORWARD       false

// ── Particle size target ─────────────────────────────────────────────────────
// Encoder-adjustable setpoint range (clinical range TBD beyond this bound).
#define TARGET_SIZE_UM_MIN      50
#define TARGET_SIZE_UM_MAX      1000
#define TARGET_SIZE_UM_DEFAULT  300
#define TARGET_SIZE_UM_STEP     5    // per encoder detent
// "In spec" window half-width around the setpoint. Placeholder — see
// firmware/CALIBRATION.md §8 before trusting for auto-stop.
#define TARGET_TOLERANCE_UM     25
// How long the UAS-voltage size estimate must read continuously within
// TARGET_TOLERANCE_UM before the mixing loop actually stops (scheduler.cpp)
// — the check itself runs every scheduler_update() call ("always", per
// product decision), but a single instantaneous in-tolerance reading isn't
// trusted to stop an irreversible process; this is the debounce. PLACEHOLDER
// — not measured against real noise characteristics of the UAS voltage
// signal yet, pick a value and tune from there once you have real
// in-tolerance sensor traces.
#define UAS_SIZE_IN_SPEC_HOLD_MS   1000

// ── UI input timing (used by ui.cpp) ────────────────────────────────────────
// BTN1 is dedicated to stop/e-stop only (no start function) — the encoder's
// own push-switch (EC11_SW) handles confirm/start instead. One safety-
// critical button with one job, per project decision.
#define BTN1_DEBOUNCE_MS      30
#define BTN1_LONGPRESS_MS     800   // held this long or more = emergency stop
#define EC11_SW_DEBOUNCE_MS   30
// EC11_SW short press = confirm/start (or return-to-menu on the done screen).
// Held this long or more = request an optional camera size verification
// instead (see rpi_uart.h / ui.cpp) — CV is opt-in per-check now, not a
// continuous control input. See CALIBRATION.md and rpi_uart.h for why.
#define EC11_SW_LONGPRESS_MS  800

// ── RPi camera verification (on-demand, single-shot) ────────────────────────
// CV is NOT a continuous control input as of this revision — the RPi only
// captures + runs CV when explicitly asked (rpi_request_capture()), and the
// result is for display/logging only. See rpi_uart.h and scheduler.h.
#define RPI_CAPTURE_TIMEOUT_MS  8000   // camera + YOLO inference time budget

// ── Mixing stroke motion (used by scheduler.cpp) ────────────────────────────
// One "stroke" = one forward + return cycle at this speed/duration. These
// are motion-profile constants (how a stroke is physically executed), as
// opposed to calibration.h's sensor/model constants (how strokes map to
// particle size) — kept separate on purpose.
#define STROKE_RUN_HZ        4000   // step rate during a stroke
#define MOTOR_JOG_HZ         2000    // step rate for manual jog moves — BLE MOVE and Settings > Motion > Jog

// ── Sensor-to-physical-unit and control-loop calibration ────────────────────
// Every value that needs measuring on real hardware (HX711 tare/scale,
// e-stop force threshold, breakage kinetics k/D_min, scheduler batch sizing)
// lives in calibration.h, NOT here — see that file's header comment and
// firmware/CALIBRATION.md for the full calibration procedure.
