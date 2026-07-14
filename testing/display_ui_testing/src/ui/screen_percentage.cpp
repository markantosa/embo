#pragma once
#include "backend/percentage_logic.h"
#include "hal/LGFX_Config.h"    // was: #include <Adafruit_ILI9341.h>

class ScreenPercentage {
public:
    void render(LGFX& tft, const PercentageLogic& percent) {   // was: Adafruit_ILI9341&
        int value = percent.getValue();

        const int barX = 20, barY = 150, barW = 270, barH = 30;
        const int innerX = barX + 1, innerY = barY + 1, innerW = barW - 2, innerH = barH - 2;

        int newFillWidth = (innerW * value) / 100;

        if (!_initialized) {
            tft.drawRect(barX, barY, barW, barH, TFT_WHITE);       // was: ILI9341_WHITE
            tft.fillRect(innerX, innerY, innerW, innerH, TFT_WHITE);
            _prevFillWidth = 0;
            _initialized = true;
        }

        if (newFillWidth > _prevFillWidth) {
            tft.fillRect(innerX + _prevFillWidth, innerY,
                         newFillWidth - _prevFillWidth, innerH, TFT_GREEN);   // was: ILI9341_GREEN
        } else if (newFillWidth < _prevFillWidth) {
            tft.fillRect(innerX + newFillWidth, innerY,
                         _prevFillWidth - newFillWidth, innerH, TFT_WHITE);
        }
        _prevFillWidth = newFillWidth;

        if (value != _prevValue) {
            tft.fillRect(barX, barY - 25, 100, 20, TFT_WHITE);
            tft.setCursor(barX, barY - 25);
            tft.setTextColor(TFT_WHITE);
            tft.setTextSize(2);
            tft.print(value);
            tft.print("%");
            _prevValue = value;
        }
    }

private:
    bool _initialized = false;
    int _prevFillWidth = 0;
    int _prevValue = -1;
};