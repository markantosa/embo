#include "ble_debug.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <stdio.h>
#include <stdarg.h>

// Standard Nordic UART Service UUIDs
#define NUS_SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_TX_UUID       "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"   // notify (ESP32→phone)
#define NUS_RX_UUID       "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"   // write  (phone→ESP32)

static NimBLECharacteristic *_tx_char = nullptr;
static bool _connected = false;

class ServerCB : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *)    override { _connected = true; }
    void onDisconnect(NimBLEServer *s) override {
        _connected = false;
        s->startAdvertising();
    }
};

void ble_debug_init() {
    NimBLEDevice::init("EMBO");
    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCB());

    NimBLEService *svc = server->createService(NUS_SERVICE_UUID);

    _tx_char = svc->createCharacteristic(NUS_TX_UUID, NIMBLE_PROPERTY::NOTIFY);
    svc->createCharacteristic(NUS_RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);

    svc->start();
    NimBLEDevice::startAdvertising();
}

void ble_debug_update() {
    // NimBLE is event-driven; nothing to poll
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
