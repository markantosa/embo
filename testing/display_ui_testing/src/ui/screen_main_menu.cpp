#pragma once
#include "backend/menu_logic.h"
#include <Adafruit_ILI9341.h>

class ScreenMenu {
public:
    void render(Adafruit_ILI9341& tft, const MenuLogic& menu) {
        tft.fillScreen(ILI9341_WHITE);
        const auto& items = menu.getItems();

        for (size_t i = 0; i < items.size(); i++) {
            int y = 20 + i * 30;
            if ((int)i == menu.getSelectedIndex()) {
                tft.fillRect(10, y - 4, 300, 24, 0x64dc);  // highlight bar, 0x64dc is a blue colour
                tft.setTextColor(ILI9341_BLACK);
            } else {
                tft.setTextColor(ILI9341_BLACK);
            }
            tft.setCursor(15, y);
            tft.print(items[i].label.c_str());
        }
    }
};