#include "DeviceManager.hpp"
#include <algorithm>
#include <utility>

namespace {
class DeviceLock {
public:
    explicit DeviceLock(SemaphoreHandle_t mutex) : mutex_(mutex) {
        locked_ = mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE;
    }
    ~DeviceLock() { if (locked_) xSemaphoreGive(mutex_); }
    explicit operator bool() const { return locked_; }
    DeviceLock(const DeviceLock &) = delete;
    DeviceLock &operator=(const DeviceLock &) = delete;
private:
    SemaphoreHandle_t mutex_;
    bool locked_ = false;
};
}

DeviceManager::~DeviceManager()
{
    if (mutex_home_devices) vSemaphoreDelete(mutex_home_devices);
}

esp_err_t DeviceManager::createDevicesFromJson(cJSON *root)
{
    if (!cJSON_IsObject(root)) return ESP_ERR_INVALID_ARG;
    cJSON *devices = cJSON_GetObjectItemCaseSensitive(root, "devices");
    if (!cJSON_IsArray(devices)) return ESP_ERR_INVALID_ARG;
    // Parse outside the lock and commit only a fully validated response.
    std::vector<Device> parsed;
    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, devices) {
        if (!cJSON_IsObject(item)) return ESP_ERR_INVALID_ARG;
        auto *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        auto *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        auto *type = cJSON_GetObjectItemCaseSensitive(item, "type");
        if (!cJSON_IsString(id) || !id->valuestring || !id->valuestring[0] ||
            !cJSON_IsString(name) || !name->valuestring ||
            !cJSON_IsString(type) || !type->valuestring)
            return ESP_ERR_INVALID_ARG;
        if (std::any_of(parsed.begin(), parsed.end(),
                        [&](const Device &device) { return device.id == id->valuestring; }))
            return ESP_ERR_INVALID_ARG;
        Device device;
        device.id = id->valuestring;
        device.name = name->valuestring;
        device.type = type->valuestring;
        parsed.push_back(std::move(device));
    }
    if (!mutex_home_devices) return ESP_ERR_NO_MEM;
    DeviceLock lock(mutex_home_devices);
    if (!lock) return ESP_ERR_TIMEOUT;
    home_devices.swap(parsed);
    return ESP_OK;
}

esp_err_t DeviceManager::getDevices(std::vector<Device> &devices)
{
    if (!mutex_home_devices) return ESP_ERR_NO_MEM;
    DeviceLock lock(mutex_home_devices);
    if (!lock) return ESP_ERR_TIMEOUT;
    devices = home_devices;
    return ESP_OK;
}

std::optional<Device> DeviceManager::getDeviceById(const std::string &id)
{
    DeviceLock lock(mutex_home_devices);
    if (!lock) return std::nullopt;
    for (const auto &device : home_devices)
        if (device.id == id) return device;
    return std::nullopt;
}

std::vector<std::string> DeviceManager::getDeviceIds()
{
    std::vector<std::string> ids;
    DeviceLock lock(mutex_home_devices);
    if (!lock) return ids;
    for (const auto &device : home_devices) ids.push_back(device.id);
    return ids;
}
