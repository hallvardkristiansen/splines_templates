/*!
 * @file lgfx_config.h
 *
 * LovyanGFX configuration for the Splines module hardware.
 *
 * Display: Waveshare 1.83" ST7789P (REV2)
 *   Physical resolution : 284 × 240 (landscape)
 *   Controller resolution: 320 × 240  ← the controller is wider than the glass
 *   Rotation             : 3  (90° CCW, USB connector on left)
 *   X centering          : handled by sprite push X offset (+18 top/bottom, +28 middle)
 *                          NOT by a panel offset — panel_width/height are pre-rotation.
 *
 * SPI bus: SPI3_HOST at 80 MHz with DMA (dedicated to LCD, NOT shared).
 */

#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "pinConfig.h"

class LGFX_Splines : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789  _panel_instance;
    lgfx::Bus_SPI       _bus_instance;
    lgfx::Light_PWM     _light_instance;

public:
    LGFX_Splines(void) {
        // ── SPI Bus ──────────────────────────────────────────────
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host   = SPI3_HOST;
            cfg.spi_mode   = 0;
            cfg.freq_write = 80000000;      // 80 MHz (validated stable)
            cfg.freq_read  = 16000000;
            cfg.spi_3wire  = false;
            cfg.use_lock   = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk   = LCD_SCK_PIN;   // GPIO 12
            cfg.pin_mosi   = LCD_MOSI_PIN;  // GPIO 11
            cfg.pin_miso   = -1;            // LCD has no MISO
            cfg.pin_dc     = LCD_DC_PIN;    // GPIO 13
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        // ── Panel ────────────────────────────────────────────────
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs        = LCD_CS_PIN;   // GPIO 10
            cfg.pin_rst       = LCD_RST_PIN;  // GPIO 21
            cfg.pin_busy      = -1;
            // Pre-rotation dimensions (controller is 320 wide × 240 tall)
            cfg.panel_width   = 240;
            cfg.panel_height  = 320;
            cfg.offset_x      = 0;
            cfg.offset_y      = 0;
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable      = false;
            cfg.invert        = true;   // ST7789P requires inversion
            cfg.rgb_order     = false;  // RGB (not BGR)
            cfg.dlen_16bit    = false;
            cfg.bus_shared    = false;  // Dedicated bus
            _panel_instance.config(cfg);
        }

        // ── Backlight (LEDC channel 0) ───────────────────────────
        // WARNING: channel 0 is taken. Use LEDC_CHANNEL_1+ for any other PWM output.
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl     = LCD_BL_PIN;  // GPIO 14
            cfg.invert     = false;
            cfg.freq       = 44100;
            cfg.pwm_channel = 0;          // LEDC channel 0
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        setPanel(&_panel_instance);
    }
};

extern LGFX_Splines display;
