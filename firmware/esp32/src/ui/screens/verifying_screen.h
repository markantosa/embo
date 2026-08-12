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
    char _specMsg[16]   = "";
    bool _inSpec        = false;
    bool _timedOut      = false;

    // Populated from rpi_pop_capture_result() in update() — detection.py/
    // sizing.py upstream may still be unimplemented on the RPi side, so
    // _gotSize can be false and _draw() then shows a "no size data"
    // placeholder instead of the real result. Once the RPi starts sending
    // real "SIZE <median> <iqr>" lines (after training completes), these
    // fill in and the real result + IN SPEC/OUT OF SPEC check render
    // automatically — no firmware change/reflash needed for that
    // transition.
    int16_t _lastMedianUm = -1;
    int16_t _lastIqrUm    = -1;
    bool _gotSize          = false;

    void _draw(bool waiting, bool forceFull);
    // Draws real median/IQR + IN SPEC/OUT OF SPEC when available
    // (_gotSize), otherwise a "no size data yet" placeholder. No per-bin
    // histogram is drawn — the wire protocol (rpi_uart.h) only carries
    // median+IQR, not a bucketed distribution, so the IQR is shown as a
    // spread bar around the median rather than a fabricated histogram.
    void _drawSizeResult(int16_t x, int16_t y, int16_t w, int16_t colH);
};
