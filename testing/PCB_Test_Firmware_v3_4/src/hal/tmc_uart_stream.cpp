#include "tmc_uart_stream.h"
#include <driver/gpio.h>

bool TmcUartStream::begin(uart_port_t uartNum, int txGpio, int rxGpio, uint32_t baud) {
	_uartNum = uartNum;

	uart_config_t cfg = {};
	cfg.baud_rate = (int)baud;
	cfg.data_bits = UART_DATA_8_BITS;
	cfg.parity = UART_PARITY_DISABLE;
	cfg.stop_bits = UART_STOP_BITS_1;
	cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
	cfg.source_clk = UART_SCLK_DEFAULT;

	if (uart_driver_install(_uartNum, 256, 256, 0, nullptr, 0) != ESP_OK) return false;
	if (uart_param_config(_uartNum, &cfg) != ESP_OK) return false;

	// Separate TX/RX GPIOs, both wired to the same physical PDN_UART bus node.
	if (uart_set_pin(_uartNum, txGpio, rxGpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) return false;

	// TX: open-drain, since it must coexist on the shared bus with the
	// TMC2209 pulling the same line low to reply. External R_PDN_UP already
	// pulls the line up, so no internal pull needed here.
	gpio_set_direction((gpio_num_t)txGpio, GPIO_MODE_INPUT_OUTPUT_OD);
	gpio_set_pull_mode((gpio_num_t)txGpio, GPIO_FLOATING);

	// RX: plain input. GPIO46 (our jumper target) is hardware input-only
	// anyway — it physically cannot drive the bus, which is the point.
	// Skip this entirely when rxGpio == txGpio (single-pin mode) — applying
	// GPIO_MODE_INPUT to the same pin here would silently overwrite and
	// undo the open-drain output config set two lines above, leaving the
	// pin unable to transmit at all.
	if (rxGpio != txGpio) {
		gpio_set_direction((gpio_num_t)rxGpio, GPIO_MODE_INPUT);
		gpio_set_pull_mode((gpio_num_t)rxGpio, GPIO_FLOATING);
	}

	return true;
}

int TmcUartStream::available() {
	size_t n = 0;
	uart_get_buffered_data_len(_uartNum, &n);
	return (int)n;
}

int TmcUartStream::read() {
	uint8_t b;
	int n = uart_read_bytes(_uartNum, &b, 1, 0);
	return n > 0 ? b : -1;
}

int TmcUartStream::peek() {
	return -1; // not used by TMCStepper; ESP-IDF's uart driver has no native peek
}

size_t TmcUartStream::write(uint8_t b) {
	int n = uart_write_bytes(_uartNum, (const char *)&b, 1);
	return n > 0 ? (size_t)n : 0;
}

size_t TmcUartStream::write(const uint8_t *buffer, size_t size) {
	int n = uart_write_bytes(_uartNum, (const char *)buffer, size);
	return n > 0 ? (size_t)n : 0;
}
