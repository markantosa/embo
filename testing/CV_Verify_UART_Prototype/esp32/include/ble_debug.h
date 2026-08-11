#pragma once

// Sets up the NimBLE stack/service/characteristics but does NOT start
// advertising — BLE is off by default. Call ble_debug_set_enabled(true)
// (wired to the UI's Developer Mode > UAS debug mode toggle) to actually
// start advertising as EMBO-Debug.
void ble_debug_init();
void ble_debug_update();    // call every loop()

// Turns BLE advertising on/off at runtime. No-op if already in that state.
// Disabling does not force-disconnect an already-connected client — it
// just stops new connections and re-advertising after that client
// eventually disconnects, matching typical "Bluetooth toggle" behavior.
void ble_debug_set_enabled(bool enabled);
bool ble_debug_is_enabled();

// printf-style wireless log — no-op when no BLE client connected.
void ble_log(const char *fmt, ...);
