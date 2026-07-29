#include "turbidity.h"
#include "config.h"
#include "ble_debug.h"
#include <Arduino.h>
#include <Wire.h>

// ---- APDS9960 registers (ALS transmission-mode use only) ----
static constexpr uint8_t APDS_ENABLE  = 0x80;
static constexpr uint8_t APDS_ATIME   = 0x81;
static constexpr uint8_t APDS_CONTROL = 0x8F;
static constexpr uint8_t APDS_ID      = 0x92;
static constexpr uint8_t APDS_CDATAL  = 0x94;

// ---- MAX30102 registers (backscatter mode) ----
static constexpr uint8_t MAX_FIFO_WR_PTR = 0x04;
static constexpr uint8_t MAX_FIFO_RD_PTR = 0x06;
static constexpr uint8_t MAX_FIFO_DATA   = 0x07;
static constexpr uint8_t MAX_MODE_CONFIG = 0x09;
static constexpr uint8_t MAX_SPO2_CONFIG = 0x0A;
static constexpr uint8_t MAX_LED1_PA     = 0x0C;  // RED
static constexpr uint8_t MAX_LED2_PA     = 0x0D;  // IR
static constexpr uint8_t MAX_PART_ID     = 0xFF;

static bool i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

static bool i2cReadRegs(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    size_t got = Wire.requestFrom((int)addr, (int)len);
    if (got != len) return false;
    for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

static bool _apdsOk = false;
static bool _maxOk = false;
static uint16_t _alsClear = 0;
static uint32_t _irRaw = 0, _redRaw = 0;

// Retry cadence for a device that's currently marked not-ok — a single
// transient I2C glitch (or a device that just wasn't powered up yet)
// shouldn't permanently drop a fusion channel for the rest of the run;
// see config.h.
static uint32_t _lastApdsRetryMs = 0;
static uint32_t _lastMaxRetryMs  = 0;

static bool _apdsTryInit() {
    uint8_t apdsId = 0;
    if (!i2cReadRegs(APDS9960_ADDR, APDS_ID, &apdsId, 1)) return false;
    i2cWriteReg(APDS9960_ADDR, APDS_ATIME, 0xC0);   // ~103ms integration
    i2cWriteReg(APDS9960_ADDR, APDS_CONTROL, 0x01); // ALS gain 4x
    i2cWriteReg(APDS9960_ADDR, APDS_ENABLE, 0x03);  // PON | AEN
    return true;
}

static bool _maxTryInit() {
    uint8_t partId = 0;
    if (!i2cReadRegs(MAX30102_ADDR, MAX_PART_ID, &partId, 1)) return false;
    i2cWriteReg(MAX30102_ADDR, MAX_MODE_CONFIG, 0x40); // reset
    delay(10);
    i2cWriteReg(MAX30102_ADDR, MAX_FIFO_WR_PTR, 0x00);
    i2cWriteReg(MAX30102_ADDR, MAX_FIFO_RD_PTR, 0x00);
    i2cWriteReg(MAX30102_ADDR, MAX_SPO2_CONFIG, 0x27); // 4096nA FS, 100Hz, 18-bit
    i2cWriteReg(MAX30102_ADDR, MAX_LED1_PA, 0x24);     // RED ~7mA
    i2cWriteReg(MAX30102_ADDR, MAX_LED2_PA, 0x24);     // IR ~7mA
    i2cWriteReg(MAX30102_ADDR, MAX_MODE_CONFIG, 0x03); // SpO2 mode
    return true;
}

void turbidity_init() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ);

    _apdsOk = _apdsTryInit();
    if (!_apdsOk) ble_log("Turbidity: APDS9960 not found at init — will keep retrying");

    _maxOk = _maxTryInit();
    if (!_maxOk) ble_log("Turbidity: MAX30102 not found at init — will keep retrying");
}

void turbidity_update() {
    uint32_t now = millis();

    if (!_apdsOk && (now - _lastApdsRetryMs) >= TURBIDITY_REINIT_INTERVAL_MS) {
        _lastApdsRetryMs = now;
        if (_apdsTryInit()) {
            _apdsOk = true;
            ble_log("Turbidity: APDS9960 recovered");
        }
    }
    if (!_maxOk && (now - _lastMaxRetryMs) >= TURBIDITY_REINIT_INTERVAL_MS) {
        _lastMaxRetryMs = now;
        if (_maxTryInit()) {
            _maxOk = true;
            ble_log("Turbidity: MAX30102 recovered");
        }
    }

    if (_apdsOk) {
        uint8_t buf[2] = {0, 0};
        if (i2cReadRegs(APDS9960_ADDR, APDS_CDATAL, buf, 2)) {
            _alsClear = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
        } else {
            _apdsOk = false;
            _lastApdsRetryMs = now;  // start the retry clock from the moment it dropped
            ble_log("Turbidity: APDS9960 read failed — marked not-ok, will retry");
        }
    }

    if (_maxOk) {
        uint8_t buf[6] = {0};
        if (i2cReadRegs(MAX30102_ADDR, MAX_FIFO_DATA, buf, 6)) {
            _redRaw = (((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2]) & 0x3FFFF;
            _irRaw  = (((uint32_t)buf[3] << 16) | ((uint32_t)buf[4] << 8) | buf[5]) & 0x3FFFF;
        } else {
            _maxOk = false;
            _lastMaxRetryMs = now;
            ble_log("Turbidity: MAX30102 read failed — marked not-ok, will retry");
        }
    }
}

bool turbidity_apds_ok() { return _apdsOk; }
bool turbidity_max_ok()  { return _maxOk; }

uint16_t turbidity_get_als_clear() { return _alsClear; }
uint32_t turbidity_get_ir_raw()    { return _irRaw; }
uint32_t turbidity_get_red_raw()   { return _redRaw; }
