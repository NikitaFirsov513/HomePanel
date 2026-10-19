#include "Network.hpp"
#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <memory>
namespace { constexpr const char *partition = "auth_nvs"; }
bool loadWifiSettings(WifiSettings &settings) {
    settings = {};
    if (nvs_flash_init_partition(partition) != ESP_OK) return false;
    nvs_handle_t h;
    auto err = nvs_open_from_partition(partition, "wifi", NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return true;
    if (err != ESP_OK) return false;
    size_t size = 0; err = nvs_get_blob(h, "settings_v1", nullptr, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); return true; }
    if (err != ESP_OK || !size || size > 1024) { nvs_close(h); return false; }
    std::string json(size, '\0'); err = nvs_get_blob(h, "settings_v1", json.data(), &size); nvs_close(h);
    if (err != ESP_OK) return false;
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_ParseWithLength(json.data(), json.size()), cJSON_Delete);
    auto *ssid = cJSON_GetObjectItemCaseSensitive(root.get(), "ssid");
    auto *password = cJSON_GetObjectItemCaseSensitive(root.get(), "password");
    if (!cJSON_IsString(ssid) || !cJSON_IsString(password)) return false;
    WifiSettings parsed{ssid->valuestring, password->valuestring};
    if (!validWifiSettings(parsed)) return false;
    settings = std::move(parsed); return true;
}
bool saveWifiSettings(const WifiSettings &settings) {
    std::string json;
    if (!settings.ssid.empty()) {
        if (!validWifiSettings(settings)) return false;
        std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_CreateObject(), cJSON_Delete);
        if (!root || !cJSON_AddStringToObject(root.get(), "ssid", settings.ssid.c_str()) ||
            !cJSON_AddStringToObject(root.get(), "password", settings.password.c_str())) return false;
        auto *raw = cJSON_PrintUnformatted(root.get()); if (!raw) return false;
        json = raw; cJSON_free(raw);
    }
    if (nvs_flash_init_partition(partition) != ESP_OK) return false;
    nvs_handle_t h;
    if (nvs_open_from_partition(partition, "wifi", NVS_READWRITE, &h) != ESP_OK) return false;
    auto err = json.empty() ? nvs_erase_key(h, "settings_v1") : nvs_set_blob(h, "settings_v1", json.data(), json.size());
    if (err == ESP_ERR_NVS_NOT_FOUND && json.empty()) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h); return err == ESP_OK;
}
