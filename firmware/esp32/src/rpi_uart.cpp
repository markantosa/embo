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

// ── On-demand capture state ──────────────────────────────────────────────────
static bool _capture_pending   = false;
static bool _result_ready      = false;
static bool _timed_out         = false;
static uint32_t _capture_sent_ms = 0;

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

        if (_capture_pending) {
            _capture_pending = false;
            _result_ready = true;
        }
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

    if (_capture_pending && (millis() - _capture_sent_ms) >= RPI_CAPTURE_TIMEOUT_MS) {
        _capture_pending = false;
        _timed_out = true;
        ble_log("RPi: capture request timed out after %lu ms", (unsigned long)RPI_CAPTURE_TIMEOUT_MS);
    }
}

int16_t rpi_get_median_um() { return _median_um; }
int16_t rpi_get_iqr_um()    { return _iqr_um; }

void rpi_send(const char *msg) {
    _rpi.println(msg);
}

bool rpi_request_capture() {
    if (_capture_pending) return false;  // one in flight already
    _rpi.println("CAPTURE");
    _capture_pending = true;
    _result_ready = false;
    _timed_out = false;
    _capture_sent_ms = millis();
    ble_log("RPi: capture requested");
    return true;
}

RpiCaptureStatus rpi_capture_status() {
    if (_result_ready) return RpiCaptureStatus::RESULT_READY;
    if (_timed_out) return RpiCaptureStatus::TIMED_OUT;
    if (_capture_pending) return RpiCaptureStatus::PENDING;
    return RpiCaptureStatus::IDLE;
}

bool rpi_pop_capture_result(int16_t &medianOut, int16_t &iqrOut) {
    if (_result_ready) {
        _result_ready = false;
        medianOut = _median_um;
        iqrOut = _iqr_um;
        return true;
    }
    _timed_out = false;  // consumed either way
    return false;
}
