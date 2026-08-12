#pragma once

#include <stdint.h>
#include "ui_screen.h"

// Optional, operator-triggered, single-shot CV check — NOT part of the
// mixing loop itself (see scheduler.h). Reachable via push() from any
// screen with a "hold knob to verify" hint (set-target, done); always
// returns to whichever screen pushed it via pop(), so it needs no
// knowledge of its caller.
class VerifyingScreen : public Screen {
public:
    void onEnter() override;
    void update(ScreenManager &mgr, bool forceFull) override;

private:
    char _resultMsg[40] = "";
    bool _timedOut       = false;

    // Populated from rpi_pop_capture_result() in update(). _gotSize is
    // only true when a real "SIZE <median> <iqr>" line actually arrived
    // this capture (e.g. no particles detected still completes the
    // capture via the image alone, but leaves _gotSize false) — see
    // _drawSizeResult()'s placeholder branch.
    int16_t _lastMedianUm = -1;
    int16_t _lastIqrUm    = -1;
    bool _gotSize          = false;
    bool _inSpec           = false;

    void _draw(bool waiting, bool forceFull);
    // Draws real median/IQR/in-spec status when available (_gotSize),
    // otherwise a "no size data" placeholder (e.g. zero particles
    // detected this capture). No per-bin histogram is drawn — the wire
    // protocol (rpi_uart.h) only carries median+IQR, not a bucketed
    // distribution, so the IQR is shown as a spread bar around the
    // median rather than fabricated bars.
    void _drawSizeResult(int16_t x, int16_t y, int16_t w, int16_t colH);
};
