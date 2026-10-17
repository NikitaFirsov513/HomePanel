#include <cstdio>
#include <cstdlib>
#include <memory>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_eth.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "cJSON.h"
#include "ethernet_init.h"
#include "HttpsClient.hpp"
#include "UiManager.hpp"
#include "DeviceManager.hpp"

static constexpr EventBits_t ETH_GOT_IP_BIT = BIT0;
static const char *TAG = "MAIN";
static EventGroupHandle_t eth_event_group;
static UiManager ui_manager;
static DeviceManager device_manager;

static bool network_ready()
{
    return (xEventGroupGetBits(eth_event_group) & ETH_GOT_IP_BIT) != 0;
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

// Notice disconnects without waiting out the whole refresh interval.
static void wait_connected(unsigned seconds)
{
    for (unsigned i = 0; i < seconds && network_ready(); ++i)
        vTaskDelay(pdMS_TO_TICKS(1000));
}

static void device_sync_task(void *)
{
    if (CONFIG_HOME_PANEL_YANDEX_TOKEN[0] == '\0') {
        ESP_LOGE(TAG, "Set the Yandex token in menuconfig > Home Panel");
        ui_manager.setLoadPage("Set Yandex token in menuconfig > Home Panel");
        vTaskDelete(nullptr);
        return;
    }
    while (true) {
        if (!network_ready()) {
            ui_manager.setLoadPage("Waiting for Ethernet IP...");
            EventBits_t bits = xEventGroupWaitBits(eth_event_group, ETH_GOT_IP_BIT,
                                                  pdFALSE, pdTRUE, pdMS_TO_TICKS(15000));
            if (!(bits & ETH_GOT_IP_BIT)) {
                ui_manager.setLoadPage("No network. Check Ethernet cable / DHCP. Retrying...");
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
        }
        esp_err_t error = ESP_OK;
        int http_status = 0;
        {
            // A new client also recovers from failed TLS connections.
            HttpsClient https("https://api.iot.yandex.net");
            HttpsReq_t req{};
            req.method = HTTP_METHOD_GET;
            req.path = "/v1.0/user/info";
            req.token = CONFIG_HOME_PANEL_YANDEX_TOKEN;
            HttpsRes_t res = https.send(req);
            http_status = res.http_status;
            error = res.transport_error;
            if (res.ok()) {
                std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
                    cJSON_ParseWithLength(res.body, res.body_len), &cJSON_Delete);
                error = device_manager.createDevicesFromJson(root.get());
            } else if (error == ESP_OK) {
                error = ESP_FAIL;
            }
        }
        if (error == ESP_OK && network_ready()) {
            error = ui_manager.setHomePage(&device_manager);
            if (error == ESP_OK) {
                ESP_LOGI(TAG, "Device list updated");
                wait_connected(CONFIG_HOME_PANEL_REFRESH_SECONDS);
                continue;
            }
        }
        char message[128];
        if (!network_ready()) {
            snprintf(message, sizeof(message), "Network lost. Reconnecting...");
        } else if (http_status == 401 || http_status == 403) {
            snprintf(message, sizeof(message), "Authorization error (HTTP %d). Check Yandex token.", http_status);
        } else {
            snprintf(message, sizeof(message), "Update failed: %s, HTTP %d. Retrying...",
                     esp_err_to_name(error), http_status);
        }
        ESP_LOGW(TAG, "%s", message);
        ui_manager.setLoadPage(message);
        wait_connected(CONFIG_HOME_PANEL_RETRY_SECONDS);
    }
}

extern "C" void app_main()
{
    ESP_LOGI(TAG, "Application starting");
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
    // Only free the array; driver ownership remains with the network interface.
    free(handles);
    ESP_ERROR_CHECK(xTaskCreate(device_sync_task, "device_sync", 16 * 1024, nullptr,
                                5, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
