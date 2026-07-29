#include "menu_screen.h"
#include "ui_screen_manager.h"
#include "ui_input.h"
#include "ui_display.h"

MenuScreen::MenuScreen(const char *title, const MenuItem *items, uint8_t count)
    : _title(title), _items(items), _count(count) {}

void MenuScreen::onEnter() {
    _selected = 0;
}

void MenuScreen::_draw(bool forceFull) {
    LGFX &tft = ui_display_tft();
    if (forceFull) {
        tft.fillScreen(TFT_BLACK);
        ui_display_draw_centered(_title, 30, TFT_WHITE, 3);
    }

    // Redraw just the item list — cheap enough to do every time something
    // changes, and avoids a full-screen flicker on every encoder detent.
    int16_t top = 80;
    int16_t rowH = 30;
    tft.fillRect(0, top, tft.width(), rowH * _count, TFT_BLACK);
    for (uint8_t i = 0; i < _count; i++) {
        bool sel = (i == _selected);
        ui_display_draw_centered(_items[i].label, top + i * rowH, sel ? TFT_GREEN : TFT_DARKGREY, 2);
    }
}

void MenuScreen::update(ScreenManager &mgr, bool forceFull) {
    int step = ui_input_read_encoder_step();
    bool changed = forceFull;
    if (step != 0 && _count > 0) {
        int16_t next = (int16_t)_selected + step;
        while (next < 0) next += _count;
        _selected = (uint8_t)(next % _count);
        changed = true;
    }

    if (changed) _draw(forceFull);

    if (ui_input_poll_enc_sw() == ButtonEvent::SHORT_PRESS && _count > 0) {
        _items[_selected].onSelect(mgr);
    }

    // BTN1 stays the dedicated stop button everywhere, including here —
    // it's not read by this screen at all, so if a run is ever active
    // behind a menu, screens above it never swallow the e-stop input.
    // (Today nothing pushes a menu while a run is active, but this keeps
    // that guarantee even if a future screen does.)
}
