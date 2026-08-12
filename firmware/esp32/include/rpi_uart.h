#pragma once
#include <stdint.h>
#include <stdbool.h>

// CV is an on-demand, single-shot verification feature, NOT a continuous
// control input — the RPi only captures a frame and runs CV when explicitly
// asked via rpi_request_capture(). See scheduler.h for why mixing itself no
// longer depends on a live CV feed.

void rpi_uart_init();
void rpi_uart_update();     // call every loop() — parses incoming size packets

// Latest particle size stats received from RPi (µm). -1 = no data yet.
int16_t rpi_get_median_um();
int16_t rpi_get_iqr_um();

// Send a status/command string to RPi.
void rpi_send(const char *msg);

// ── On-demand capture + verify ───────────────────────────────────────────────
// Sends "CAPTURE\n" — the RPi captures one frame, runs CV once, and replies
// with the same "SIZE <median_um> <iqr_um>\n" line the old continuous stream
// used, just a single shot instead of a repeating one. A no-op (returns
// false) if a capture is already pending.
bool rpi_request_capture();

enum class RpiCaptureStatus { IDLE, PENDING, RESULT_READY, TIMED_OUT };

// PENDING until either a reply parses (RESULT_READY, one-shot — call
// rpi_pop_capture_result() to consume it and return to IDLE) or
// RPI_CAPTURE_TIMEOUT_MS (config.h) elapses with no reply (TIMED_OUT,
// also one-shot, cleared by rpi_pop_capture_result() or another request).
RpiCaptureStatus rpi_capture_status();

// Consumes a RESULT_READY/TIMED_OUT status, resetting to IDLE. Returns true
// and fills medianOut/iqrOut only when the status was RESULT_READY.
bool rpi_pop_capture_result(int16_t &medianOut, int16_t &iqrOut);

// ── Raw preview image (prototype, see testing/CV_Verify_UART_Prototype) ────
// Wire format: "IMG <width> <height>\n" followed by exactly width*height
// raw greyscale bytes (1 byte/pixel, row-major, no line ending after the
// binary payload — length is already known from the header, so no
// delimiter is needed and none is expected).
//
// This is the prototype's answer to the "future work" JPEG note that used
// to live here — deliberately NOT a JPEG: this board (ESP32-S3-WROOM-1-N4)
// most likely has no PSRAM, so a small fixed-size raw buffer this program
// can size at compile time is a much safer bet than pulling in a JPEG
// decoder and its transient memory footprint. 160x100 chosen as a size
// that's fast over UART (~16KB, ~175ms @ 921600 baud) and fits directly
// into the 240px-wide display without needing to scale.
//
// Receiving an image completes the pending capture the same way a SIZE
// line does (sets RESULT_READY) — this prototype's Pi side doesn't send a
// SIZE line at all (detection.py is still unimplemented upstream), so
// image arrival IS the "result" for now. Once real detection/sizing
// exists, a SIZE line arriving separately will still work through the
// same existing path — the two aren't mutually exclusive.
#define RPI_IMG_MAX_W 160
#define RPI_IMG_MAX_H 100

// Raw greyscale pixel buffer for the most recently received image — valid
// once RpiCaptureStatus::RESULT_READY has fired (or was popped from it);
// contents are undefined/stale before the first successful image receive.
const uint8_t *rpi_get_last_image();
uint16_t rpi_get_last_image_width();
uint16_t rpi_get_last_image_height();
