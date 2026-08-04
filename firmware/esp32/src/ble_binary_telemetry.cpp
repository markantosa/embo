#include "ble_binary_telemetry.h"
#include "scheduler.h"
#include "motors.h"
#include "uas.h"
#include "turbidity.h"
#include "force_sensor.h"
#include "ble_debug.h"
#include <NimBLEDevice.h>
#include <Arduino.h>
#include <string.h>

#define BIN_SERVICE_UUID   "8f6a2001-3b1a-4e3d-9f2e-6a2c6c9a9a10"
#define BIN_TELEMETRY_UUID "8f6a2002-3b1a-4e3d-9f2e-6a2c6c9a9a10"
#define BIN_MOTOR_CMD_UUID "8f6a2003-3b1a-4e3d-9f2e-6a2c6c9a9a10"
#define BIN_HOME_CMD_UUID  "8f6a2004-3b1a-4e3d-9f2e-6a2c6c9a9a10"
#define BIN_LOG_UUID       "8f6a2005-3b1a-4e3d-9f2e-6a2c6c9a9a10"
#define BIN_FREQ_CMD_UUID  "8f6a2006-3b1a-4e3d-9f2e-6a2c6c9a9a10"

#define TELEMETRY_INTERVAL_MS 40  // matches the dashboard's own comment ("telemetry updates every ~40ms")

static NimBLECharacteristic *_telemetryChar = nullptr;
static NimBLECharacteristic *_logChar       = nullptr;
static uint32_t _lastTelemetryMs = 0;

// Exact byte layout the dashboard's onTelemetry() expects — little-endian,
// packed, 41 bytes. Do not reorder/resize fields without updating the JS
// side to match; this has to mirror index_logging_to_record_UAS_csv_data.html
// exactly, not the other way around.
#pragma pack(push, 1)
struct TelemetryPacket {
    float    uasVolts;
    float    uasFreqHz;
    uint16_t sgResultM1;
    uint16_t sgResultM2;
    int32_t  positionM1;   // always 0 — this firmware drives timed strokes,
    int32_t  positionM2;   // not step-counted absolute position, see motors.h
    uint8_t  homingFlags;  // bit0/1: homing-in-progress M1/M2 (always 0 here —
                            //   motors_home() is blocking, so nothing can poll
                            //   telemetry while it's running anyway);
                            //   bit2/3: homed M1/M2 (this firmware only tracks
                            //   one combined homed flag, so both bits mirror
                            //   motors_is_homed() together)
    uint8_t  limitFlags;   // bit0: M1 limit active, bit1: M2 limit active
    int32_t  forceRaw1;
    int32_t  forceRaw2;
    uint16_t alsClear;
    uint32_t irRaw;
    uint32_t redRaw;
    uint8_t  turbidityFlags; // bit0: APDS9960 ok, bit1: MAX30102 ok
};
#pragma pack(pop)
static_assert(sizeof(TelemetryPacket) == 41, "TelemetryPacket layout must match the dashboard's parser exactly");

static void _sendLogLine(const char *line) {
    if (!_logChar) return;
    _logChar->setValue((const uint8_t *)line, strlen(line));
    _logChar->notify();
}

class MotorCmdCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c) override {
        std::string v = c->getValue();
        if (v.size() != 3) return;
        uint8_t motorId = (uint8_t)v[0];
        int8_t  dir     = (int8_t)v[1];
        uint8_t speedPct = (uint8_t)v[2];

        if (scheduler_is_running()) {
            _sendLogLine("MOTOR_CMD: refused - a run is in progress");
            return;
        }
        if (motorId != 1 && motorId != 2) return;
        if (speedPct > 100) speedPct = 100;

        // speedPct is a percentage of the same bench jog rate MOVE uses over
        // BLE text — see ble_debug.cpp's DEBUG_STEP_HZ.
        uint32_t hz = (uint32_t)((uint32_t)500 * speedPct / 100);
        motor_set_dir(motorId, dir >= 0);
        motor_enable(motorId, hz > 0);
        motor_clear_limit(motorId);
        motor_set_speed(motorId, hz);
    }
};

class HomeCmdCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c) override {
        std::string v = c->getValue();
        if (v.size() != 1) return;
        if (scheduler_is_running()) {
            _sendLogLine("HOME_CMD: refused - a run is in progress");
            return;
        }
        // v[0] (which motor) isn't meaningful here — motors_home() always
        // homes both together, unlike the finer-grained per-motor jog above.
        bool ok = motors_home();
        _sendLogLine(ok ? "HOME_CMD: done" : "HOME_CMD: FAILED - check limit switches");
    }
};

class FreqCmdCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c) override {
        std::string v = c->getValue();
        if (v.size() != 4) return;
        float hz;
        memcpy(&hz, v.data(), 4);
        if (scheduler_is_running()) {
            _sendLogLine("FREQ_CMD: refused - a run is in progress");
            return;
        }
        if (hz <= 0.0f) return;
        uas_set_manual_override_hz(hz);
    }
};

void ble_binary_telemetry_init(NimBLEServer *server) {
    NimBLEService *svc = server->createService(BIN_SERVICE_UUID);

    _telemetryChar = svc->createCharacteristic(BIN_TELEMETRY_UUID, NIMBLE_PROPERTY::NOTIFY);
    _logChar       = svc->createCharacteristic(BIN_LOG_UUID, NIMBLE_PROPERTY::NOTIFY);

    NimBLECharacteristic *motorCmdChar = svc->createCharacteristic(
        BIN_MOTOR_CMD_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    motorCmdChar->setCallbacks(new MotorCmdCB());

    NimBLECharacteristic *homeCmdChar = svc->createCharacteristic(
        BIN_HOME_CMD_UUID, NIMBLE_PROPERTY::WRITE);
    homeCmdChar->setCallbacks(new HomeCmdCB());

    NimBLECharacteristic *freqCmdChar = svc->createCharacteristic(
        BIN_FREQ_CMD_UUID, NIMBLE_PROPERTY::WRITE);
    freqCmdChar->setCallbacks(new FreqCmdCB());

    svc->start();

    // Also advertise this service's UUID so the dashboard's
    // requestDevice({filters: [{services: [SERVICE_UUID]}]}) can find it —
    // same reasoning as the NUS service UUID in ble_debug.cpp.
    NimBLEDevice::getAdvertising()->addServiceUUID(BIN_SERVICE_UUID);
}

void ble_binary_telemetry_update() {
    if (!_telemetryChar) return;  // not yet initialized

    uint32_t now = millis();
    if (now - _lastTelemetryMs < TELEMETRY_INTERVAL_MS) return;
    _lastTelemetryMs = now;

    TelemetryPacket pkt{};
    pkt.uasVolts    = uas_read_mv() / 1000.0f;
    pkt.uasFreqHz   = uas_get_current_frequency_hz();
    pkt.sgResultM1  = motor_sg_result(1);
    pkt.sgResultM2  = motor_sg_result(2);
    pkt.positionM1  = 0;
    pkt.positionM2  = 0;
    pkt.homingFlags = motors_is_homed() ? 0x0C : 0x00;  // bits 2+3, see struct comment
    pkt.limitFlags  = (motor_limit_hit(1) ? 0x01 : 0x00) | (motor_limit_hit(2) ? 0x02 : 0x00);
    pkt.forceRaw1   = force_sensor_get_raw_1();
    pkt.forceRaw2   = force_sensor_get_raw_2();
    pkt.alsClear    = turbidity_get_als_clear();
    pkt.irRaw       = turbidity_get_ir_raw();
    pkt.redRaw      = turbidity_get_red_raw();
    pkt.turbidityFlags = (turbidity_apds_ok() ? 0x01 : 0x00) | (turbidity_max_ok() ? 0x02 : 0x00);

    _telemetryChar->setValue((const uint8_t *)&pkt, sizeof(pkt));
    _telemetryChar->notify();
}
