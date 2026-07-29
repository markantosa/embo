#include "done_screen.h"
#include "set_target_screen.h"
#include "verifying_screen.h"
#include "ui_screen_manager.h"
#include "ui_input.h"
#include "ui_display.h"
#include <string.h>

void DoneScreen::setResult(const char *msg) {
    strncpy(_resultMsg, msg, sizeof(_resultMsg) - 1);
    _resultMsg[sizeof(_resultMsg) - 1] = '\0';
}

void DoneScreen::_draw() {
    LGFX &tft = ui_display_tft();
    tft.fillScreen(TFT_BLACK);
    ui_display_draw_centered(_resultMsg, 100, TFT_GREEN, 2);
    ui_display_draw_centered("Press knob for new run", 190, TFT_DARKGREY, 2);
    ui_display_draw_centered("Hold knob to verify with camera", 220, TFT_DARKGREY, 1);
}

void DoneScreen::update(ScreenManager &mgr, bool forceFull) {
    if (forceFull) _draw();

    ButtonEvent ev = ui_input_poll_enc_sw();
    if (ev == ButtonEvent::SHORT_PRESS && _setTargetScreen) {
        mgr.goTo(_setTargetScreen);  // no way back -- starting a new run
    } else if (ev == ButtonEvent::LONG_PRESS && _verifyingScreen) {
        mgr.push(_verifyingScreen);  // verify returns here via pop()
    }
}
