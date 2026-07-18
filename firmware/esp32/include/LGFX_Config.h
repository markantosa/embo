#pragma once
#include <LovyanGFX.hpp>
#include "config.h"

// LovyanGFX panel definition for the UI breakout's ILI9341, validated on the
// bench as the most responsive of the display libraries trialed (see
// testing/display_ui_testing) — replaces the old TFT_eSPI setup.
//
// No MISO/readback: v3.3 moved the touch chip's data-out (T_DO) off the old
// shared SPI_MISO pin onto GPIO46 (see config.h), and this UI doesn't use
// touch at all (encoder + one button only, see ui.cpp) — so the display bus
// here is write-only.
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
            cfg.pin_miso    = -1;          // write-only bus, see class comment above
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
            cfg.readable         = false;  // no MISO, see class comment above
            cfg.invert           = false;
            cfg.rgb_order        = false;
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = false;
            _panel_instance.config(cfg);
        }
        setPanel(&_panel_instance);
    }
};
