#pragma once

// The BSP's EK79007 timing preset predates the IDF 6.1 DPI configuration API.
// Include it first, then override only the preset and factory used by this board.
#include "esp_lcd_ek79007.h"
#include "esp_lcd_panel_ops.h"

#define LCD_COLOR_PIXEL_FORMAT_RGB565 LCD_COLOR_FMT_RGB565
#define LCD_COLOR_PIXEL_FORMAT_RGB888 LCD_COLOR_FMT_RGB888
#define ESP_LCD_COLOR_SPACE_RGB LCD_RGB_ELEMENT_ORDER_RGB

#undef EK79007_1024_600_PANEL_60HZ_CONFIG
#define EK79007_1024_600_PANEL_60HZ_CONFIG(format) { \
    .virtual_channel = 0, \
    .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT, \
    .dpi_clock_freq_mhz = 52, \
    .in_color_format = (format), \
    .out_color_format = (format), \
    .num_fbs = 1, \
    .video_timing = { \
        .h_size = 1024, .v_size = 600, \
        .hsync_pulse_width = 10, .hsync_back_porch = 160, .hsync_front_porch = 160, \
        .vsync_pulse_width = 1, .vsync_back_porch = 23, .vsync_front_porch = 12, \
    }, \
}

static inline esp_err_t home_panel_new_ek79007(esp_lcd_panel_io_handle_t io,
    const esp_lcd_panel_dev_config_t *config, esp_lcd_panel_handle_t *panel)
{
    esp_err_t err = esp_lcd_new_panel_ek79007(io, config, panel);
    if (err != ESP_OK) return err;
    // This replaces the removed flags.use_dma2d setting in the old preset.
    err = esp_lcd_dpi_panel_enable_dma2d(*panel);
    if (err != ESP_OK) {
        esp_lcd_panel_del(*panel);
        *panel = NULL;
    }
    return err;
}

#define esp_lcd_new_panel_ek79007 home_panel_new_ek79007
