#pragma once
#include "ble/ble_service.h"

// Physical bench-test UI — TFT + encoder + buzzer, alongside (not instead
// of) the BLE web dashboard. Two selectable boxes, one per motor:
//
//   SELECT state — rotate the encoder to move the highlight between
//     "Motor 1" / "Motor 2"; press the knob to take control of the
//     highlighted motor.
//   CONTROL state — rotate the encoder to jog that motor (direction
//     follows rotation direction); the buzzer sounds continuously while
//     the motor is moving from encoder input. Press the knob to stop and
//     return to SELECT.
//
// This is a second, independent control path alongside the BLE MOTOR_CMD
// jog — both can drive the same motors, so don't run both at once on the
// bench without expecting them to fight each other.

void ui_init();
void ui_update();  // call every loop()

// Read-only access to main.cpp's telemetry snapshot, for the on-device
// sensor panel — same data the BLE TELEMETRY characteristic sends.
const TelemetryPacket &currentTelemetry();
