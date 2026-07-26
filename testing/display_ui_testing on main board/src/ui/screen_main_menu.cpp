#pragma once
#include "backend/menu_logic.h"
#include "hal/LGFX_Config.h"

class ScreenMenu {
public:
    // Tune these to taste / to match your other screens
    static constexpr uint16_t COLOR_BG        = TFT_WHITE;
    static constexpr uint16_t COLOR_TEXT      = TFT_BLACK;
    static constexpr uint16_t COLOR_TEXT_DIM  = 0x8410; // muted grey for unselected items
    static constexpr uint16_t COLOR_ACCENT    = 0x64DC; // your existing accent blue

    void render(LGFX& tft, const MenuLogic& menu) {
        tft.fillScreen(COLOR_BG);
        const auto& items = menu.getItems();

        const int rowHeight = 40;                 // generous spacing = minimalist breathing room
        const int startY    = 30;
        const int textX     = 20;
        const int barHeight = 3;
        const int barWidth  = 28;                 // short accent bar, not a full-width block

        tft.setTextSize(2);

        for (size_t i = 0; i < items.size(); i++) {
            int y = startY + i * rowHeight;
            bool selected = ((int)i == menu.getSelectedIndex());

            tft.setTextColor(selected ? COLOR_TEXT : COLOR_TEXT_DIM);
            tft.setCursor(textX, y);
            tft.print(items[i].label.c_str());

            if (selected) {
                // Thin accent underline instead of a filled selection box
                int textH = tft.fontHeight();
                int barY  = y + textH + 4;
                tft.fillRect(textX, barY, barWidth, barHeight, COLOR_ACCENT);
            }
        }
    }
};