#pragma once

// Compatibility for esp_lvgl_adapter 0.1.x on IDF 6.1.
// Equal source/destination FourCC values request a DMA2D window copy.
#include "esp_async_color_convert.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef async_color_convert_handle_t esp_async_fbcpy_handle_t;
typedef async_color_convert_event_data_t esp_async_fbcpy_event_data_t;
typedef async_color_convert_isr_cb_t esp_async_fbcpy_event_callback_t;
typedef struct { unsigned reserved; } esp_async_fbcpy_config_t;

// These names are used only inside the adapter's legacy initializers.
#define COLOR_SPACE_RGB 0
#define COLOR_PIXEL_RGB565 ESP_COLOR_FOURCC_RGB16
#define COLOR_PIXEL_RGB888 ESP_COLOR_FOURCC_RGB24
#define COLOR_TYPE_ID(space, pixel) (pixel)

typedef struct {
    const void *src_buffer;
    void *dst_buffer;
    size_t src_buffer_size_x;
    size_t src_buffer_size_y;
    size_t dst_buffer_size_x;
    size_t dst_buffer_size_y;
    size_t src_offset_x;
    size_t src_offset_y;
    size_t dst_offset_x;
    size_t dst_offset_y;
    size_t copy_size_x;
    size_t copy_size_y;
    struct { esp_color_fourcc_t color_type_id; } pixel_format_unique_id;
} esp_async_fbcpy_trans_desc_t;

static inline esp_err_t esp_async_fbcpy_install(const esp_async_fbcpy_config_t *config,
                                               esp_async_fbcpy_handle_t *handle)
{
    if (!config || !handle) return ESP_ERR_INVALID_ARG;
    async_color_convert_config_t dma_config = {0};
    dma_config.dma_burst_size = 128;
    return esp_async_color_convert_install_dma2d(&dma_config, handle);
}

static inline esp_err_t esp_async_fbcpy_uninstall(esp_async_fbcpy_handle_t handle)
{
    return esp_async_color_convert_uninstall(handle);
}

static inline esp_err_t esp_async_fbcpy(esp_async_fbcpy_handle_t handle,
                                       esp_async_fbcpy_trans_desc_t *desc,
                                       esp_async_fbcpy_event_callback_t callback,
                                       void *context)
{
    if (!desc) return ESP_ERR_INVALID_ARG;
    async_color_convert_request_t request = {0};
    request.src_buffer = desc->src_buffer;
    request.dst_buffer = desc->dst_buffer;
    request.src_stride = desc->src_buffer_size_x;
    request.src_height = desc->src_buffer_size_y;
    request.dst_stride = desc->dst_buffer_size_x;
    request.dst_height = desc->dst_buffer_size_y;
    request.src_x = desc->src_offset_x;
    request.src_y = desc->src_offset_y;
    request.dst_x = desc->dst_offset_x;
    request.dst_y = desc->dst_offset_y;
    request.copy_width = desc->copy_size_x;
    request.copy_height = desc->copy_size_y;
    request.src_color_format = desc->pixel_format_unique_id.color_type_id;
    request.dst_color_format = desc->pixel_format_unique_id.color_type_id;
    // Forward the callback unchanged: completion still runs in DMA ISR context.
    return esp_async_color_convert(handle, &request, callback, context);
}

#ifdef __cplusplus
}
#endif
