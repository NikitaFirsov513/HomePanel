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
    home_page_.update({});
    home_page_.setStatus("Подготавливаем подключения...", false, false);
    home_page_.setDeviceLoadState(DeviceLoadState::Starting);
    loading_page_.create(); loading_page_.setText("Подготавливаем панель");
    lv_screen_load(loading_page_.root());
    auto *boot_timer = lv_timer_create([](lv_timer_t *timer) {
        auto *self = static_cast<UiManager *>(lv_timer_get_user_data(timer));
        if (lv_screen_active() == self->loading_page_.root()) lv_screen_load(self->home_page_.root());
        self->loading_page_.stop();
    }, 1100, this);
    lv_timer_set_repeat_count(boot_timer, 1);
#if CONFIG_HOME_PANEL_LOG_FPS
    if (esp_lv_adapter_fps_stats_enable(nullptr, true) == ESP_OK) {
        lv_timer_create([](lv_timer_t *) {
            uint32_t fps = 0;
            if (esp_lv_adapter_get_fps(nullptr, &fps) == ESP_OK)
                ESP_LOGI(TAG, "Display FPS: %lu (measure while moving the UI)", static_cast<unsigned long>(fps));
        }, 5000, nullptr);
    }
#endif
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
    bool snapshot_available = true;
    if (msg.type == UiMessageType::SET_HOME_PAGE || msg.type == UiMessageType::CONTROL_RESULT ||
        msg.type == UiMessageType::AUTH_STATE || msg.type == UiMessageType::CONNECTION_STATE) {
        if (!msg.device_manager || msg.device_manager->getDevices(devices) != ESP_OK) {
            ESP_LOGW(TAG, "Cannot obtain device snapshot");
            if (msg.type == UiMessageType::SET_HOME_PAGE) return;
            snapshot_available = false;
        }
    }
    ESP_ERROR_CHECK(bsp_display_lock(-1));
    switch (msg.type) {
    case UiMessageType::DEVICE_LOAD_STATE:
        home_page_.setDeviceLoadState(msg.device_load);
        break;
    case UiMessageType::NETWORK_STATE:
        home_page_.updateNetwork(msg.network);
        break;
    case UiMessageType::AUTH_STATE:
        home_page_.updateAuth(msg.auth);
        if (snapshot_available) home_page_.update(devices);
        break;
    case UiMessageType::CONNECTION_STATE:
        home_page_.updateConnection(msg.connection);
        if (snapshot_available) home_page_.update(devices);
        break;
    case UiMessageType::SET_LOAD_PAGE:
    case UiMessageType::SET_ERROR_PAGE:
        // The local floor-plan editor remains accessible while offline.
        home_page_.setStatus(msg.text, msg.type == UiMessageType::SET_ERROR_PAGE, false);
        ESP_LOGI(TAG, "Status screen: %s", msg.text);
        break;
    case UiMessageType::SET_HOME_PAGE:
        home_page_.update(devices);
        if (lv_screen_active() != loading_page_.root()) lv_screen_load(home_page_.root());
        break;
    case UiMessageType::CONTROL_RESULT:
        // A periodic refresh must not release controls belonging to an action
        // queued while that refresh was in flight.
        if (!msg.finished && home_page_.busy()) break;
        home_page_.setStatus(msg.text, msg.error);
        if (snapshot_available) home_page_.update(devices);
        if (lv_screen_active() != home_page_.root() && lv_screen_active() != loading_page_.root()) lv_screen_load(home_page_.root());
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

esp_err_t UiManager::setControlResult(DeviceManager *devices, const char *text, bool error, bool finished)
{
    UiMessage msg{};
    msg.type = UiMessageType::CONTROL_RESULT;
    msg.device_manager = devices;
    msg.error = error;
    msg.finished = finished;
    snprintf(msg.text, sizeof(msg.text), "%s", text ? text : "");
    return ui_send(msg) ? ESP_OK : ESP_FAIL;
}

esp_err_t UiManager::updateMainPage(DeviceManager *device_manager)
{
    return setHomePage(device_manager);
}

esp_err_t UiManager::setAuthState(const AuthSnapshot &snapshot, DeviceManager *devices)
{
    UiMessage msg{}; msg.type = UiMessageType::AUTH_STATE; msg.auth = snapshot;
    msg.device_manager = devices;
    return ui_send(msg) ? ESP_OK : ESP_FAIL;
}

bool UiManager::ui_send(const UiMessage &msg)
{
    return ui_queue_ && xQueueSend(ui_queue_, &msg, pdMS_TO_TICKS(100)) == pdTRUE;
}

esp_err_t UiManager::setNetworkState(const NetworkSnapshot &snapshot)
{
    UiMessage msg{}; msg.type = UiMessageType::NETWORK_STATE; msg.network = snapshot;
    return ui_send(msg) ? ESP_OK : ESP_FAIL;
}

esp_err_t UiManager::setConnectionState(const ConnectionSnapshot &snapshot, DeviceManager *devices)
{
    UiMessage msg{}; msg.type = UiMessageType::CONNECTION_STATE; msg.connection = snapshot;
    msg.device_manager = devices;
    return ui_send(msg) ? ESP_OK : ESP_FAIL;
}

esp_err_t UiManager::setDeviceLoadState(DeviceLoadState state) {
    UiMessage msg{}; msg.type = UiMessageType::DEVICE_LOAD_STATE; msg.device_load = state;
    return ui_send(msg) ? ESP_OK : ESP_FAIL;
}
