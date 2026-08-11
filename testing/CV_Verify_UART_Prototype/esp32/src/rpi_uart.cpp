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

// ── Raw image receive state ──────────────────────────────────────────────
// Static buffer, not malloc'd — this board most likely has no PSRAM, so a
// fixed compile-time-sized buffer is the safer choice over anything
// dynamically sized off an attacker/bug-controlled header value.
static uint8_t _img_buf[RPI_IMG_MAX_W * RPI_IMG_MAX_H];
static uint16_t _img_w = 0, _img_h = 0;
static uint32_t _img_bytes_expected = 0;
static uint32_t _img_bytes_received = 0;
static bool _receiving_image = false;

// ── On-demand capture state ──────────────────────────────────────────────────
static bool _capture_pending   = false;
static bool _result_ready      = false;
static bool _timed_out         = false;
static uint32_t _capture_sent_ms = 0;

void rpi_uart_init() {
    _rpi.begin(BAUD_RPI, SERIAL_8N1, PIN_RPI_RX, PIN_RPI_TX);
}

static void _parse_line() {
    // "IMG <width> <height>" — switches the reader into binary mode for
    // the next width*height bytes, see rpi_uart_update(). Checked before
    // SIZE since it's the prototype's primary path (see rpi_uart.h).
    unsigned int w, h;
    if (sscanf(_buf, "IMG %u %u", &w, &h) == 2) {
        if (w > RPI_IMG_MAX_W || h > RPI_IMG_MAX_H || w == 0 || h == 0) {
            ble_log("RPi: IMG header %ux%u exceeds %ux%u max, ignored",
                    w, h, RPI_IMG_MAX_W, RPI_IMG_MAX_H);
            return;
        }
        _img_w = (uint16_t)w;
        _img_h = (uint16_t)h;
        _img_bytes_expected = (uint32_t)w * h;
        _img_bytes_received = 0;
        _receiving_image = true;
        return;
    }

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
        if (_receiving_image) {
            // Binary mode: consume raw bytes directly, no line buffering —
            // image bytes may legitimately contain \n/\r, so the line
            // parser below must not see them.
            int n = _rpi.readBytes(_img_buf + _img_bytes_received,
                                    min((uint32_t)_rpi.available(),
                                        _img_bytes_expected - _img_bytes_received));
            _img_bytes_received += n;
            if (_img_bytes_received >= _img_bytes_expected) {
                _receiving_image = false;
                ble_log("RPi: image received %ux%u", _img_w, _img_h);
                if (_capture_pending) {
                    _capture_pending = false;
                    _result_ready = true;
                }
            }
            continue;
        }

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

const uint8_t *rpi_get_last_image()      { return _img_buf; }
uint16_t rpi_get_last_image_width()      { return _img_w; }
uint16_t rpi_get_last_image_height()     { return _img_h; }

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
