#pragma once

void ble_debug_init();
void ble_debug_update();    // call every loop()

// printf-style wireless log — no-op when no BLE client connected.
void ble_log(const char *fmt, ...);
