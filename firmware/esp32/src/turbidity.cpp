#include "turbidity.h"
#include "config.h"
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

void turbidity_init() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ);

    uint8_t apdsId = 0;
    _apdsOk = i2cReadRegs(APDS9960_ADDR, APDS_ID, &apdsId, 1);
    if (_apdsOk) {
        i2cWriteReg(APDS9960_ADDR, APDS_ATIME, 0xC0);   // ~103ms integration
        i2cWriteReg(APDS9960_ADDR, APDS_CONTROL, 0x01); // ALS gain 4x
        i2cWriteReg(APDS9960_ADDR, APDS_ENABLE, 0x03);  // PON | AEN
    }

    uint8_t partId = 0;
    _maxOk = i2cReadRegs(MAX30102_ADDR, MAX_PART_ID, &partId, 1);
    if (_maxOk) {
        i2cWriteReg(MAX30102_ADDR, MAX_MODE_CONFIG, 0x40); // reset
        delay(10);
        i2cWriteReg(MAX30102_ADDR, MAX_FIFO_WR_PTR, 0x00);
        i2cWriteReg(MAX30102_ADDR, MAX_FIFO_RD_PTR, 0x00);
        i2cWriteReg(MAX30102_ADDR, MAX_SPO2_CONFIG, 0x27); // 4096nA FS, 100Hz, 18-bit
        i2cWriteReg(MAX30102_ADDR, MAX_LED1_PA, 0x24);     // RED ~7mA
        i2cWriteReg(MAX30102_ADDR, MAX_LED2_PA, 0x24);     // IR ~7mA
        i2cWriteReg(MAX30102_ADDR, MAX_MODE_CONFIG, 0x03); // SpO2 mode
    }
}

void turbidity_update() {
    if (_apdsOk) {
        uint8_t buf[2] = {0, 0};
        if (i2cReadRegs(APDS9960_ADDR, APDS_CDATAL, buf, 2)) {
            _alsClear = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
        } else {
            _apdsOk = false;
        }
    }

    if (_maxOk) {
        uint8_t buf[6] = {0};
        if (i2cReadRegs(MAX30102_ADDR, MAX_FIFO_DATA, buf, 6)) {
            _redRaw = (((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2]) & 0x3FFFF;
            _irRaw  = (((uint32_t)buf[3] << 16) | ((uint32_t)buf[4] << 8) | buf[5]) & 0x3FFFF;
        } else {
            _maxOk = false;
        }
    }
}

bool turbidity_apds_ok() { return _apdsOk; }
bool turbidity_max_ok()  { return _maxOk; }

uint16_t turbidity_get_als_clear() { return _alsClear; }
uint32_t turbidity_get_ir_raw()    { return _irRaw; }
uint32_t turbidity_get_red_raw()   { return _redRaw; }
