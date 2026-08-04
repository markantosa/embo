#pragma once

#include <NimBLEServer.h>

// A second BLE service, added to the same NimBLE server/connection as the
// text NUS console (see ble_debug.cpp), specifically to match
// index_logging_to_record_UAS_csv_data.html's fixed binary protocol
// (custom SERVICE_UUID + 5 characteristics, not the Nordic UART Service).
// That dashboard's UUIDs and packet layout are hardcoded client-side, so
// this side has to match them exactly rather than the other way around.
//
// Shares BLE on/off with the text console — both live behind the same
// ble_debug_set_enabled() toggle (Developer Mode > UAS debug mode), since
// they're two services on one advertised device, not two separate radios.
//
// Direct motor jog (MOTOR_CMD) and homing (HOME_CMD) are exposed here the
// same way MOVE/HOME are on the text console, with the same safety guard —
// refused outright while scheduler_is_running(), so this can't hijack the
// motors out from under an active mix, same reasoning as ble_debug.cpp's
// existing bench commands.

void ble_binary_telemetry_init(NimBLEServer *server);
void ble_binary_telemetry_update();  // call every loop() — throttles its own ~40ms notify cadence internally
