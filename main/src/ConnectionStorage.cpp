#include "Connections.hpp"
#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <memory>

namespace { constexpr const char *partition = "auth_nvs"; }
bool loadHomeAssistantConfig(HomeAssistantConfig &config) {
    config = {};
    if (nvs_flash_init_partition(partition) != ESP_OK) return false;
    nvs_handle_t handle;
    auto err = nvs_open_from_partition(partition, "homeassistant", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return true;
    if (err != ESP_OK) return false;
    size_t size = 0; err = nvs_get_blob(handle, "config_v1", nullptr, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) { nvs_close(handle); return true; }
    if (err != ESP_OK || size == 0 || size > 2048) { nvs_close(handle); return false; }
    std::string json(size, '\0'); err = nvs_get_blob(handle, "config_v1", json.data(), &size); nvs_close(handle);
    if (err != ESP_OK) return false;
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_ParseWithLength(json.data(), json.size()), cJSON_Delete);
    const auto *address = cJSON_GetObjectItemCaseSensitive(root.get(), "address");
    const auto *token = cJSON_GetObjectItemCaseSensitive(root.get(), "token");
    if (!cJSON_IsString(address) || !cJSON_IsString(token)) return false;
    HomeAssistantConfig parsed{address->valuestring, token->valuestring};
    if (!normalizeHomeAssistantConfig(parsed)) return false;
    config = std::move(parsed); return true;
}
bool saveHomeAssistantConfig(const HomeAssistantConfig &config) {
    std::string json;
    if (!config.address.empty()) {
        HomeAssistantConfig normalized = config;
        if (!normalizeHomeAssistantConfig(normalized)) return false;
        std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_CreateObject(), cJSON_Delete);
        if (!root || !cJSON_AddStringToObject(root.get(), "address", normalized.address.c_str()) ||
            !cJSON_AddStringToObject(root.get(), "token", normalized.token.c_str())) return false;
        char *encoded = cJSON_PrintUnformatted(root.get()); if (!encoded) return false;
        json = encoded; cJSON_free(encoded);
    }
    if (nvs_flash_init_partition(partition) != ESP_OK) return false;
    nvs_handle_t handle;
    if (nvs_open_from_partition(partition, "homeassistant", NVS_READWRITE, &handle) != ESP_OK) return false;
    auto err = json.empty() ? nvs_erase_key(handle, "config_v1") : nvs_set_blob(handle, "config_v1", json.data(), json.size());
    if (err == ESP_ERR_NVS_NOT_FOUND && json.empty()) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle); return err == ESP_OK;
}
