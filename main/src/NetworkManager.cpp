#include "NetworkManager.hpp"
#include "ethernet_init.h"
#include "esp_eth.h"
#include "esp_wifi.h"
#include "esp_hosted_event.h"
#include "esp_hosted.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>

namespace { const char *TAG = "NETWORK"; }
esp_err_t NetworkManager::init() {
    mutex_ = xSemaphoreCreateMutex(); queue_ = xQueueCreate(1, sizeof(WifiCommand));
    if (!mutex_ || !queue_) return ESP_ERR_NO_MEM;
    auto err = esp_netif_init(); if (err != ESP_OK) return err;
    err = esp_event_loop_create_default(); if (err != ESP_OK) return err;
    uint8_t count = 0; esp_eth_handle_t *handles = nullptr;
    err = example_eth_init(&handles, &count); if (err != ESP_OK) return err;
    if (count != 1 || !handles) { free(handles); return ESP_ERR_INVALID_STATE; }
    esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_ETH(); base.route_prio = 100;
    esp_netif_config_t cfg{&base, nullptr, ESP_NETIF_NETSTACK_DEFAULT_ETH};
    ethernet_ = esp_netif_new(&cfg); if (!ethernet_) { free(handles); return ESP_ERR_NO_MEM; }
    auto glue = esp_eth_new_netif_glue(handles[0]);
    err = glue ? esp_netif_attach(ethernet_, glue) : ESP_ERR_NO_MEM;
    if (err == ESP_OK) err = esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, event, this);
    if (err == ESP_OK) err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, event, this);
    if (err == ESP_OK) err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event, this);
    if (err == ESP_OK) err = esp_event_handler_register(ESP_HOSTED_EVENT, ESP_EVENT_ANY_ID, event, this);
    if (err == ESP_OK) err = esp_eth_start(handles[0]);
    free(handles); if (err != ESP_OK) return err;
    if (xTaskCreate([](void *ctx) { static_cast<NetworkManager *>(ctx)->run(); }, "wifi_config", 8192, this, 4, &worker_) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
bool NetworkManager::enqueue(const WifiCommand &command) {
    return queue_ && xQueueSend(queue_, &command, 0) == pdTRUE;
}
NetworkSnapshot NetworkManager::snapshot() {
    if (!mutex_) return {};
    xSemaphoreTake(mutex_, portMAX_DELAY); const auto copy = state_; xSemaphoreGive(mutex_); return copy;
}
bool NetworkManager::interfaceName(char *name, size_t capacity) {
    if (!mutex_ || !name || capacity < 6) return false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const auto path = active_.load();
    auto *netif = path == NetworkPath::Ethernet ? ethernet_ : path == NetworkPath::Wifi ? wifi_ : nullptr;
    const bool ok = netif && esp_netif_get_netif_impl_name(netif, name) == ESP_OK;
    xSemaphoreGive(mutex_); return ok;
}
void NetworkManager::setMessage(const char *message, bool busy) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    std::snprintf(state_.message, sizeof(state_.message), "%s", message); state_.busy = busy;
    state_.wifi_configured = !settings_.ssid.empty();
    state_.wifi_open = state_.wifi_configured && settings_.password.empty();
    std::snprintf(state_.saved_password, sizeof(state_.saved_password), "%s", settings_.password.c_str());
    std::snprintf(state_.ssid, sizeof(state_.ssid), "%s", settings_.ssid.c_str()); ++state_.generation;
    xSemaphoreGive(mutex_);
}
void NetworkManager::updateRoute() {
    // This function never makes remote Wi-Fi calls. Ethernet can take over even
    // while the configuration worker is waiting for the C6 coprocessor.
    xSemaphoreTake(mutex_, portMAX_DELAY);
    esp_netif_ip_info_t eth{}, wifi{};
    const bool eth_ready = eth_ip_.load() && ethernet_ && esp_netif_get_ip_info(ethernet_, &eth) == ESP_OK && eth.ip.addr;
    const bool wifi_ready = wifi_enabled_.load() && wifi_ip_.load() && wifi_ && esp_netif_get_ip_info(wifi_, &wifi) == ESP_OK && wifi.ip.addr;
    auto path = selectNetworkPath(eth_ready, wifi_ready);
    auto *selected = path == NetworkPath::Ethernet ? ethernet_ : path == NetworkPath::Wifi ? wifi_ : nullptr;
    if (selected && esp_netif_set_default_netif(selected) != ESP_OK) path = NetworkPath::None;
    state_.active = path; state_.ethernet_ready = eth_ready; state_.wifi_ready = wifi_ready;
    state_.ethernet_ip[0] = state_.wifi_ip[0] = 0;
    if (eth_ready) std::snprintf(state_.ethernet_ip, sizeof(state_.ethernet_ip), IPSTR, IP2STR(&eth.ip));
    if (wifi_ready) std::snprintf(state_.wifi_ip, sizeof(state_.wifi_ip), IPSTR, IP2STR(&wifi.ip));
    if (wifi_ready && !state_.busy) std::snprintf(state_.message, sizeof(state_.message), "Wi-Fi подключён. Провод имеет приоритет.");
    ++state_.generation; active_.store(path); xSemaphoreGive(mutex_);
    ESP_LOGI(TAG, "Route DMA heap: free=%u, largest=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT)));
    ESP_LOGI(TAG, "Active interface: %s", path == NetworkPath::Ethernet ? "Ethernet" : path == NetworkPath::Wifi ? "Wi-Fi" : "none");
}
void NetworkManager::event(void *context, esp_event_base_t base, int32_t id, void *data) {
    auto *self = static_cast<NetworkManager *>(context);
    if (base == ESP_HOSTED_EVENT && (id == ESP_HOSTED_EVENT_TRANSPORT_FAILURE || id == ESP_HOSTED_EVENT_TRANSPORT_DOWN)) {
        self->wifi_ip_.store(false); self->connect_requested_.store(true);
        xSemaphoreTake(self->mutex_, portMAX_DELAY);
        std::snprintf(self->state_.message, sizeof(self->state_.message), "Нет связи с ESP32-C6. Проверьте модуль и его прошивку.");
        ++self->state_.generation; xSemaphoreGive(self->mutex_);
    }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        if (self->scan_running_.load()) self->scan_done_.store(true);
        return;
    }
    else if (base == ETH_EVENT && (id == ETHERNET_EVENT_DISCONNECTED || id == ETHERNET_EVENT_STOP)) self->eth_ip_.store(false);
    else if (base == IP_EVENT && id == IP_EVENT_ETH_GOT_IP) self->eth_ip_.store(true);
    else if (base == IP_EVENT && id == IP_EVENT_ETH_LOST_IP) self->eth_ip_.store(false);
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) self->wifi_ip_.store(self->wifi_enabled_.load());
    else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) self->wifi_ip_.store(false);
    else if (base == WIFI_EVENT && (id == WIFI_EVENT_STA_DISCONNECTED || id == WIFI_EVENT_STA_STOP)) {
        self->wifi_ip_.store(false);
        if (id == WIFI_EVENT_STA_DISCONNECTED) {
            const auto *disconnected = static_cast<wifi_event_sta_disconnected_t *>(data);
            ESP_LOGW(TAG, "Wi-Fi disconnected, reason=%u", disconnected ? disconnected->reason : 0);
        }
        self->connect_requested_.store(true);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) { self->connect_requested_.store(true); return; }
    else return;
    self->updateRoute();
}
esp_err_t NetworkManager::initializeWifi() {
    if (!hosted_initialized_) {
        ESP_LOGI(TAG, "Starting ESP-Hosted after system init: internal free=%u, largest=%u",
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
        ESP_LOGI(TAG, "Wi-Fi DMA heap: free=%u, largest=%u; packet buffers allocated on demand=%s",
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT)),
#ifdef CONFIG_ESP_HOSTED_USE_MEMPOOL
                 "no");
#else
                 "yes");
#endif
        const auto err = esp_hosted_init();
        if (err != ESP_OK) return err;
        hosted_initialized_ = true;
    }
    if (!wifi_initialized_) {
        // esp_wifi_remote maps the standard station API to ESP-Hosted / SDIO.
        if (!wifi_) {
            auto *netif = esp_netif_create_default_wifi_sta(); if (!netif) return ESP_ERR_NO_MEM;
            esp_netif_set_route_prio(netif, 50);
            xSemaphoreTake(mutex_, portMAX_DELAY); wifi_ = netif; xSemaphoreGive(mutex_);
        }
        wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
        const auto err = esp_wifi_init(&config); if (err != ESP_OK) return err;
        wifi_initialized_ = true;
    }
    auto err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    return err;
}
esp_err_t NetworkManager::configureWifi() {
    auto err = initializeWifi(); if (err != ESP_OK) return err;
    if (wifi_started_) { esp_wifi_disconnect(); wifi_ip_.store(false); updateRoute(); }
    wifi_config_t config{};
    std::memcpy(config.sta.ssid, settings_.ssid.data(), settings_.ssid.size());
    std::memcpy(config.sta.password, settings_.password.data(), settings_.password.size());
    config.sta.threshold.authmode = settings_.password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    config.sta.pmf_cfg.capable = true;
    err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err == ESP_OK && !wifi_started_) { err = esp_wifi_start(); if (err == ESP_OK) wifi_started_ = true; }
    if (err == ESP_OK) connect_requested_.store(true);
    return err;
}
void NetworkManager::setScanResult(WifiScanState status, const char *message, const std::vector<WifiNetwork> &networks) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    auto &scan = state_.scan; scan.state = status;
    scan.count = static_cast<uint8_t>(std::min(networks.size(), WIFI_SCAN_LIMIT));
    for (size_t i = 0; i < WIFI_SCAN_LIMIT; ++i) scan.networks[i] = i < scan.count ? networks[i] : WifiNetwork{};
    std::snprintf(scan.message, sizeof(scan.message), "%s", message);
    ++scan.generation; ++state_.generation; state_.busy = status == WifiScanState::Scanning;
    xSemaphoreGive(mutex_);
}
void NetworkManager::cancelScan() {
    const bool running = scan_running_.exchange(false);
    const bool pending = scan_pending_; scan_pending_ = false; scan_done_.store(false);
    if (running) { esp_wifi_scan_stop(); esp_wifi_clear_ap_list(); }
    if (running || pending) setScanResult(WifiScanState::Idle, "Поиск остановлен");
}
void NetworkManager::requestScan() {
    if (scan_pending_ || scan_running_.load()) return;
    setScanResult(WifiScanState::Scanning, "Поиск сетей 2.4 ГГц...");
    auto err = initializeWifi();
    if (err == ESP_OK && !wifi_started_) { err = esp_wifi_start(); if (err == ESP_OK) wifi_started_ = true; }
    // A connecting station cannot scan. Keep an established connection intact.
    if (err == ESP_OK && !wifi_ip_.load()) esp_wifi_disconnect();
    if (err != ESP_OK) {
        char message[160]; std::snprintf(message, sizeof(message), "Ошибка Wi-Fi: %s. Проверьте ESP32-C6.", esp_err_to_name(err));
        setScanResult(WifiScanState::Error, message); return;
    }
    scan_done_.store(false); scan_pending_ = true;
    scan_start_after_ = static_cast<uint64_t>(esp_timer_get_time() / 1000) + 300;
}
void NetworkManager::finishScan() {
    scan_running_.store(false); scan_done_.store(false);
    uint16_t count = 40;
    std::vector<wifi_ap_record_t> records(count);
    const auto err = esp_wifi_scan_get_ap_records(&count, records.data());
    if (err != ESP_OK) {
        esp_wifi_clear_ap_list(); setScanResult(WifiScanState::Error, "Не удалось получить список сетей. Повторите поиск."); return;
    }
    std::vector<WifiNetwork> networks; networks.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        const auto &ap = records[i]; WifiNetwork network;
        std::memcpy(network.ssid, ap.ssid, 32); network.ssid[32] = 0;
        network.rssi = ap.rssi; network.channel = ap.primary;
        switch (ap.authmode) {
            case WIFI_AUTH_OPEN: network.security = WifiSecurity::Open; break;
            case WIFI_AUTH_WPA2_PSK: case WIFI_AUTH_WPA_WPA2_PSK:
            case WIFI_AUTH_WPA3_PSK: case WIFI_AUTH_WPA2_WPA3_PSK:
                network.security = WifiSecurity::Password; break;
            default: network.security = WifiSecurity::Unsupported; break;
        }
        networks.push_back(network);
    }
    networks = normalizeWifiNetworks(std::move(networks));
    setScanResult(WifiScanState::Ready, networks.empty() ? "Сети не найдены. Можно ввести SSID вручную." : "Выберите сеть. Сначала показаны сети с лучшим сигналом.", networks);
    if (!settings_.ssid.empty() && !wifi_ip_.load()) connect_requested_.store(true);
}
void NetworkManager::run() {
    const bool loaded = loadWifiSettings(settings_);
    wifi_enabled_.store(!settings_.ssid.empty());
    setMessage(loaded ? settings_.ssid.empty() ? "Wi-Fi не настроен. Подключите сеть 2.4 ГГц." : "Подключаем Wi-Fi..." : "Не удалось прочитать настройки Wi-Fi");
    bool apply = !settings_.ssid.empty(); uint64_t next_connect = 0;
    while (true) {
        WifiCommand command{};
        if (xQueueReceive(queue_, &command, pdMS_TO_TICKS(500)) == pdTRUE) {
            if (command.action == WifiAction::Scan) { requestScan(); }
            else if (command.action == WifiAction::Disconnect) {
                cancelScan();
                if (!saveWifiSettings({})) setMessage("Не удалось удалить настройки Wi-Fi");
                else {
                    settings_ = {}; wifi_enabled_.store(false); connect_requested_.store(false); wifi_ip_.store(false); updateRoute();
                    if (wifi_started_) { esp_wifi_stop(); wifi_started_ = false; }
                    setMessage("Wi-Fi отключён. Провод продолжает работать."); apply = false;
                }
            } else {
                WifiSettings updated;
                if (!resolveWifiSettings(command, settings_, updated)) setMessage("Проверьте SSID и пароль (8-63 символа). Для открытой сети включите флажок.");
                else if (!saveWifiSettings(updated)) setMessage("Не удалось сохранить настройки Wi-Fi");
                else { cancelScan(); settings_ = std::move(updated); wifi_enabled_.store(true); apply = true; }
            }
            command = {};
        }
        const auto millis = static_cast<uint64_t>(esp_timer_get_time() / 1000);
        if (scan_pending_ && millis >= scan_start_after_) {
            scan_pending_ = false; scan_done_.store(false); scan_running_.store(true);
            wifi_scan_config_t config{}; config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
            config.scan_time.active.max = 120;
            const auto err = esp_wifi_scan_start(&config, false);
            if (err != ESP_OK) {
                scan_running_.store(false);
                setScanResult(WifiScanState::Error, "Не удалось начать поиск. Повторите попытку.");
                connect_requested_.store(!settings_.ssid.empty());
            } else scan_deadline_ = millis + 15000;
        }
        if (scan_running_.load() && scan_done_.load()) finishScan();
        if (scan_running_.load() && millis >= scan_deadline_) {
            cancelScan(); setScanResult(WifiScanState::Error, "Нет ответа Wi-Fi модуля. Повторите поиск.");
            connect_requested_.store(!settings_.ssid.empty());
        }
        if (apply && !scan_pending_ && !scan_running_.load()) {
            setMessage("Подключаем Wi-Fi...", true);
            const auto err = configureWifi(); apply = false; next_connect = 0;
            if (err != ESP_OK) {
                char text[192]; std::snprintf(text, sizeof(text), "Ошибка модуля Wi-Fi: %s. Проверьте прошивку ESP32-C6.", esp_err_to_name(err));
                setMessage(text); connect_requested_.store(false);
            }
        }
        const auto now = static_cast<uint64_t>(esp_timer_get_time() / 1000000);
        if (!scan_pending_ && !scan_running_.load() && wifi_started_ && !settings_.ssid.empty() && !wifi_ip_.load() && connect_requested_.load() && now >= next_connect) {
            connect_requested_.store(false); next_connect = now + 5;
            setMessage("Подключение Wi-Fi. Если не удаётся войти, проверьте сеть 2.4 ГГц и пароль.");
            const auto err = esp_wifi_connect();
            if (err != ESP_OK) { setMessage("Не удалось подключить Wi-Fi. Повтор через 5 секунд."); connect_requested_.store(true); }
        }
    }
}
