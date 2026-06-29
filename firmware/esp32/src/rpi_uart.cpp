#include "rpi_uart.h"
#include "config.h"
#include <Arduino.h>

// UART1 routed to GPIO47 TX / GPIO48 RX via GPIO matrix
static HardwareSerial _rpi(1);

static int16_t _median_um = -1;
static int16_t _iqr_um    = -1;

// Simple line-based protocol: "SIZE median_um iqr_um\n"
static char _buf[64];
static uint8_t _buf_pos = 0;

void rpi_uart_init() {
    _rpi.begin(BAUD_RPI, SERIAL_8N1, PIN_RPI_RX, PIN_RPI_TX);
}

void rpi_uart_update() {
    while (_rpi.available()) {
        char c = _rpi.read();
        if (c == '\n' || _buf_pos >= sizeof(_buf) - 1) {
            _buf[_buf_pos] = '\0';
            _buf_pos = 0;
            // TODO: parse "SIZE <median> <iqr>" and update _median_um / _iqr_um
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
