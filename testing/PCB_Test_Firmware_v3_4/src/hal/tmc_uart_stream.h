#pragma once

#include <Arduino.h>
#include <driver/uart.h>

// Minimal Stream wrapper around the raw ESP-IDF UART driver, used instead of
// Arduino's HardwareSerial for the TMC2209 bus.
//
// Uses genuinely separate TX and RX GPIOs, both wired to the same physical
// PDN_UART bus node (TX through the board's R_UART, RX via a jumper wire
// direct to the same net — see config.h). A same-pin shared TX/RX config was
// tried first (both via HardwareSerial and via this same raw-IDF class) and
// failed consistently regardless of software approach; separate pins is the
// standard, proven pattern for this exact chip+bus topology.
class TmcUartStream : public Stream {
public:
	bool begin(uart_port_t uartNum, int txGpio, int rxGpio, uint32_t baud);

	int available() override;
	int read() override;
	int peek() override;
	size_t write(uint8_t b) override;
	size_t write(const uint8_t *buffer, size_t size) override;

private:
	uart_port_t _uartNum = UART_NUM_1;
};
