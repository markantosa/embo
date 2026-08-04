#pragma once
#include <LovyanGFX.hpp>
#include "config.h"

// LovyanGFX panel definition for the UI breakout's ILI9341, validated on the
// bench as the most responsive of the display libraries trialed (see
// testing/display_ui_testing) — replaces the old TFT_eSPI setup.
//
// Touch (XPT2046) — REVERTED. A bus_shared=true Touch_XPT2046 config was
// added here (see git history) to support on-screen touch buttons, but a
// board hung at boot (stopped mid-setup(), buzzer stuck on) immediately
// after that change went in — consistent with tft.init() itself never
// returning, since that's what brings the touch bus up. bus_shared=true is
// LovyanGFX's own supported way to share a bus between a panel and touch,
// and should in principle avoid the same class of problem as the AD9833's
// SPI change (see uas.cpp) — but "should in principle" isn't verified on
// this exact board, and the symptom lines up too well to leave it in
// place. Reverting to a write-only display bus (no touch) pending real
// hardware debugging with a scope on GPIO12/46 during boot.
//
// ui_input_poll_touch_tap() and ui_display_draw_touch_button() (see
// ui_input.h/ui_display.h) are left in place — LGFX::getTouch() safely
// returns false with no touch panel attached, so every screen's touch
// button is simply unreachable right now rather than broken; each screen
// still needs an encoder-based way to do whatever that button did, since
// this makes those buttons currently non-functional. See TOUCH_TODO below
// for what to restore once this is debugged.
//
// TOUCH_TODO: re-add lgfx::Touch_XPT2046 _touch_instance; a Touch config
// block (pin_cs=PIN_TOUCH_CS, pin_int=-1, bus_shared=true, spi_host=SPI2_HOST,
// freq=SPI_CLK_TOUCH) and _panel_instance.setTouch(&_touch_instance); and
// pin_miso=PIN_TOUCH_DO in the bus config below — only after confirming on
// a scope that this doesn't stall tft.init().
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9341 _panel_instance;
    lgfx::Bus_SPI       _bus_instance;

public:
    LGFX(void) {
        { // SPI bus config
            auto cfg = _bus_instance.config();
            cfg.spi_host    = SPI2_HOST;   // ESP32-S3: SPI2_HOST or SPI3_HOST (avoid SPI1, used for flash)
            cfg.spi_mode    = 0;
            cfg.freq_write  = SPI_CLK_TFT;
            cfg.freq_read   = SPI_CLK_TFT;
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = PIN_SPI_CLK;
            cfg.pin_mosi    = PIN_SPI_MOSI;
            cfg.pin_miso    = -1;          // write-only bus, see TOUCH_TODO above
            cfg.pin_dc      = PIN_TFT_DC;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        { // Panel config
            auto cfg = _panel_instance.config();
            cfg.pin_cs           = PIN_TFT_CS;
            cfg.pin_rst          = PIN_TFT_RST;
            cfg.pin_busy         = -1;
            cfg.panel_width      = 240;
            cfg.panel_height     = 320;
            cfg.offset_x         = 0;
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 0;
            cfg.readable         = false;  // no MISO, see TOUCH_TODO above
            cfg.invert           = false;
            cfg.rgb_order        = false;
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = false;
            _panel_instance.config(cfg);
        }
        setPanel(&_panel_instance);
    }
};
