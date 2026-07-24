#pragma once
#include <Arduino.h>
#include "config.h"

// GATT layout (all UUIDs 128-bit custom, see ble_service.cpp):
//
//   Service: EMBO PCB Test v3.4
//     Characteristic TELEMETRY (notify): packed TelemetryPacket, ~25Hz —
//       all v3.4 sensors: UAS envelope, 2x load cell, turbidity pair,
//       StallGuard x2, limit switches, motor position/homing.
//     Characteristic MOTOR_CMD (write):  MotorCmd  {motorId, dir(-1/0/1), speedPct}
//     Characteristic HOME_CMD  (write):  1 byte motorId (0=M1, 1=M2, 2=both)
//     Characteristic LOG       (notify): UTF-8 text log line, for bring-up debugging
//
// This is a bench-test protocol, not the production BLE debug link.

#pragma pack(push, 1)
struct TelemetryPacket {
	float uasVolts;
	uint16_t sgResultM1;
	uint16_t sgResultM2;
	int32_t positionM1;
	int32_t positionM2;
	uint8_t homingFlags;  // bit0: M1 homing, bit1: M2 homing, bit2: M1 homed, bit3: M2 homed
	uint8_t limitFlags;   // bit0: limit M1 triggered, bit1: limit M2 triggered

	// Load cells (§10) — raw 24-bit signed HX711 counts, channel A / gain 128.
	int32_t forceRaw1;
	int32_t forceRaw2;

	// Turbidity pair (§9) — APDS9960 ALS clear channel + MAX30102 backscatter.
	uint16_t alsClear;
	uint32_t irRaw;
	uint32_t redRaw;
	uint8_t turbidityFlags; // bit0: APDS9960 present/ok, bit1: MAX30102 present/ok
};

struct MotorCmd {
	uint8_t motorId; // MotorId enum
	int8_t dir;      // -1, 0, +1
	uint8_t speedPct;
};
#pragma pack(pop)

void bleServiceInit();
void bleNotifyTelemetry(const TelemetryPacket &pkt);

// True once a central (the web dashboard) is connected. Used by main.cpp to
// blink the status LED while waiting for a connection and hold it solid once
// one is made.
bool bleIsConnected();

// Formats like printf, sends to Serial AND (if a client is subscribed) as a
// BLE notification on the LOG characteristic — lets the web dashboard show
// firmware debug output without a serial monitor.
void bleLog(const char *fmt, ...);

// Set by the BLE write callbacks, drained from loop() so motor/homing
// logic never runs inside the NimBLE callback/task context.
bool blePopMotorCmd(MotorCmd &out);
bool blePopHomeCmd(uint8_t &motorIdOut);
