#include "rpi_uart.h"
#include "config.h"
#include "ble_debug.h"
#include <Arduino.h>
#include <stdio.h>

// UART2 routed to GPIO47 TX / GPIO48 RX via GPIO matrix.
// UART1 is reserved for TMC2209 half-duplex on GPIO4 (motors.cpp).
static HardwareSerial _rpi(2);

static int16_t _median_um = -1;
static int16_t _iqr_um    = -1;

// Simple line-based protocol: "SIZE median_um iqr_um\n"
static char _buf[64];
static uint8_t _buf_pos = 0;

void rpi_uart_init() {
    _rpi.begin(BAUD_RPI, SERIAL_8N1, PIN_RPI_RX, PIN_RPI_TX);
}

static void _parse_line() {
    // Expected format: "SIZE <median_um> <iqr_um>"
    // Values are positive integers. Anything else is silently discarded.
    int median, iqr;
    if (sscanf(_buf, "SIZE %d %d", &median, &iqr) == 2
            && median > 0 && iqr >= 0) {
        _median_um = (int16_t)median;
        _iqr_um    = (int16_t)iqr;
        ble_log("RPi: median=%d iqr=%d um", _median_um, _iqr_um);
    }
    // Unrecognised lines (status strings, errors from RPi) are ignored.
}

void rpi_uart_update() {
    while (_rpi.available()) {
        char c = _rpi.read();
        if (c == '\r') continue;  // strip Windows-style CR if present
        if (c == '\n' || _buf_pos >= sizeof(_buf) - 1) {
            _buf[_buf_pos] = '\0';
            _buf_pos = 0;
            if (_buf[0] != '\0') _parse_line();
        } else {
            _buf[_buf_pos++] = c;
        }
    }
}

int16_t rpi_get_median_um() { return _median_um; }
int16_t rpi_get_iqr_um()    { return _iqr_um; }

void rpi_send(const char *msg) {
    _rpi.println(msg);
}
