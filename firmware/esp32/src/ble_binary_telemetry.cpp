#include "ble_binary_telemetry.h"
#include "scheduler.h"
#include "motors.h"
#include "uas.h"
#include "turbidity.h"
#include "force_sensor.h"
#include "ble_debug.h"
#include "config.h"
#include "data_logger.h"
#include <NimBLEDevice.h>
#include <Arduino.h>
#include <string.h>

#define BIN_SERVICE_UUID   "8f6a2001-3b1a-4e3d-9f2e-6a2c6c9a9a10"
#define BIN_TELEMETRY_UUID "8f6a2002-3b1a-4e3d-9f2e-6a2c6c9a9a10"
#define BIN_MOTOR_CMD_UUID "8f6a2003-3b1a-4e3d-9f2e-6a2c6c9a9a10"
#define BIN_HOME_CMD_UUID  "8f6a2004-3b1a-4e3d-9f2e-6a2c6c9a9a10"
#define BIN_LOG_UUID       "8f6a2005-3b1a-4e3d-9f2e-6a2c6c9a9a10"
#define BIN_FREQ_CMD_UUID  "8f6a2006-3b1a-4e3d-9f2e-6a2c6c9a9a10"
#define BIN_LOG_CTRL_UUID  "8f6a2008-3b1a-4e3d-9f2e-6a2c6c9a9a10"
#define BIN_LOG_DATA_UUID  "8f6a2009-3b1a-4e3d-9f2e-6a2c6c9a9a10"

#define TELEMETRY_INTERVAL_MS 40  // matches the dashboard's own comment ("telemetry updates every ~40ms")

static NimBLECharacteristic *_telemetryChar = nullptr;
static NimBLECharacteristic *_logChar       = nullptr;
static NimBLECharacteristic *_logDataChar   = nullptr;
static uint32_t _lastTelemetryMs = 0;

// Chunked file dump state — serviced from ble_binary_telemetry_update(),
// one chunk every DUMP_CHUNK_INTERVAL_MS, not blasted out all at once.
// 100-byte payload chosen conservatively to stay well within commonly
// negotiated BLE MTUs (NimBLE-Arduino typically negotiates up into the
// 180+ range, but this doesn't rely on that). Each chunk is prefixed with
// seq/total/len so the dashboard can detect a dropped chunk instead of
// silently reassembling a corrupt file — the whole point of this feature
// is to be MORE trustworthy than the live stream it's replacing.
#define LOG_CHUNK_PAYLOAD 100
#define DUMP_CHUNK_INTERVAL_MS 20
static bool     _dumpInProgress = false;
static uint16_t _dumpSeq = 0;
static uint16_t _dumpTotalChunks = 0;
static uint32_t _lastDumpChunkMs = 0;

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
    int32_t  positionM1;   // exact, ISR-counted step position — see motors.h
    int32_t  positionM2;
    uint8_t  homingFlags;  // bit0/1: homing-in-progress M1/M2 (always 0 here —
                            //   motors_home() is blocking, so nothing can poll
                            //   telemetry while it's running anyway);
                            //   bit2/3: homed M1/M2 (this firmware only tracks
                            //   one combined homed flag, so both bits mirror
                            //   motors_is_homed() together)
    uint8_t  limitFlags;   // bit0/1: M1/M2 hardware limit switch active
                            //   bit2/3: M1/M2 AT a configured soft position
                            //   limit — fixed firmware config, see
                            //   MOTOR1/2_SOFT_LIMIT_MIN/MAX in config.h
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

        // speedPct is a percentage of MOTOR_JOG_HZ — the same jog speed
        // used everywhere else a motor is bench-driven (BLE MOVE, the UI
        // Jog Motor screens), so changing one constant in config.h changes
        // all of them together.
        uint32_t hz = (uint32_t)((uint32_t)MOTOR_JOG_HZ * speedPct / 100);
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

static void _startDump() {
    if (_dumpInProgress) {
        _sendLogLine("LOG_CTRL: dump already in progress");
        return;
    }
    if (data_logger_is_running()) {
        _sendLogLine("LOG_CTRL: stop the capture before dumping it");
        return;
    }
    if (!data_logger_open_for_read()) {
        _sendLogLine("LOG_CTRL: no data to dump (start a capture first)");
        return;
    }
    size_t total = data_logger_file_size();
    _dumpTotalChunks = (uint16_t)((total + LOG_CHUNK_PAYLOAD - 1) / LOG_CHUNK_PAYLOAD);
    _dumpSeq = 0;
    _dumpInProgress = true;
    _lastDumpChunkMs = 0;  // send the first chunk on the very next update()
    char msg[48];
    snprintf(msg, sizeof(msg), "LOG_CTRL: dump starting (%u bytes, %u chunks)",
             (unsigned)total, _dumpTotalChunks);
    _sendLogLine(msg);
}

// LOG_CTRL payload: single byte. 0=stop capture, 1=start capture,
// 2=clear stored data, 3=dump stored data over LOG_DATA_UUID in chunks.
// Deliberately NOT gated on scheduler_is_running() — logging load cell
// data is passive (doesn't move anything), unlike MOTOR_CMD/HOME_CMD/
// FREQ_CMD, so there's no safety reason to refuse it during a run.
class LogCtrlCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c) override {
        std::string v = c->getValue();
        if (v.size() != 1) return;
        switch ((uint8_t)v[0]) {
            case 0: data_logger_stop(); break;
            case 1: data_logger_start(); break;
            case 2: data_logger_clear(); break;
            case 3: _startDump(); break;
            default: break;
        }
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

    NimBLECharacteristic *logCtrlChar = svc->createCharacteristic(
        BIN_LOG_CTRL_UUID, NIMBLE_PROPERTY::WRITE);
    logCtrlChar->setCallbacks(new LogCtrlCB());
    _logDataChar = svc->createCharacteristic(BIN_LOG_DATA_UUID, NIMBLE_PROPERTY::NOTIFY);

    svc->start();

    // Also advertise this service's UUID so the dashboard's
    // requestDevice({filters: [{services: [SERVICE_UUID]}]}) can find it —
    // same reasoning as the NUS service UUID in ble_debug.cpp.
    NimBLEDevice::getAdvertising()->addServiceUUID(BIN_SERVICE_UUID);
}

// Sends one LOG_DATA chunk per call at most, throttled to
// DUMP_CHUNK_INTERVAL_MS — called every ble_binary_telemetry_update(), a
// no-op unless a dump is actually in progress. Chunk layout: seq(u16 LE) +
// totalChunks(u16 LE) + payloadLen(u16 LE) + payload bytes.
static void _serviceDump() {
    if (!_dumpInProgress) return;
    uint32_t now = millis();
    if (now - _lastDumpChunkMs < DUMP_CHUNK_INTERVAL_MS) return;
    _lastDumpChunkMs = now;

    uint8_t payload[LOG_CHUNK_PAYLOAD];
    size_t n = data_logger_read_chunk(payload, LOG_CHUNK_PAYLOAD);
    if (n == 0) {
        data_logger_close_read();
        _dumpInProgress = false;
        _sendLogLine("LOG_CTRL: dump complete");
        return;
    }

    uint8_t chunk[6 + LOG_CHUNK_PAYLOAD];
    chunk[0] = (uint8_t)(_dumpSeq & 0xFF);
    chunk[1] = (uint8_t)(_dumpSeq >> 8);
    chunk[2] = (uint8_t)(_dumpTotalChunks & 0xFF);
    chunk[3] = (uint8_t)(_dumpTotalChunks >> 8);
    chunk[4] = (uint8_t)(n & 0xFF);
    chunk[5] = (uint8_t)(n >> 8);
    memcpy(chunk + 6, payload, n);

    if (_logDataChar) {
        _logDataChar->setValue(chunk, 6 + n);
        _logDataChar->notify();
    }
    _dumpSeq++;
}

void ble_binary_telemetry_update() {
    if (!_telemetryChar) return;  // not yet initialized

    // Serviced independently of the telemetry throttle below — a dump in
    // progress needs to keep sending chunks every DUMP_CHUNK_INTERVAL_MS
    // regardless of whether this particular call also happens to be a
    // telemetry tick.
    _serviceDump();

    uint32_t now = millis();
    if (now - _lastTelemetryMs < TELEMETRY_INTERVAL_MS) return;
    _lastTelemetryMs = now;

    TelemetryPacket pkt{};
    pkt.uasVolts    = uas_read_mv() / 1000.0f;
    pkt.uasFreqHz   = uas_get_current_frequency_hz();
    pkt.sgResultM1  = motor_sg_result(1);
    pkt.sgResultM2  = motor_sg_result(2);
    pkt.positionM1  = motor_get_position(1);
    pkt.positionM2  = motor_get_position(2);
    pkt.homingFlags = motors_is_homed() ? 0x0C : 0x00;  // bits 2+3, see struct comment
    pkt.limitFlags  = (motor_limit_hit(1) ? 0x01 : 0x00) | (motor_limit_hit(2) ? 0x02 : 0x00)
                    | (motor_at_soft_limit(1) ? 0x04 : 0x00) | (motor_at_soft_limit(2) ? 0x08 : 0x00);
    pkt.forceRaw1   = force_sensor_get_raw_1();
    pkt.forceRaw2   = force_sensor_get_raw_2();
    pkt.alsClear    = turbidity_get_als_clear();
    pkt.irRaw       = turbidity_get_ir_raw();
    pkt.redRaw      = turbidity_get_red_raw();
    pkt.turbidityFlags = (turbidity_apds_ok() ? 0x01 : 0x00) | (turbidity_max_ok() ? 0x02 : 0x00);

    _telemetryChar->setValue((const uint8_t *)&pkt, sizeof(pkt));
    _telemetryChar->notify();
}
