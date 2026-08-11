#pragma once

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

    void _draw(bool waiting, bool forceFull);
    // PROTOTYPE (testing/CV_Verify_UART_Prototype): histogram + median text
    // are fabricated locally, not from the RPi — detection.py/sizing.py
    // upstream are still unimplemented, so there is no real PSD to show
    // yet. Only the image itself is real (received over UART). See
    // rpi_uart.h's IMG protocol note for why.
    void _drawStubHistogramAndMedian();
};
