#pragma once

// Pin map — EMBO main board v2.51, ESP32-S3-WROOM-1-N4 (soldered)
// Source: docs/EMBO_PCB_Design_Brief_v2.51.txt section 4 and 6.

// UAS envelope ADC
constexpr int PIN_UAS_ADC   = 1;   // ADC1_CH0, envelope output

// Shared TMC2209 UART (single wire, both drivers on same bus, different addr)
constexpr int PIN_TMC_UART  = 4;

// Motor 1 (U5, TMC UART addr 0)
constexpr int PIN_STEP_M1   = 5;
constexpr int PIN_DIR_M1    = 6;
constexpr int PIN_EN_M1     = 7;

// Motor 2 (U6, TMC UART addr 1)
constexpr int PIN_STEP_M2   = 8;
constexpr int PIN_DIR_M2    = 9;
constexpr int PIN_EN_M2     = 10;

// Limit switches
constexpr int PIN_LIMIT_M1  = 14;
constexpr int PIN_LIMIT_M2  = 15;

// Status LED (onboard)
constexpr int PIN_STATUS_LED = 2;

// TMC2209 UART addresses (set via MS1/MS2 straps on board, see §6.2)
constexpr uint8_t TMC_ADDR_M1 = 0;
constexpr uint8_t TMC_ADDR_M2 = 1;

// Sense resistor on BTT TMC2209 plug-in modules (§6.1)
constexpr float TMC_R_SENSE = 0.11f;

// Run current for bench testing — conservative default, tune per motor/lead screw.
constexpr uint16_t TMC_RUN_CURRENT_MA = 600;
constexpr uint16_t TMC_HOLD_CURRENT_MA = 300;

// Homing behavior
constexpr uint32_t HOMING_SPEED_SPS   = 400;   // steps/sec while seeking the switch
constexpr uint32_t HOMING_BACKOFF_STEPS = 100; // back off after trigger

// Jog / slider speed range (steps/sec), mapped from the 0-100 BLE speed value
constexpr uint32_t JOG_MIN_SPS = 100;
constexpr uint32_t JOG_MAX_SPS = 2000;

// Sensor polling
constexpr uint32_t UAS_SAMPLE_PERIOD_MS = 40;   // ~25 Hz
constexpr uint32_t SG_SAMPLE_PERIOD_MS  = 50;   // ~20 Hz
constexpr uint32_t UAS_OVERSAMPLE_COUNT = 8;

// Motor identifiers used in BLE commands
enum MotorId : uint8_t {
	MOTOR_1 = 0,
	MOTOR_2 = 1,
	MOTOR_BOTH = 2,
};
