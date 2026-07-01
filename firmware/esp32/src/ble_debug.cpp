#include "ble_debug.h"
#include "motors.h"
#include "uas.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

// Standard Nordic UART Service UUIDs
#define NUS_SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_TX_UUID       "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"   // notify (ESP32→phone)
#define NUS_RX_UUID       "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"   // write  (phone→ESP32)

#define DEBUG_STEP_HZ     500   // step rate used for MOVE commands

static NimBLECharacteristic *_tx_char = nullptr;
static bool _connected = false;

// ── Streaming state ───────────────────────────────────────────────────────────
static bool     _uas_streaming  = false;
static uint32_t _stream_last_ms = 0;
static const uint32_t STREAM_INTERVAL_MS = 200;

// ── Pending move state ────────────────────────────────────────────────────────
static bool     _move_active      = false;
static uint8_t  _move_motor       = 0;
static uint32_t _move_end_ms      = 0;

// ── Incoming command buffer ───────────────────────────────────────────────────
static char     _cmd_buf[64]  = {};
static bool     _cmd_pending  = false;

// ── BLE callbacks ─────────────────────────────────────────────────────────────

class ServerCB : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *) override {
        _connected = true;
    }
    void onDisconnect(NimBLEServer *s) override {
        _connected = false;
        // Stop any active debug move so motors don't run unattended after disconnect.
        if (_move_active) {
            motor_set_speed(_move_motor, 0);
            motor_enable(_move_motor, false);
            _move_active = false;
        }
        _uas_streaming = false;
        s->startAdvertising();
    }
};

class RxCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c) override {
        std::string val = c->getValue();
        if (val.empty()) return;
        // Copy into command buffer — strip trailing CR/LF.
        size_t len = val.size();
        if (len >= sizeof(_cmd_buf)) len = sizeof(_cmd_buf) - 1;
        memcpy(_cmd_buf, val.data(), len);
        while (len > 0 && (_cmd_buf[len-1] == '\n' || _cmd_buf[len-1] == '\r')) len--;
        _cmd_buf[len] = '\0';
        _cmd_pending = true;
        // Processing happens in ble_debug_update() (main loop context),
        // not here in the BLE task, so motor/ADC calls are safe.
    }
};

// ── Command processing ────────────────────────────────────────────────────────

static void _handle_command(const char *cmd) {
    // HOME
    if (strcmp(cmd, "HOME") == 0) {
        ble_log("CMD: homing...");
        bool ok = motors_home();
        ble_log(ok ? "HOME: done" : "HOME: FAILED — check limit switches");
        return;
    }

    // MOVE <motor 1|2> <steps>   (negative steps = reverse)
    int motor, steps;
    if (sscanf(cmd, "MOVE %d %d", &motor, &steps) == 2
            && (motor == 1 || motor == 2) && steps != 0) {
        if (_move_active) {
            motor_set_speed(_move_motor, 0);
            motor_enable(_move_motor, false);
        }
        bool fwd = (steps > 0);
        uint32_t abs_steps = (uint32_t)(steps < 0 ? -steps : steps);
        uint32_t duration_ms = (abs_steps * 1000UL) / DEBUG_STEP_HZ;
        motor_set_dir(motor, fwd);
        motor_enable(motor, true);
        motor_clear_limit(motor);
        motor_set_speed(motor, DEBUG_STEP_HZ);
        _move_motor  = motor;
        _move_end_ms = millis() + duration_ms;
        _move_active = true;
        ble_log("MOVE: M%d %+d steps (~%lu ms)", motor, steps, duration_ms);
        return;
    }

    // UAS ON / UAS OFF
    if (strcmp(cmd, "UAS ON") == 0) {
        _uas_streaming = true;
        ble_log("UAS: streaming ON");
        return;
    }
    if (strcmp(cmd, "UAS OFF") == 0) {
        _uas_streaming = false;
        ble_log("UAS: streaming OFF");
        return;
    }

    ble_log("CMD unknown: \"%s\"", cmd);
    ble_log("Commands: HOME | MOVE <1|2> <steps> | UAS ON | UAS OFF");
}

// ── Public API ────────────────────────────────────────────────────────────────

void ble_debug_init() {
    NimBLEDevice::init("EMBO-Debug");
    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCB());

    NimBLEService *svc = server->createService(NUS_SERVICE_UUID);

    _tx_char = svc->createCharacteristic(NUS_TX_UUID,
                   NIMBLE_PROPERTY::NOTIFY);

    NimBLECharacteristic *rx_char = svc->createCharacteristic(NUS_RX_UUID,
                   NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    rx_char->setCallbacks(new RxCB());

    svc->start();
    NimBLEDevice::startAdvertising();
}

void ble_debug_update() {
    // Process any pending command from the BLE RX callback.
    if (_cmd_pending) {
        _cmd_pending = false;
        _handle_command(_cmd_buf);
    }

    // Finish a timed MOVE when duration elapses or limit trips.
    if (_move_active) {
        if (millis() >= _move_end_ms || motor_limit_hit(_move_motor)) {
            motor_set_speed(_move_motor, 0);
            motor_enable(_move_motor, false);
            _move_active = false;
            ble_log("MOVE: done (M%d)", _move_motor);
        }
    }

    // Periodic streaming.
    uint32_t now = millis();
    if (now - _stream_last_ms < STREAM_INTERVAL_MS) return;
    _stream_last_ms = now;

    if (_uas_streaming) {
        ble_log("UAS: %lu mV", uas_read_mv());
    }

    if (_move_active) {
        ble_log("SG M%d: %u", _move_motor, motor_sg_result(_move_motor));
    }
}

void ble_log(const char *fmt, ...) {
    if (!_connected || !_tx_char) return;
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    _tx_char->setValue((uint8_t *)buf, strlen(buf));
    _tx_char->notify();
}
