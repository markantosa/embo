#pragma once
#include "backend/menu_logic.h"
#include "hal/LGFX_Config.h"   // was: #include <Adafruit_ILI9341.h>

class ScreenMenu {
public:
    void render(LGFX& tft, const MenuLogic& menu) {   // was: Adafruit_ILI9341& tft
        tft.fillScreen(TFT_WHITE);                    // was: ILI9341_BLACK
        const auto& items = menu.getItems();

        for (size_t i = 0; i < items.size(); i++) {
            int y = 20 + i * 30;
            if ((int)i == menu.getSelectedIndex()) {
                tft.fillRect(10, y - 4, 300, 24, 0x64dc); // 0x64dc is the blue colour
                tft.setTextColor(TFT_BLACK);           // black 
            } else {
                tft.setTextColor(TFT_BLACK);
            }
            tft.setCursor(15, y);
            tft.print(items[i].label.c_str());
        }
    }
};