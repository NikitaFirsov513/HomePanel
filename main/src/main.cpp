#include <cstdio>
#include <cstdlib>
#include <memory>
#include <cstring>
#include <atomic>
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
#include "YandexAuth.hpp"
#include "AuthStorage.hpp"
#include "esp_mac.h"
#include "esp_timer.h"

static constexpr EventBits_t ETH_GOT_IP_BIT = BIT0;
static const char *TAG = "MAIN";
static EventGroupHandle_t eth_event_group;
static QueueHandle_t action_queue;
static QueueHandle_t auth_queue;
static std::atomic<bool> auth_ready{false};
static std::atomic<TaskHandle_t> sync_task_handle{nullptr};
static UiManager ui_manager;
static DeviceManager device_manager;

static bool network_ready()
{
    return (xEventGroupGetBits(eth_event_group) & ETH_GOT_IP_BIT) != 0;
}

static bool enqueue_action(const DeviceAction &action)
{
    // Called by LVGL: no HTTP, waiting, or model mutexes on the display thread.
    if (!action_queue || !eth_event_group || !network_ready() || !auth_ready.load() ||
        xQueueSend(action_queue, &action, 0) != pdTRUE) return false;
    if (auto task = sync_task_handle.load()) xTaskNotifyGive(task);
    return true;
}

static bool enqueue_auth(AuthAction action)
{
    if (!auth_queue || xQueueSend(auth_queue, &action, 0) != pdTRUE) return false;
    if (action == AuthAction::Logout || action == AuthAction::Restart) auth_ready.store(false);
    if (auto task = sync_task_handle.load()) xTaskNotifyGive(task);
    return true;
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

static bool refresh_devices(const std::string &token, std::string &message, int &status)
{
    HttpsClient https("https://api.iot.yandex.net");
    HttpsReq_t req{};
    req.path = "/v1.0/user/info";
    req.token = token.c_str();
    const auto res = https.send(req);
    status = res.http_status;
    if (!res.ok()) {
        message = request_error(res.transport_error, res.http_status, false);
        return false;
    }
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
        cJSON_ParseWithLength(res.body, res.body_len), &cJSON_Delete);
    const auto *api_status = cJSON_GetObjectItemCaseSensitive(root.get(), "status");
    if (!cJSON_IsString(api_status) || strcmp(api_status->valuestring, "ok")) {
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

static bool send_action(const DeviceAction &action, const std::string &token, std::string &message, int &status)
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
    req.token = token.c_str();
    req.body = body.c_str();
    const auto res = https.send(req);
    status = res.http_status;
    if (!res.ok()) {
        message = request_error(res.transport_error, res.http_status, true);
        return false;
    }
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
        cJSON_ParseWithLength(res.body, res.body_len), &cJSON_Delete);
    return deviceActionSucceeded(root.get(), action, message);
}

static OAuthReply oauth_request(const char *path, const std::string &body)
{
    if (!network_ready()) return {false, 0, {}, "ETHERNET_NOT_READY"};
    HttpsClient https("https://oauth.yandex.ru");
    HttpsReq_t req{}; req.path = path; req.method = HTTP_METHOD_POST;
    req.body = body.c_str(); req.content_type = "application/x-www-form-urlencoded";
    const auto reply = https.send(req);
    if (reply.transport_error != ESP_OK || reply.http_status >= 500)
        ESP_LOGW(TAG, "OAuth %s: %s, HTTP %d", path, esp_err_to_name(reply.transport_error), reply.http_status);
    return {reply.transport_error == ESP_OK, reply.http_status,
            reply.body ? std::string(reply.body, reply.body_len) : std::string{},
            reply.transport_error != ESP_OK ? esp_err_to_name(reply.transport_error) : ""};
}

static bool same_auth(const AuthSnapshot &a, const AuthSnapshot &b)
{
    return a.state == b.state && a.authorized == b.authorized && a.seconds_left == b.seconds_left &&
           !strcmp(a.user_code, b.user_code) && !strcmp(a.verification_url, b.verification_url) && !strcmp(a.message, b.message);
}

static void device_sync_task(void *)
{
    uint8_t mac[6]{}; esp_read_mac(mac, ESP_MAC_ETH);
    char device_id[40];
    snprintf(device_id, sizeof(device_id), "home-panel-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    YandexAuth auth(CONFIG_HOME_PANEL_OAUTH_CLIENT_ID, CONFIG_HOME_PANEL_OAUTH_CLIENT_SECRET,
                    device_id, oauth_request, saveAuthSession);
    std::string saved;
    const bool storage_ok = loadAuthSession(saved);
    if (storage_ok) auth.restore(saved);
    bool cached_devices = false, published = false;
    uint64_t next_sync = 0;
    AuthSnapshot previous;
    std::string last_status;
    auto status = [&](const std::string &text, bool error, bool finished = false) {
        if (text != last_status || finished) {
            ui_manager.setControlResult(&device_manager, text.c_str(), error, finished);
            last_status = text;
        }
    };
    while (true) {
        const auto now = static_cast<uint64_t>(esp_timer_get_time() / 1000000);
        AuthAction command;
        const bool auth_command_received = xQueueReceive(auth_queue, &command, 0) == pdTRUE;
        if (auth_command_received) {
            if (command == AuthAction::Begin) auth.begin(now);
            else if (command == AuthAction::Restart) {
                auth.restart(now);
                xQueueReset(action_queue); next_sync = 0;
            }
            else if (command == AuthAction::Cancel) {
                // A confirmation may have finished while Cancel was queued.
                if (auth.authorized()) auth.logout(); else auth.cancel();
            }
            else if (auth.logout()) { xQueueReset(action_queue); next_sync = 0; }
        }
        auth.tick(now, network_ready());
        const bool token_ok = network_ready() && auth.ensureToken(now);
        auth_ready.store(token_ok);
        DeviceAction action{};
        const bool received = xQueueReceive(action_queue, &action, 0) == pdTRUE;
        if (!auth.authorized()) {
            // Drop the previous account's cloud snapshot on logout/expiry.
            if (cached_devices && device_manager.clearDevices() == ESP_OK) {
                cached_devices = false;
                ui_manager.setControlResult(&device_manager, "Аккаунт отключён", false);
            }
            status(storage_ok ? "Для подключения устройств откройте раздел Яндекс и войдите по QR" :
                                "Не удалось прочитать хранилище входа. Откройте раздел Яндекс.", !storage_ok, received);
            next_sync = 0;
        } else if (!network_ready()) {
            status("Нет сети. Ожидание Ethernet...", true, received); next_sync = 0;
        } else if (!token_ok) {
            status(auth.snapshot(now).message, true, received); next_sync = 0;
        } else if (received || now >= next_sync) {
            std::string action_message;
            bool action_ok = true;
            int http_status = 0;
            if (received && action.kind != ActionValue::Refresh) {
                // No automatic replay of device commands, even after HTTP 401.
                action_ok = send_action(action, auth.accessToken(), action_message, http_status);
                if (http_status == 401) auth.ensureToken(now, true);
            }
            std::string refresh_message;
            bool refreshed = false;
            if (auth.authorized() && auth.ensureToken(now)) {
                refreshed = refresh_devices(auth.accessToken(), refresh_message, http_status);
                // GET is idempotent; retry it once after renewing a rejected token.
                if (!refreshed && http_status == 401 && auth.ensureToken(now, true))
                    refreshed = refresh_devices(auth.accessToken(), refresh_message, http_status);
            } else refresh_message = "Нужно восстановить вход в Яндекс";
            if (refreshed) {
                const auto message = received && action.kind != ActionValue::Refresh ? action_message : "Состояния обновлены";
                // Always deliver a successful snapshot, even when status text repeats.
                ui_manager.setControlResult(&device_manager, message.c_str(), !action_ok, received);
                last_status = message; cached_devices = true;
                next_sync = now + CONFIG_HOME_PANEL_REFRESH_SECONDS;
            } else {
                if (received && action.kind != ActionValue::Refresh) refresh_message = action_message + ". " + refresh_message;
                status(refresh_message, true, received);
                next_sync = now + CONFIG_HOME_PANEL_RETRY_SECONDS;
            }
        }
        const auto snapshot = auth.snapshot(now);
        if (!published || auth_command_received || !same_auth(snapshot, previous)) {
            if (ui_manager.setAuthState(snapshot, &device_manager) == ESP_OK) { previous = snapshot; published = true; }
        }
        if (!auth.authorized()) auth_ready.store(false);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
    }
}

extern "C" void app_main()
{
    ESP_LOGI(TAG, "Application starting");
    action_queue = xQueueCreate(1, sizeof(DeviceAction));
    auth_queue = xQueueCreate(4, sizeof(AuthAction));
    ESP_ERROR_CHECK(action_queue ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(auth_queue ? ESP_OK : ESP_ERR_NO_MEM);
    ui_manager.setActionHandler(enqueue_action);
    ui_manager.setAuthHandler(enqueue_auth);
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
    TaskHandle_t sync_handle = nullptr;
    ESP_ERROR_CHECK(xTaskCreate(device_sync_task, "device_sync", 16 * 1024, nullptr,
                                5, &sync_handle) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    sync_task_handle.store(sync_handle);
}
