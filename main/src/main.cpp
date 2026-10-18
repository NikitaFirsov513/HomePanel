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
#include "HomeAssistant.hpp"
#include "esp_mac.h"
#include "esp_timer.h"

static constexpr EventBits_t ETH_GOT_IP_BIT = BIT0;
static const char *TAG = "MAIN";
static EventGroupHandle_t eth_event_group;
static QueueHandle_t action_queue;
static QueueHandle_t auth_queue;
static QueueHandle_t connection_queue;
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
    if (auto task = sync_task_handle.load()) xTaskNotifyGive(task);
    return true;
}

static bool enqueue_connection(const ConnectionCommand &command)
{
    if (!connection_queue || xQueueSend(connection_queue, &command, 0) != pdTRUE) return false;
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
    if (device->source != DeviceSource::Yandex || !device->available) { message = "Устройство недоступно"; return false; }
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

static bool refresh_home_assistant(const HomeAssistantConfig &config, ConnectionSnapshot &snapshot)
{
    HttpsClient http(config.address.c_str(), 512 * 1024);
    HttpsReq_t req{}; req.path = "/api/states"; req.token = config.token.c_str(); req.timeout_ms = 6000;
    const auto result = http.send(req);
    snapshot.connected = false; snapshot.devices = 0;
    if (!result.ok()) {
        if (result.http_status == 401 || result.http_status == 403)
            snprintf(snapshot.message, sizeof(snapshot.message), "Токен Home Assistant отклонён. Проверьте токен и права пользователя.");
        else if (result.transport_error == ESP_ERR_INVALID_SIZE)
            snprintf(snapshot.message, sizeof(snapshot.message), "Список Home Assistant превышает 512 КБ.");
        else snprintf(snapshot.message, sizeof(snapshot.message), "Нет связи: %s, HTTP %d. Проверьте адрес и сеть.", esp_err_to_name(result.transport_error), result.http_status);
        return false;
    }
    std::vector<Device> devices;
    if (!parseHomeAssistantStates(result.body, result.body_len, devices)) {
        snprintf(snapshot.message, sizeof(snapshot.message), "Некорректный ответ Home Assistant"); return false;
    }
    const auto count = devices.size();
    if (device_manager.replaceSource(DeviceSource::HomeAssistant, std::move(devices)) != ESP_OK) {
        snprintf(snapshot.message, sizeof(snapshot.message), "Не удалось обновить список Home Assistant"); return false;
    }
    snapshot.connected = true; snapshot.devices = static_cast<unsigned>(count);
    snprintf(snapshot.message, sizeof(snapshot.message), "%s", config.address.c_str());
    return true;
}

static bool send_home_assistant(const Device &device, const DeviceAction &action,
                                const HomeAssistantConfig &config, std::string &message)
{
    std::string path, body;
    if (!buildHomeAssistantAction(device, action, path, body)) {
        message = "Команда недоступна для этого устройства"; return false;
    }
    HttpsClient http(config.address.c_str(), 512 * 1024);
    HttpsReq_t req{}; req.path = path.c_str(); req.token = config.token.c_str();
    req.method = HTTP_METHOD_POST; req.body = body.c_str(); req.timeout_ms = 6000;
    const auto result = http.send(req);
    // Service calls are never replayed after a timeout: the device may have acted already.
    if (!result.ok()) {
        char text[160]; snprintf(text, sizeof(text), "Home Assistant: %s, HTTP %d. Проверьте состояние устройства.", esp_err_to_name(result.transport_error), result.http_status);
        message = text; return false;
    }
    message = "Команда принята Home Assistant"; return true;
}

static void device_sync_task(void *)
{
    uint8_t mac[6]{}; esp_read_mac(mac, ESP_MAC_ETH);
    char device_id[40];
    snprintf(device_id, sizeof(device_id), "home-panel-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    YandexAuth auth(CONFIG_HOME_PANEL_OAUTH_CLIENT_ID, CONFIG_HOME_PANEL_OAUTH_CLIENT_SECRET,
                    device_id, oauth_request, saveAuthSession);
    std::string saved;
    if (loadAuthSession(saved)) auth.restore(saved);
    HomeAssistantConfig ha;
    ConnectionSnapshot connection, previous_connection;
    const bool ha_storage_ok = loadHomeAssistantConfig(ha);
    connection.configured = !ha.address.empty();
    snprintf(connection.address, sizeof(connection.address), "%s", ha.address.c_str());
    if (!ha_storage_ok) snprintf(connection.message, sizeof(connection.message), "Не удалось прочитать настройки Home Assistant");
    bool published = false, connection_published = false, was_authorized = auth.authorized();
    bool was_online = false;
    uint64_t next_yandex = 0, next_ha = 0;
    AuthSnapshot previous;
    while (true) {
        const auto now = static_cast<uint64_t>(esp_timer_get_time() / 1000000);
        const bool online = network_ready();
        bool changed = false, request_yandex = false, request_ha = false;
        std::string result_message;
        bool result_error = false;
        AuthAction command;
        const bool auth_command_received = xQueueReceive(auth_queue, &command, 0) == pdTRUE;
        if (auth_command_received) {
            if (command == AuthAction::Begin) auth.begin(now);
            else if (command == AuthAction::Restart) auth.restart(now);
            else if (command == AuthAction::Cancel) {
                if (auth.authorized()) auth.logout(); else auth.cancel();
            } else auth.logout();
            next_yandex = 0;
        }
        ConnectionCommand settings{};
        if (xQueueReceive(connection_queue, &settings, 0) == pdTRUE) {
            HomeAssistantConfig updated;
            bool valid = true;
            if (settings.action == ConnectionAction::Save) {
                updated = {settings.address, settings.token};
                // Reusing a credential is allowed only for the exact same normalized origin.
                if (updated.token.empty()) {
                    updated.token = ha.token;
                    valid = normalizeHomeAssistantConfig(updated) && updated.address == ha.address;
                } else valid = normalizeHomeAssistantConfig(updated);
            }
            if (!valid) snprintf(connection.message, sizeof(connection.message), "Укажите адрес http:// или https:// и токен. При смене адреса нужен новый ввод токена.");
            else if (!saveHomeAssistantConfig(updated)) snprintf(connection.message, sizeof(connection.message), "Не удалось сохранить настройки Home Assistant");
            else {
                ha = std::move(updated); connection = {};
                connection.configured = !ha.address.empty();
                snprintf(connection.address, sizeof(connection.address), "%s", ha.address.c_str());
                snprintf(connection.message, sizeof(connection.message), "%s", connection.configured ? "Проверяем подключение..." : "Home Assistant отключён");
                device_manager.clearSource(DeviceSource::HomeAssistant); changed = true; next_ha = 0;
            }
            // Clear the worker's queue copy as soon as settings have been processed.
            settings = {}; connection_published = false;
        }
        auth.tick(now, online);
        const bool token_ok = online && auth.ensureToken(now);
        if (was_authorized && !auth.authorized()) {
            device_manager.clearSource(DeviceSource::Yandex); changed = true; next_yandex = 0;
        }
        was_authorized = auth.authorized();
        auth_ready.store(online && (token_ok || connection.configured));
        DeviceAction action{};
        const bool received = xQueueReceive(action_queue, &action, 0) == pdTRUE;
        if (received) {
            if (action.kind == ActionValue::Refresh) { request_yandex = request_ha = true; result_message = "Состояния обновлены"; }
            else {
                const auto device = device_manager.getDeviceById(action.device_id);
                if (!online) { result_error = true; result_message = "Нет сети. Проверьте Ethernet."; }
                else if (!device || !device->available) { result_error = true; result_message = "Устройство недоступно. Обновите список."; }
                else if (device->source == DeviceSource::HomeAssistant) {
                    request_ha = true;
                    if (!connection.configured || !connection.connected) { result_error = true; result_message = "Нет подключения к Home Assistant"; }
                    else result_error = !send_home_assistant(*device, action, ha, result_message);
                } else {
                    request_yandex = true;
                    int http_status = 0;
                    if (!token_ok) { result_error = true; result_message = "Нужно восстановить вход в Яндекс"; }
                    else {
                        result_error = !send_action(action, auth.accessToken(), result_message, http_status);
                        if (http_status == 401) auth.ensureToken(now, true);
                    }
                }
            }
        }
        if (online && auth.authorized() && token_ok && (request_yandex || now >= next_yandex)) {
            std::string message; int http_status = 0;
            bool refreshed = refresh_devices(auth.accessToken(), message, http_status);
            if (!refreshed && http_status == 401 && auth.ensureToken(now, true))
                refreshed = refresh_devices(auth.accessToken(), message, http_status);
            changed |= refreshed;
            if (!refreshed) {
                if (http_status == 401 || http_status == 403) { device_manager.clearSource(DeviceSource::Yandex); changed = true; }
                if (request_yandex) { result_message += ". " + message; result_error = true; }
                else if (!received) { result_message = message; result_error = true; changed = true; }
            }
            next_yandex = now + (refreshed ? CONFIG_HOME_PANEL_REFRESH_SECONDS : CONFIG_HOME_PANEL_RETRY_SECONDS);
        } else if (request_yandex && auth.authorized() && !token_ok) {
            result_message += ". Яндекс недоступен"; result_error = true;
        }
        if (online && connection.configured && (request_ha || now >= next_ha)) {
            const bool refreshed = refresh_home_assistant(ha, connection);
            if (!refreshed) device_manager.clearSource(DeviceSource::HomeAssistant);
            changed = true;
            if (!refreshed && request_ha) { result_message += ". " + std::string(connection.message); result_error = true; }
            else if (!refreshed && !received) { result_message = connection.message; result_error = true; }
            next_ha = now + (refreshed ? 5 : CONFIG_HOME_PANEL_RETRY_SECONDS);
        }
        if (was_online != online) {
            next_yandex = next_ha = 0;
            if (!online) {
                device_manager.clearSource(DeviceSource::HomeAssistant);
                device_manager.clearSource(DeviceSource::Yandex);
                connection.connected = false; connection.devices = 0;
                if (connection.configured) snprintf(connection.message, sizeof(connection.message), "Нет сети. Проверьте Ethernet.");
                changed = true;
            }
            was_online = online;
        }
        // Invalidated tokens affect only their own source, even if renewal failed during an action.
        if (!auth.authorized() && was_authorized) {
            device_manager.clearSource(DeviceSource::Yandex); was_authorized = false; changed = true;
        }
        const auto snapshot = auth.snapshot(now);
        if (!published || auth_command_received || !same_auth(snapshot, previous)) {
            if (ui_manager.setAuthState(snapshot, &device_manager) == ESP_OK) { previous = snapshot; published = true; }
        }
        if (!connection_published || connection.configured != previous_connection.configured ||
            connection.connected != previous_connection.connected || connection.devices != previous_connection.devices ||
            strcmp(connection.message, previous_connection.message) || strcmp(connection.address, previous_connection.address)) {
            if (ui_manager.setConnectionState(connection, &device_manager) == ESP_OK) { previous_connection = connection; connection_published = true; }
        }
        if (received || changed) {
            if (!received && result_message.empty()) result_message = !online ? "Нет сети. Ожидание Ethernet..." : "Состояния устройств обновлены";
            ui_manager.setControlResult(&device_manager, result_message.c_str(), result_error || !online, received);
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
    }
}

extern "C" void app_main()
{
    ESP_LOGI(TAG, "Application starting");
    action_queue = xQueueCreate(1, sizeof(DeviceAction));
    auth_queue = xQueueCreate(4, sizeof(AuthAction));
    connection_queue = xQueueCreate(1, sizeof(ConnectionCommand));
    ESP_ERROR_CHECK(action_queue ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(auth_queue ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(connection_queue ? ESP_OK : ESP_ERR_NO_MEM);
    ui_manager.setActionHandler(enqueue_action);
    ui_manager.setAuthHandler(enqueue_auth);
    ui_manager.setConnectionHandler(enqueue_connection);
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
