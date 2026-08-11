#include "menu_screen.h"
#include "ui_screen_manager.h"
#include "ui_input.h"
#include "ui_display.h"
#include "scheduler.h"

MenuScreen::MenuScreen(const char *title, const MenuItem *items, uint8_t count, const char *subtitle)
    : _title(title), _items(items), _count(count), _subtitle(subtitle) {}

void MenuScreen::onEnter() {
    // Default to the first item, but skip forward to the first ENABLED
    // one if that first item happens to be gated off — otherwise the
    // cursor would land on a greyed-out, unselectable item by default.
    // Falls back to 0 if every item is somehow disabled (nothing better
    // to land on).
    _selected = 0;
    for (uint8_t i = 0; i < _count; i++) {
        if (!_items[i].isEnabled || _items[i].isEnabled()) {
            _selected = i;
            break;
        }
    }
}

void MenuScreen::_draw(bool forceFull) {
    LGFX &tft = ui_display_tft();
    if (forceFull) {
        tft.fillScreen(TFT_WHITE); // background colour 
        tft.setFont(&fonts::FreeSansBold12pt7b); // setting the font
        ui_display_draw_centered(_title, 24, TFT_BLACK, 1); // drawing 
        if (_subtitle) {
            tft.setFont(&fonts::FreeSans9pt7b);
            ui_display_draw_centered(_subtitle, 48, COLOR_ASH, 1);
        }
        tft.drawFastHLine(30, 62, tft.width() - 60, TFT_BLACK); // line below title line 
    }

    // Redraw just the item list — cheap enough to do every time something
    // changes, and avoids a full-screen flicker on every encoder detent.
    int16_t top  = 90;
    int16_t rowH = 40;
    tft.fillRect(0, top, tft.width(), rowH * _count, TFT_WHITE);
    tft.setFont(&fonts::FreeSans9pt7b);
    for (uint8_t i = 0; i < _count; i++) {
        bool sel = (i == _selected);
        bool enabled = !_items[i].isEnabled || _items[i].isEnabled();
        int16_t rowY = top + i * rowH;
        if (sel && enabled) {
            tft.fillRoundRect(20, rowY, tft.width() - 40, rowH - 8, 8, COLOR_BRIGHT_BLUE);
        }
        uint16_t textColor;
        if (!enabled) {
            textColor = COLOR_ASH;  // dark grey, greyed out — same whether or not the cursor is on it
        } else {
            textColor = sel ? TFT_WHITE : COLOR_BRIGHT_BLUE;
        }
        ui_display_draw_centered(_items[i].label, rowY + 10, textColor, 1);
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
        const MenuItem &item = _items[_selected];
        if (!item.isEnabled || item.isEnabled()) {
            item.onSelect(mgr);
        }
        // Disabled item: no-op. It's still visibly there (greyed out) and
        // still reachable by rotating to it, it just can't be confirmed.
    }

    // BTN1 = Back here — but ONLY when nothing is running. This screen
    // isn't currently reachable while a run is active (nothing pushes a
    // menu from the running screen), but gating on scheduler_is_running()
    // rather than relying on that structural guarantee means BTN1 still
    // can't be mistaken for "back" instead of "stop" even if that ever
    // changes — see mixing_running_screen.h, where BTN1's stop/e-stop
    // function is untouched and remains the only thing it does there.
    // mgr.pop() itself is a safe no-op on the root screen (Start Menu),
    // so this doesn't need a separate "is this the root" check.
    if (!scheduler_is_running() && ui_input_poll_btn1() == ButtonEvent::SHORT_PRESS) {
        mgr.pop();
    }
}
