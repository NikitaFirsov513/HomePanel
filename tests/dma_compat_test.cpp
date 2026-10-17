#include <cstdio>
#include <cstdlib>
#include "esp_async_fbcpy.h"

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "FAILED at line %d: %s\n", __LINE__, #condition); \
    std::exit(1); } } while (false)

static async_color_convert_request_t captured;
static async_color_convert_isr_cb_t captured_callback;
static void *captured_context;
static esp_err_t driver_result = ESP_OK;
static int instance;
static const auto fake_handle = reinterpret_cast<async_color_convert_handle_t>(&instance);

extern "C" esp_err_t esp_async_color_convert_install_dma2d(
    const async_color_convert_config_t *config, async_color_convert_handle_t *handle)
{
    CHECK(config->dma_burst_size == 128);
    *handle = fake_handle;
    return driver_result;
}

extern "C" esp_err_t esp_async_color_convert_uninstall(async_color_convert_handle_t handle)
{
    CHECK(handle == fake_handle);
    return driver_result;
}

extern "C" esp_err_t esp_async_color_convert(async_color_convert_handle_t handle,
    const async_color_convert_request_t *request, async_color_convert_isr_cb_t callback, void *context)
{
    CHECK(handle == fake_handle);
    captured = *request;
    captured_callback = callback;
    captured_context = context;
    return driver_result;
}

static bool done(esp_async_fbcpy_handle_t, esp_async_fbcpy_event_data_t *, void *) { return true; }

int main()
{
    esp_async_fbcpy_config_t config{};
    esp_async_fbcpy_handle_t handle = nullptr;
    CHECK(esp_async_fbcpy_install(nullptr, &handle) == ESP_ERR_INVALID_ARG);
    CHECK(esp_async_fbcpy_install(&config, nullptr) == ESP_ERR_INVALID_ARG);
    CHECK(esp_async_fbcpy_install(&config, &handle) == ESP_OK);
    CHECK(handle == fake_handle);
    char src[1], dst[1];
    int context;
    esp_async_fbcpy_trans_desc_t desc{};
    desc.src_buffer = src;
    desc.dst_buffer = dst;
    desc.src_buffer_size_x = 100;
    desc.src_buffer_size_y = 60;
    desc.dst_buffer_size_x = 1024;
    desc.dst_buffer_size_y = 600;
    desc.src_offset_x = 1;
    desc.src_offset_y = 2;
    desc.dst_offset_x = 3;
    desc.dst_offset_y = 4;
    desc.copy_size_x = 20;
    desc.copy_size_y = 30;
    const esp_color_fourcc_t formats[] = {
        COLOR_TYPE_ID(COLOR_SPACE_RGB, COLOR_PIXEL_RGB565),
        COLOR_TYPE_ID(COLOR_SPACE_RGB, COLOR_PIXEL_RGB888)};
    for (auto format : formats) {
        desc.pixel_format_unique_id.color_type_id = format;
        CHECK(esp_async_fbcpy(handle, &desc, done, &context) == ESP_OK);
        CHECK(captured.src_buffer == src && captured.dst_buffer == dst);
        CHECK(captured.src_stride == 100 && captured.src_height == 60);
        CHECK(captured.dst_stride == 1024 && captured.dst_height == 600);
        CHECK(captured.src_x == 1 && captured.src_y == 2);
        CHECK(captured.dst_x == 3 && captured.dst_y == 4);
        CHECK(captured.copy_width == 20 && captured.copy_height == 30);
        CHECK(captured.src_color_format == format && captured.dst_color_format == format);
        CHECK(captured_callback == done && captured_context == &context);
    }
    CHECK(esp_async_fbcpy(handle, nullptr, done, nullptr) == ESP_ERR_INVALID_ARG);
    driver_result = ESP_ERR_TIMEOUT;
    CHECK(esp_async_fbcpy(handle, &desc, done, &context) == ESP_ERR_TIMEOUT);
    CHECK(esp_async_fbcpy_install(&config, &handle) == ESP_ERR_TIMEOUT);
    CHECK(esp_async_fbcpy_uninstall(handle) == ESP_ERR_TIMEOUT);
    std::puts("DMA compatibility tests passed");
}
