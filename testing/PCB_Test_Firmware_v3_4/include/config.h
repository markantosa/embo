#pragma once

// Pin map — EMBO main board v3.4, ESP32-S3-WROOM-1-N4.
// Source: docs/EMBO_PCB_Design_Brief_v3_4.txt.
// Unchanged from earlier board revisions for STEP/DIR/EN/limits/UAS ADC —
// v3.4's changes (220R GPIO protection resistors, MS1/MS2 jumpers, PDN
// pin4/5 correction, F2 backstop fuse) are all analog/passive-side hardware
// hardening and don't move any GPIO assignment in this table.

// UAS envelope ADC
constexpr int PIN_UAS_ADC   = 1;   // ADC1_CH0, envelope output

// Shared TMC2209 UART bus, both drivers on one PDN bus (§7.3).
constexpr int PIN_TMC_UART_TX = 4;
constexpr int PIN_TMC_UART_RX = 44;

// Motor 1 (U5, TMC UART addr set via MS1/MS2 jumpers, §7.2)
constexpr int PIN_STEP_M1   = 5;
constexpr int PIN_DIR_M1    = 6;
constexpr int PIN_EN_M1     = 7;

// Motor 2 (U6)
constexpr int PIN_STEP_M2   = 8;
constexpr int PIN_DIR_M2    = 9;
constexpr int PIN_EN_M2     = 10;

// Limit switches
constexpr int PIN_LIMIT_M1  = 14;
constexpr int PIN_LIMIT_M2  = 15;

// Status LED (onboard)
constexpr int PIN_STATUS_LED = 2;

// I2C turbidity bus (§9, §6.6): APDS9960 0x39 (ALS), MAX30102 0x57 (backscatter)
constexpr int PIN_I2C_SDA = 3;
constexpr int PIN_I2C_SCL = 43;
constexpr uint32_t I2C_CLOCK_HZ = 400000;
constexpr uint8_t APDS9960_ADDR = 0x39;
constexpr uint8_t MAX30102_ADDR = 0x57;

// HX711 load cells, shared clock (§10.2)
constexpr int PIN_HX711_SCK   = 37; // shared between both modules
constexpr int PIN_HX711_1_DT  = 42;
constexpr int PIN_HX711_2_DT  = 21;

// TMC2209 UART addresses (set via MS1/MS2 straps on board, §7.2)
constexpr uint8_t TMC_ADDR_M1 = 0;
constexpr uint8_t TMC_ADDR_M2 = 1;

// Sense resistor on MKS V2.0 TMC2209 plug-in modules (§7.1): 110mOhm class,
// which caps full-scale current at ~1.77A RMS / 2.5A peak — see brief.
constexpr float TMC_R_SENSE = 0.11f;

// Run current for bench testing — conservative default, tune per motor/lead screw.
constexpr uint16_t TMC_RUN_CURRENT_MA = 600;
constexpr uint16_t TMC_HOLD_CURRENT_MA = 300;

// Homing behavior
constexpr uint32_t HOMING_SPEED_SPS   = 400;   // steps/sec while seeking the switch
constexpr uint32_t HOMING_BACKOFF_STEPS = 100; // back off after trigger

// Jog / slider speed range (steps/sec), mapped from the 0-100 BLE speed value
constexpr uint32_t JOG_MIN_SPS = 1800;
constexpr uint32_t JOG_MAX_SPS = 36000;

// Ramp rate (steps/sec, per second) used to glide from JOG_MIN_SPS up to the
// commanded target instead of snapping straight there.
constexpr uint32_t JOG_ACCEL_SPS2 = 20000;

// Sensor polling
constexpr uint32_t UAS_SAMPLE_PERIOD_MS   = 40;   // ~25 Hz
constexpr uint32_t SG_SAMPLE_PERIOD_MS    = 50;   // ~20 Hz
constexpr uint32_t UAS_OVERSAMPLE_COUNT   = 8;
constexpr uint32_t FORCE_SAMPLE_PERIOD_MS = 13;   // matches HX711 80SPS (§10.1)
constexpr uint32_t TURBIDITY_SAMPLE_PERIOD_MS = 20; // 50Hz, within the 50-100Hz budget (§11.2)

// Motor identifiers used in BLE commands
enum MotorId : uint8_t {
	MOTOR_1 = 0,
	MOTOR_2 = 1,
	MOTOR_BOTH = 2,
};
