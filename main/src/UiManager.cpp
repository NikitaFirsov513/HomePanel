#include "UiManager.hpp"
#include <cstdio>
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

static const char *TAG = "UiManager";

esp_err_t UiManager::init()
{
    if (ui_queue_) return ESP_ERR_INVALID_STATE;
    ui_queue_ = xQueueCreate(UI_QUEUE_LENGTH, sizeof(UiMessage));
    if (!ui_queue_) return ESP_ERR_NO_MEM;
    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
        .touch_flags = {.swap_xy = 0, .mirror_x = 1, .mirror_y = 1}
    };
    // The BSP adapter owns the render buffers and tear-avoid configuration.
    if (!bsp_display_start_with_config(&cfg)) {
        vQueueDelete(ui_queue_);
        ui_queue_ = nullptr;
        return ESP_FAIL;
    }
    ESP_ERROR_CHECK(bsp_display_lock(-1));
    loading_page_.create();
    lv_screen_load(loading_page_.root());
    bsp_display_unlock();
    bsp_display_backlight_on();
    if (xTaskCreate(ui_task_entry, "ui_task", UI_TASK_STACK, this,
                    UI_TASK_PRIORITY, nullptr) != pdPASS) {
        vQueueDelete(ui_queue_);
        ui_queue_ = nullptr;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "UI initialized");
    return ESP_OK;
}

void UiManager::ui_task_entry(void *arg)
{
    static_cast<UiManager *>(arg)->ui_task(nullptr);
}

void UiManager::ui_task(void *)
{
    UiMessage msg{};
    while (true) {
        if (xQueueReceive(ui_queue_, &msg, portMAX_DELAY) == pdTRUE)
            ui_process_message(msg);
    }
}

void UiManager::ui_process_message(const UiMessage &msg)
{
    // Snapshot before locking LVGL, so model contention cannot freeze rendering.
    std::vector<Device> devices;
    if (msg.type == UiMessageType::SET_HOME_PAGE) {
        if (!msg.device_manager || msg.device_manager->getDevices(devices) != ESP_OK) {
            ESP_LOGW(TAG, "Cannot obtain device snapshot");
            return;
        }
    }
    ESP_ERROR_CHECK(bsp_display_lock(-1));
    switch (msg.type) {
    case UiMessageType::SET_LOAD_PAGE:
    case UiMessageType::SET_ERROR_PAGE:
        loading_page_.setText(msg.text, msg.type == UiMessageType::SET_LOAD_PAGE);
        lv_screen_load(loading_page_.root());
        ESP_LOGI(TAG, "Status screen: %s", msg.text);
        break;
    case UiMessageType::SET_HOME_PAGE:
        home_page_.update(devices);
        lv_screen_load(home_page_.root());
        break;
    }
    bsp_display_unlock();
}

esp_err_t UiManager::setLoadPage(const char *text)
{
    UiMessage msg{};
    msg.type = UiMessageType::SET_LOAD_PAGE;
    snprintf(msg.text, sizeof(msg.text), "%s", text ? text : "Loading...");
    return ui_send(msg) ? ESP_OK : ESP_FAIL;
}

esp_err_t UiManager::setHomePage(DeviceManager *device_manager)
{
    if (!device_manager) return ESP_ERR_INVALID_ARG;
    UiMessage msg{};
    msg.type = UiMessageType::SET_HOME_PAGE;
    msg.device_manager = device_manager;
    return ui_send(msg) ? ESP_OK : ESP_FAIL;
}

esp_err_t UiManager::setErrorPage(const char *text)
{
    UiMessage msg{};
    msg.type = UiMessageType::SET_ERROR_PAGE;
    snprintf(msg.text, sizeof(msg.text), "%s", text ? text : "An error occurred");
    return ui_send(msg) ? ESP_OK : ESP_FAIL;
}

esp_err_t UiManager::updateMainPage(DeviceManager *device_manager)
{
    return setHomePage(device_manager);
}

bool UiManager::ui_send(const UiMessage &msg)
{
    return ui_queue_ && xQueueSend(ui_queue_, &msg, pdMS_TO_TICKS(100)) == pdTRUE;
}
