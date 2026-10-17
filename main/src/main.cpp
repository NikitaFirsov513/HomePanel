#include <cstdio>
#include <cstdlib>
#include <memory>
#include <cstring>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_event.h"
#include "esp_eth.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "cJSON.h"
#include "ethernet_init.h"
#include "HttpsClient.hpp"
#include "UiManager.hpp"
#include "DeviceManager.hpp"
#include "DeviceAction.hpp"

static constexpr EventBits_t ETH_GOT_IP_BIT = BIT0;
static const char *TAG = "MAIN";
static EventGroupHandle_t eth_event_group;
static QueueHandle_t action_queue;
static UiManager ui_manager;
static DeviceManager device_manager;

static bool network_ready()
{
    return (xEventGroupGetBits(eth_event_group) & ETH_GOT_IP_BIT) != 0;
}

static bool enqueue_action(const DeviceAction &action)
{
    // Called by LVGL: no HTTP, waiting, or model mutexes on the display thread.
    return action_queue && xQueueSend(action_queue, &action, 0) == pdTRUE;
}

static void eth_event_handler(void *, esp_event_base_t, int32_t event_id, void *)
{
    if (event_id == ETHERNET_EVENT_DISCONNECTED || event_id == ETHERNET_EVENT_STOP) {
        xEventGroupClearBits(eth_event_group, ETH_GOT_IP_BIT);
        ESP_LOGW(TAG, "Ethernet unavailable");
    }
}

static void ip_event_handler(void *, esp_event_base_t, int32_t event_id, void *)
{
    if (event_id == IP_EVENT_ETH_GOT_IP) {
        xEventGroupSetBits(eth_event_group, ETH_GOT_IP_BIT);
        ESP_LOGI(TAG, "Network is ready");
    } else if (event_id == IP_EVENT_ETH_LOST_IP) {
        xEventGroupClearBits(eth_event_group, ETH_GOT_IP_BIT);
    }
}

static std::string request_error(esp_err_t error, int status, bool control)
{
    if (!network_ready()) return "Нет сети. Проверьте Ethernet.";
    if (status == 401) return "HTTP 401: токен Яндекса недействителен";
    if (status == 403) return control ? "HTTP 403: токену нужно разрешение iot:control" :
                                                     "HTTP 403: токену нужно разрешение iot:view";
    char text[100];
    snprintf(text, sizeof(text), "Ошибка запроса: %s, HTTP %d", esp_err_to_name(error), status);
    return text;
}

static bool refresh_devices(std::string &message)
{
    HttpsClient https("https://api.iot.yandex.net");
    HttpsReq_t req{};
    req.path = "/v1.0/user/info";
    req.token = CONFIG_HOME_PANEL_YANDEX_TOKEN;
    const auto res = https.send(req);
    if (!res.ok()) {
        message = request_error(res.transport_error, res.http_status, false);
        return false;
    }
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
        cJSON_ParseWithLength(res.body, res.body_len), &cJSON_Delete);
    const auto *status = cJSON_GetObjectItemCaseSensitive(root.get(), "status");
    if (!cJSON_IsString(status) || strcmp(status->valuestring, "ok")) {
        message = "Некорректный ответ списка устройств";
        return false;
    }
    const auto error = device_manager.createDevicesFromJson(root.get());
    if (error != ESP_OK) {
        message = request_error(error, res.http_status, false);
        return false;
    }
    return true;
}

static bool send_action(const DeviceAction &action, std::string &message)
{
    auto device = device_manager.getDeviceById(action.device_id);
    if (!device) { message = "Устройство больше не найдено. Обновите список."; return false; }
    std::string body;
    const auto error = buildDeviceAction(*device, action, body);
    if (error != ESP_OK) { message = "Команда недоступна для этого устройства"; return false; }
    HttpsClient https("https://api.iot.yandex.net");
    HttpsReq_t req{};
    req.method = HTTP_METHOD_POST;
    req.path = "/v1.0/devices/actions";
    req.token = CONFIG_HOME_PANEL_YANDEX_TOKEN;
    req.body = body.c_str();
    const auto res = https.send(req);
    if (!res.ok()) {
        message = request_error(res.transport_error, res.http_status, true);
        return false;
    }
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
        cJSON_ParseWithLength(res.body, res.body_len), &cJSON_Delete);
    return deviceActionSucceeded(root.get(), action, message);
}

static void device_sync_task(void *)
{
    if (CONFIG_HOME_PANEL_YANDEX_TOKEN[0] == '\0') {
        ESP_LOGE(TAG, "Set the Yandex token in menuconfig > Home Panel");
        ui_manager.setErrorPage("Yandex token is missing. Set it in menuconfig > Home Panel.");
        vTaskDelete(nullptr);
        return;
    }
    bool home_shown = false;
    unsigned delay_seconds = 0;
    while (true) {
        DeviceAction action{};
        bool received = false;
        for (unsigned second = 0; second < delay_seconds && network_ready(); ++second) {
            if (xQueueReceive(action_queue, &action, pdMS_TO_TICKS(1000)) == pdTRUE) {
                received = true;
                break;
            }
        }
        // Also consume commands arriving during a refresh or a disconnect.
        if (!received) received = xQueueReceive(action_queue, &action, 0) == pdTRUE;
        if (!network_ready()) {
            if (home_shown) ui_manager.setControlResult(&device_manager, "Нет сети. Ожидание Ethernet...", true, received);
            else ui_manager.setLoadPage("Waiting for Ethernet IP...");
            xEventGroupWaitBits(eth_event_group, ETH_GOT_IP_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(1000));
            delay_seconds = 0;
            continue;
        }
        std::string action_message;
        bool action_ok = true;
        if (received && action.kind != ActionValue::Refresh) {
            // Never automatically retry an action: vacuum commands and relative
            // changes may have already executed even if the response was lost.
            action_ok = send_action(action, action_message);
            ESP_LOGI(TAG, "Device action: %s", action_ok ? "DONE" : "ERROR");
        }
        if (!home_shown) ui_manager.setLoadPage("Loading devices...");
        std::string refresh_message;
        const bool refreshed = refresh_devices(refresh_message);
        if (refreshed) {
            if (received) {
                const auto message = action.kind == ActionValue::Refresh ? "Состояния обновлены" : action_message;
                ui_manager.setControlResult(&device_manager, message.c_str(), !action_ok);
            } else if (!home_shown) ui_manager.setHomePage(&device_manager);
            else {
                ui_manager.setControlResult(&device_manager, "Состояния обновлены", false, false);
            }
            home_shown = true;
            delay_seconds = CONFIG_HOME_PANEL_REFRESH_SECONDS;
            ESP_LOGI(TAG, "Device list updated");
        } else {
            std::string message = refresh_message;
            if (received && action.kind != ActionValue::Refresh) {
                message = action_message + ". Состояние не обновлено: " + refresh_message;
            }
            if (home_shown) ui_manager.setControlResult(&device_manager, message.c_str(), true, received);
            else ui_manager.setErrorPage(message.c_str());
            delay_seconds = CONFIG_HOME_PANEL_RETRY_SECONDS;
            ESP_LOGW(TAG, "Device update failed");
        }
    }
}

extern "C" void app_main()
{
    ESP_LOGI(TAG, "Application starting");
    action_queue = xQueueCreate(1, sizeof(DeviceAction));
    ESP_ERROR_CHECK(action_queue ? ESP_OK : ESP_ERR_NO_MEM);
    ui_manager.setActionHandler(enqueue_action);
    ESP_ERROR_CHECK(ui_manager.init());
    eth_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(eth_event_group ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    uint8_t port_count = 0;
    esp_eth_handle_t *handles = nullptr;
    ESP_ERROR_CHECK(example_eth_init(&handles, &port_count));
    ESP_ERROR_CHECK(port_count == 1 && handles ? ESP_OK : ESP_ERR_INVALID_STATE);
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *netif = esp_netif_new(&cfg);
    ESP_ERROR_CHECK(netif ? ESP_OK : ESP_ERR_NO_MEM);
    esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(handles[0]);
    ESP_ERROR_CHECK(glue ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_netif_attach(netif, glue));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                             eth_event_handler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                             ip_event_handler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP,
                                             ip_event_handler, nullptr));
    ESP_ERROR_CHECK(esp_eth_start(handles[0]));
    free(handles);
    ESP_ERROR_CHECK(xTaskCreate(device_sync_task, "device_sync", 16 * 1024, nullptr,
                                5, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
