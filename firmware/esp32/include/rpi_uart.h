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

// Future work (RPi/software side, not yet implemented here): the RPi may
// eventually also send a compressed JPEG of the annotated frame (particle
// circles overlaid) alongside the SIZE line. No wire format for that exists
// yet — this header intentionally does not stub it out until that protocol
// is decided, to avoid a half-finished binary-transfer path.
