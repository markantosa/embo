#include "turbidity.h"
#include "config.h"
#include "driver/i2c.h"

// Uses the legacy ESP-IDF driver/i2c.h API (not driver/i2c_master.h) — this
// PlatformIO espressif32 platform pins ESP-IDF 5.1.4, which predates IDF's
// newer i2c_master.h bus/device API (added in IDF 5.2). If this project is
// ever rebuilt against a newer pioarduino/espidf release, i2c_master.h would
// be the preferred replacement.

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

static constexpr i2c_port_t I2C_PORT = I2C_NUM_0;

static bool i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
	uint8_t buf[2] = {reg, val};
	return i2c_master_write_to_device(I2C_PORT, addr, buf, sizeof(buf), pdMS_TO_TICKS(100)) == ESP_OK;
}

static bool i2cReadRegs(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len) {
	return i2c_master_write_read_device(I2C_PORT, addr, &reg, 1, buf, len, pdMS_TO_TICKS(100)) == ESP_OK;
}

static bool apdsPresent = false;
static bool maxPresent = false;

void turbidityInit() {
	i2c_config_t cfg = {};
	cfg.mode = I2C_MODE_MASTER;
	cfg.sda_io_num = PIN_I2C_SDA;
	cfg.scl_io_num = PIN_I2C_SCL;
	cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
	cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
	cfg.master.clk_speed = I2C_CLOCK_HZ;
	i2c_param_config(I2C_PORT, &cfg);
	i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);

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
