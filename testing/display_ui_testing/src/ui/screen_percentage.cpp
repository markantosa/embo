#pragma once
#include "backend/percentage_logic.h"
#include <Adafruit_ILI9341.h>

class ScreenPercentage {
public:
    void render(Adafruit_ILI9341& tft, const PercentageLogic& percent) {
        int value = percent.getValue();

        const int barX = 20, barY = 150, barW = 270, barH = 30;
        const int innerX = barX + 1, innerY = barY + 1, innerW = barW - 2, innerH = barH - 2;

        int newFillWidth = (innerW * value) / 100;

        if (!_initialized) {
            // Draw static parts once
            tft.drawRect(barX, barY, barW, barH, ILI9341_WHITE);
            tft.fillRect(innerX, innerY, innerW, innerH, ILI9341_WHITE);
            _prevFillWidth = 0;
            _initialized = true;
        }

        if (newFillWidth > _prevFillWidth) {
            // Growing — only draw the new strip
            tft.fillRect(innerX + _prevFillWidth, innerY,
                         newFillWidth - _prevFillWidth, innerH, ILI9341_GREEN);
        } else if (newFillWidth < _prevFillWidth) {
            // Shrinking — only erase the removed strip
            tft.fillRect(innerX + newFillWidth, innerY,
                         _prevFillWidth - newFillWidth, innerH, ILI9341_WHITE);
        }
        _prevFillWidth = newFillWidth;

        // --REDRAWING TEXT-- Only redraw text if the displayed number actually changed
        if (value != _prevValue) {
            tft.fillRect(barX, barY - 25, 100, 20, ILI9341_WHITE);
            tft.setCursor(barX, barY - 25);
            tft.setTextColor(ILI9341_WHITE);
            tft.setTextSize(2);
            tft.print(value);
            tft.print("%");
            _prevValue = value;
        }
    }

private:
    bool _initialized = false;
    int _prevFillWidth = 0;
    int _prevValue = -1;   // force first draw
};