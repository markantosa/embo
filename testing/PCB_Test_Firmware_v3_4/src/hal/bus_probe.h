#pragma once

// Software "logic analyzer" for the shared TMC2209 UART line (GPIO4), built
// on the ESP32-S3's RMT peripheral (a hardware pulse-timer, unrelated use
// case originally but exactly what we need: it records the duration of
// every level transition on a GPIO with hardware timestamps, no CPU jitter).
//
// Bring-up aid only: lets us see whether the driver replies at all, replies
// garbled, or stays silent, without needing a scope/logic analyzer on hand.

void busProbeInit();

// Arms a capture window, triggers a TMC read via motorM1, waits briefly for
// the capture to finish, and logs the raw edge list via bleLog().
void busProbeCaptureAndLog();
