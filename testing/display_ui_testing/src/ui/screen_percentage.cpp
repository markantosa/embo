#pragma once
#include "backend/percentage_logic.h"
#include <Adafruit_ILI9341.h>

class ScreenPercentage {
public:
    void render(Adafruit_ILI9341& tft, const PercentageLogic& percent) {
        int value = percent.getValue();

        const int barX = 20, barY = 100, barW = 200, barH = 30;

        // Outline (draw once conceptually, but fine to redraw here for simplicity)
        tft.drawRect(barX, barY, barW, barH, ILI9341_WHITE);

        // Clear inside, then fill according to percentage
        tft.fillRect(barX + 1, barY + 1, barW - 2, barH - 2, ILI9341_BLACK);
        int fillWidth = ((barW - 2) * value) / 100;
        tft.fillRect(barX + 1, barY + 1, fillWidth, barH - 2, ILI9341_GREEN);

        // Text readout above the bar
        tft.fillRect(barX, barY - 25, 100, 20, ILI9341_BLACK); // clear old text
        tft.setCursor(barX, barY - 25);
        tft.setTextColor(ILI9341_WHITE);
        tft.setTextSize(2);
        tft.print(value);
        tft.print("%");
    }
};