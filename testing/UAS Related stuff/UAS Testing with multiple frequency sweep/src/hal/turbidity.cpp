#include "turbidity.h"
#include "config.h"
#include <Wire.h>

// ---- APDS9960 registers (ALS transmission-mode use only, §9) ----
static constexpr uint8_t APDS_ENABLE = 0x80;
static constexpr uint8_t APDS_ATIME  = 0x81;
static constexpr uint8_t APDS_CONTROL = 0x8F;
static constexpr uint8_t APDS_ID      = 0x92;
static constexpr uint8_t APDS_CDATAL  = 0x94;

// ---- MAX30102 registers (backscatter mode, §9) ----
static constexpr uint8_t MAX_INT_ENABLE_1 = 0x02;
static constexpr uint8_t MAX_FIFO_WR_PTR  = 0x04;
static constexpr uint8_t MAX_FIFO_RD_PTR  = 0x06;
static constexpr uint8_t MAX_FIFO_DATA    = 0x07;
static constexpr uint8_t MAX_MODE_CONFIG  = 0x09;
static constexpr uint8_t MAX_SPO2_CONFIG  = 0x0A;
static constexpr uint8_t MAX_LED1_PA      = 0x0C; // RED
static constexpr uint8_t MAX_LED2_PA      = 0x0D; // IR
static constexpr uint8_t MAX_PART_ID      = 0xFF;

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

static bool apdsPresent = false;
static bool maxPresent = false;

void turbidityInit() {
	Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ);

	// APDS9960: power on, enable ALS, ~103ms integration time (ATIME=0xC0),
	// 4x gain. Internal proximity/gesture engines left disabled — this
	// board only uses the ALS clear channel (§9).
	uint8_t apdsId = 0;
	apdsPresent = i2cReadRegs(APDS9960_ADDR, APDS_ID, &apdsId, 1);
	if (apdsPresent) {
		i2cWriteReg(APDS9960_ADDR, APDS_ATIME, 0xC0);
		i2cWriteReg(APDS9960_ADDR, APDS_CONTROL, 0x01); // ALS gain 4x
		i2cWriteReg(APDS9960_ADDR, APDS_ENABLE, 0x03);  // PON | AEN
	}

	// MAX30102: reset, then SpO2 mode (red+IR), 100Hz/16-bit, moderate LED
	// current — backscatter mode per brief, own onboard LEDs (§9).
	uint8_t partId = 0;
	maxPresent = i2cReadRegs(MAX30102_ADDR, MAX_PART_ID, &partId, 1);
	if (maxPresent) {
		i2cWriteReg(MAX30102_ADDR, MAX_MODE_CONFIG, 0x40); // reset
		delay(10);
		i2cWriteReg(MAX30102_ADDR, MAX_FIFO_WR_PTR, 0x00);
		i2cWriteReg(MAX30102_ADDR, MAX_FIFO_RD_PTR, 0x00);
		i2cWriteReg(MAX30102_ADDR, MAX_SPO2_CONFIG, 0x27); // 4096nA full-scale, 100Hz, 18-bit
		i2cWriteReg(MAX30102_ADDR, MAX_LED1_PA, 0x24);     // RED ~7mA
		i2cWriteReg(MAX30102_ADDR, MAX_LED2_PA, 0x24);     // IR ~7mA
		i2cWriteReg(MAX30102_ADDR, MAX_MODE_CONFIG, 0x03); // SpO2 mode
	}
}

TurbidityReading turbidityRead() {
	TurbidityReading r{};
	r.apdsOk = apdsPresent;
	r.maxOk = maxPresent;

	if (apdsPresent) {
		uint8_t buf[2] = {0, 0};
		if (i2cReadRegs(APDS9960_ADDR, APDS_CDATAL, buf, 2)) {
			r.alsClear = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
		} else {
			r.apdsOk = false;
		}
	}

	if (maxPresent) {
		uint8_t buf[6] = {0};
		if (i2cReadRegs(MAX30102_ADDR, MAX_FIFO_DATA, buf, 6)) {
			// Each sample is 3 bytes/channel, 18 significant bits, MSB first.
			r.redRaw = (((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2]) & 0x3FFFF;
			r.irRaw  = (((uint32_t)buf[3] << 16) | ((uint32_t)buf[4] << 8) | buf[5]) & 0x3FFFF;
		} else {
			r.maxOk = false;
		}
	}

	return r;
}
