#pragma once
#include "Network.hpp"
#include "esp_netif.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <atomic>

class NetworkManager {
public:
    esp_err_t init();
    bool ready() const { return active_.load() != NetworkPath::None; }
    NetworkSnapshot snapshot();
    bool enqueue(const WifiCommand &command);
    bool interfaceName(char *name, size_t capacity);
private:
    std::atomic<NetworkPath> active_{NetworkPath::None};
    std::atomic<bool> eth_ip_{false}, wifi_ip_{false}, connect_requested_{false};
    std::atomic<bool> wifi_enabled_{false};
    std::atomic<bool> scan_running_{false}, scan_done_{false};
    bool scan_pending_ = false;
    uint64_t scan_start_after_ = 0, scan_deadline_ = 0;
    SemaphoreHandle_t mutex_ = nullptr;
    QueueHandle_t queue_ = nullptr;
    TaskHandle_t worker_ = nullptr;
    esp_netif_t *ethernet_ = nullptr, *wifi_ = nullptr;
    bool hosted_initialized_ = false, wifi_initialized_ = false, wifi_started_ = false;
    NetworkSnapshot state_;
    WifiSettings settings_;
    void updateRoute();
    void setMessage(const char *message, bool busy = false);
    void run();
    esp_err_t configureWifi();
    esp_err_t initializeWifi();
    void requestScan();
    void finishScan();
    void cancelScan();
    void setScanResult(WifiScanState state, const char *message, const std::vector<WifiNetwork> &networks = {});
    static void event(void *context, esp_event_base_t base, int32_t id, void *data);
};
