#include "tmc2209_uart.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cmath>
#include <cstring>

namespace tmc2209 {

static uart_port_t s_uart = UART_NUM_1;
static bool s_ready = false;

// TMC-specific CRC8 ("CRC8-ATM"), computed MSB-first bit by bit — same
// algorithm TMCStepper's CRC8() implements, per the TMC2208/09 datasheet.
static uint8_t crc8(const uint8_t *data, uint8_t len) {
	uint8_t crc = 0;
	for (uint8_t i = 0; i < len; i++) {
		uint8_t currentByte = data[i];
		for (uint8_t b = 0; b < 8; b++) {
			if ((crc >> 7) ^ (currentByte & 0x01)) {
				crc = (uint8_t)((crc << 1) ^ 0x07);
			} else {
				crc = (uint8_t)(crc << 1);
			}
			currentByte >>= 1;
		}
	}
	return crc;
}

bool busInit(uart_port_t uartNum, int txGpio, int rxGpio, uint32_t baud) {
	s_uart = uartNum;

	uart_config_t cfg = {};
	cfg.baud_rate = (int)baud;
	cfg.data_bits = UART_DATA_8_BITS;
	cfg.parity = UART_PARITY_DISABLE;
	cfg.stop_bits = UART_STOP_BITS_1;
	cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
	cfg.source_clk = UART_SCLK_DEFAULT;

	if (uart_driver_install(s_uart, 256, 256, 0, nullptr, 0) != ESP_OK) return false;
	if (uart_param_config(s_uart, &cfg) != ESP_OK) return false;

	// Separate TX/RX GPIOs, both wired to the same physical PDN_UART bus node.
	if (uart_set_pin(s_uart, txGpio, rxGpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) return false;

	// TX: open-drain, since it must coexist on the shared bus with the
	// TMC2209 pulling the same line low to reply. External R_PDN_UP already
	// pulls the line up, so no internal pull needed here.
	gpio_set_direction((gpio_num_t)txGpio, GPIO_MODE_INPUT_OUTPUT_OD);
	gpio_set_pull_mode((gpio_num_t)txGpio, GPIO_FLOATING);

	// RX: plain input, unless it's the same physical pin as TX (single-pin
	// mode) — in that case leave the open-drain output config above alone.
	if (rxGpio != txGpio) {
		gpio_set_direction((gpio_num_t)rxGpio, GPIO_MODE_INPUT);
		gpio_set_pull_mode((gpio_num_t)rxGpio, GPIO_FLOATING);
	}

	s_ready = true;
	return true;
}

bool writeReg(uint8_t addr, uint8_t reg, uint32_t val) {
	if (!s_ready) return false;
	uint8_t dg[8];
	dg[0] = 0x05;
	dg[1] = addr;
	dg[2] = reg | 0x80; // write bit
	dg[3] = (uint8_t)(val >> 24);
	dg[4] = (uint8_t)(val >> 16);
	dg[5] = (uint8_t)(val >> 8);
	dg[6] = (uint8_t)(val);
	dg[7] = crc8(dg, 7);

	int n = uart_write_bytes(s_uart, (const char *)dg, sizeof(dg));
	return n == (int)sizeof(dg);
}

bool readReg(uint8_t addr, uint8_t reg, uint32_t &val) {
	if (!s_ready) return false;

	uart_flush_input(s_uart);

	uint8_t req[4];
	req[0] = 0x05;
	req[1] = addr;
	req[2] = reg;
	req[3] = crc8(req, 3);

	if (uart_write_bytes(s_uart, (const char *)req, sizeof(req)) != (int)sizeof(req)) return false;

	// Because TX and RX share the same physical bus node, we first read back
	// our own 4-byte request as a bus echo, then the driver's 8-byte reply.
	uint8_t buf[12];
	size_t total = 0;
	while (total < sizeof(buf)) {
		int n = uart_read_bytes(s_uart, buf + total, sizeof(buf) - total, pdMS_TO_TICKS(5));
		if (n <= 0) break;
		total += (size_t)n;
	}
	if (total < sizeof(buf)) return false;

	const uint8_t *reply = buf + 4;
	if (reply[0] != 0x05) return false;
	if (reply[2] != reg) return false;
	if (crc8(reply, 7) != reply[7]) return false;

	val = ((uint32_t)reply[3] << 24) | ((uint32_t)reply[4] << 16) |
	      ((uint32_t)reply[5] << 8) | (uint32_t)reply[6];
	return true;
}

// Current scaling — TMCStepper's rms_current() formula (simplified: no
// internal-sense/high-sensitivity vsense switching, since this board's
// 110mOhm MKS modules never need it at these bench-test currents):
//   CS = 32 * sqrt(2) * I_rms[A] * (R_sense + 0.02) / 0.325 - 1, clamped 0-31.
static uint8_t currentScale(uint16_t mA) {
	float cs = 32.0f * 1.41421356f * ((float)mA / 1000.0f) * (TMC_R_SENSE + 0.02f) / 0.325f - 1.0f;
	if (cs < 0.0f) cs = 0.0f;
	if (cs > 31.0f) cs = 31.0f;
	return (uint8_t)(cs + 0.5f);
}

bool configureDriver(uint8_t addr, uint16_t runCurrentMa, uint16_t holdCurrentMa) {
	bool ok = true;

	// GCONF: pdn_disable=1 (UART, not step/dir enable, uses PDN pin),
	// i_scale_analog=0, en_spreadCycle=1 (brief §7.4 mandates SpreadCycle).
	uint32_t gconf = 0;
	ok &= readReg(addr, REG_GCONF, gconf);
	gconf |= GCONF_PDN_DISABLE;
	gconf |= GCONF_EN_SPREADCYCLE;
	gconf &= ~GCONF_I_SCALE_ANALOG;
	ok &= writeReg(addr, REG_GCONF, gconf);

	// CHOPCONF: toff = 4 (bits 3:0), leave the rest at hardware defaults.
	uint32_t chopconf = 0;
	ok &= readReg(addr, REG_CHOPCONF, chopconf);
	chopconf = (chopconf & ~0xFu) | 0x4u;
	ok &= writeReg(addr, REG_CHOPCONF, chopconf);

	// IHOLD_IRUN: IHOLD bits[4:0], IRUN bits[12:8]. Leave IHOLDDELAY
	// bits[19:16] at whatever the hardware default is.
	uint8_t irun = currentScale(runCurrentMa);
	uint8_t ihold = (uint8_t)((uint32_t)irun * holdCurrentMa / runCurrentMa);
	uint32_t iholdirun = 0;
	ok &= readReg(addr, REG_IHOLD_IRUN, iholdirun);
	iholdirun &= ~(0x1Fu | (0x1Fu << 8));
	iholdirun |= ((uint32_t)ihold & 0x1Fu);
	iholdirun |= (((uint32_t)irun & 0x1Fu) << 8);
	ok &= writeReg(addr, REG_IHOLD_IRUN, iholdirun);

	// SLAVECONF: SENDDELAY bits[11:8] = 4 — "In a multiple node system, set
	// SENDDELAY minimum 2 to ensure clean bus transitions" (datasheet); we
	// have 2 drivers sharing one UART bus.
	uint32_t slaveconf = 0;
	ok &= readReg(addr, REG_SLAVECONF, slaveconf);
	slaveconf = (slaveconf & ~(0xFu << 8)) | (0x4u << 8);
	ok &= writeReg(addr, REG_SLAVECONF, slaveconf);

	return ok;
}

bool confirmSpreadCycle(uint8_t addr) {
	uint32_t gconf = 0;
	if (!readReg(addr, REG_GCONF, gconf)) return false;
	gconf |= GCONF_EN_SPREADCYCLE;
	if (!writeReg(addr, REG_GCONF, gconf)) return false;
	if (!readReg(addr, REG_GCONF, gconf)) return false;
	return (gconf & GCONF_EN_SPREADCYCLE) != 0;
}

uint8_t testConnection(uint8_t addr) {
	uint32_t ioin = 0;
	if (!readReg(addr, REG_IOIN, ioin)) return 2; // communication failure
	uint8_t version = (uint8_t)(ioin >> 24);
	return (version == TMC2209_VERSION) ? 0 : 1;
}

uint16_t readStallGuard(uint8_t addr) {
	uint32_t sg = 0;
	if (!readReg(addr, REG_SG_RESULT, sg)) return 0;
	return (uint16_t)(sg & 0x3FFu);
}

} // namespace tmc2209
