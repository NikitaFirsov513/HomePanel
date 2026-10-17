#include "DeviceManager.hpp"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <type_traits>

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "FAILED at line %d: %s\n", __LINE__, #condition); \
    std::exit(1); } } while (false)

static esp_err_t load(DeviceManager &manager, const char *json)
{
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(json), &cJSON_Delete);
    return manager.createDevicesFromJson(root.get());
}

int main()
{
    static_assert(!std::is_copy_constructible<DeviceManager>::value);
    {
        DeviceManager manager;
        CHECK(load(manager, R"({"devices":[{"id":"a","name":"Lamp","type":"light"}]})") == ESP_OK);
        const char *invalid[] = {
            "{", "null", "[]", "{}", R"({"devices":{}})", R"({"devices":[null]})",
            R"({"devices":[{"id":"","name":"Lamp","type":"light"}]})",
            R"({"devices":[{"id":"b","name":1,"type":"light"}]})",
            R"({"devices":[{"id":"b","name":"Lamp"}]})",
            R"({"devices":[{"id":"b","name":"New","type":"light"},false]})",
            R"({"devices":[{"id":"b","name":"One","type":"light"},{"id":"b","name":"Two","type":"light"}]})"
        };
        for (const char *json : invalid) {
            CHECK(load(manager, json) == ESP_ERR_INVALID_ARG);
            auto device = manager.getDeviceById("a");
            CHECK(device && device->name == "Lamp");
            CHECK(manager.getDeviceIds().size() == 1);
        }
        std::vector<Device> snapshot;
        CHECK(manager.getDevices(snapshot) == ESP_OK && snapshot.size() == 1);
        force_mutex_timeout = true;
        CHECK(load(manager, R"({"devices":[]})") == ESP_ERR_TIMEOUT);
        CHECK(manager.getDevices(snapshot) == ESP_ERR_TIMEOUT);
        CHECK(snapshot.size() == 1);
        force_mutex_timeout = false;
        CHECK(manager.getDeviceById("a").has_value());
        CHECK(load(manager, R"({"devices":[{"id":"b","name":"Updated","type":"socket"}]})") == ESP_OK);
        CHECK(!manager.getDeviceById("a").has_value());
        CHECK(manager.getDeviceById("b")->name == "Updated");
        CHECK(snapshot[0].id == "a");
        CHECK(load(manager, R"({"devices":[]})") == ESP_OK);
        CHECK(manager.getDeviceIds().empty());
    }
    CHECK(live_mutexes == 0);
    fail_mutex_allocation = true;
    {
        DeviceManager manager;
        CHECK(load(manager, R"({"devices":[]})") == ESP_ERR_NO_MEM);
        std::vector<Device> snapshot;
        CHECK(manager.getDevices(snapshot) == ESP_ERR_NO_MEM);
        CHECK(!manager.getDeviceById("a"));
    }
    CHECK(live_mutexes == 0);
    std::puts("DeviceManager regression tests passed");
}
