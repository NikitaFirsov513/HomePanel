#include "HomeAssistant.hpp"
#include "DeviceManager.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <algorithm>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #c); std::exit(1); } } while (false)
int main() {
    HomeAssistantConfig config{"http://192.168.31.10:8123/", "test-token"};
    CHECK(normalizeHomeAssistantConfig(config) && config.address == "http://192.168.31.10:8123");
    for (auto address : {"ftp://server", "http://user:pass@server", "https://server/api", "http://server?token=secret", "http://server\r\n", "http://", "http://:8123", "http://server:", "http://server:0", "http://server:99999", "http://server:abc", "http://[bad]"}) {
        HomeAssistantConfig invalid{address, "test"}; CHECK(!normalizeHomeAssistantConfig(invalid));
    }
    HomeAssistantConfig bad_token{"https://server", "token\r\nHeader: bad"}; CHECK(!normalizeHomeAssistantConfig(bad_token));
    const char *states = R"([
      {"entity_id":"light.bedroom","state":"on","attributes":{"friendly_name":"Спальня HA","brightness":128,"supported_color_modes":["rgb","color_temp"],"color_temp_kelvin":3000,"min_color_temp_kelvin":2700,"max_color_temp_kelvin":6500}},
      {"entity_id":"switch.socket","state":"off","attributes":{}},
      {"entity_id":"vacuum.robot","state":"docked","attributes":{"supported_features":8252,"fan_speed":"quiet","fan_speed_list":["quiet","turbo"]}},
      {"entity_id":"cover.blinds","state":"open","attributes":{"supported_features":15,"current_position":75}},
      {"entity_id":"climate.room","state":"heat","attributes":{"supported_features":9,"temperature":22,"min_temp":10,"max_temp":30,"target_temp_step":0.5,"hvac_modes":["off","heat"],"fan_modes":["auto","low"],"fan_mode":"auto"}},
      {"entity_id":"sensor.temperature","state":"21.5","attributes":{"unit_of_measurement":"°C"}},
      {"entity_id":"binary_sensor.leak","state":"off","attributes":{}},
      {"entity_id":"light.offline","state":"unavailable","attributes":{"supported_color_modes":["rgb"]}},
      {"entity_id":"scene.night","state":"2026-10-18T01:00:00Z","attributes":{}},
      {"entity_id":"button.test","state":"unknown","attributes":{}},
      {"entity_id":"camera.front","state":"idle","attributes":{}}
    ])";
    std::vector<Device> devices;
    CHECK(parseHomeAssistantStates(states, std::strlen(states), devices)); CHECK(devices.size() == 10);
    auto device = [&](const char *id) -> Device & {
        auto it = std::find_if(devices.begin(), devices.end(), [&](const Device &d) { return d.id == id; }); CHECK(it != devices.end()); return *it;
    };
    auto &lamp = device("ha:light.bedroom");
    CHECK(lamp.source == DeviceSource::HomeAssistant && lamp.available && lamp.controls.size() == 4);
    CHECK(devicePlanKey(lamp) == "ha:light.bedroom");
    CHECK(device("ha:light.offline").controls.empty() && !device("ha:light.offline").available);
    CHECK(device("ha:sensor.temperature").readings.front() == "Показание: 21.5 C");
    CHECK(device("ha:button.test").available && device("ha:button.test").controls.size() == 1);
    std::string path, body;
    auto action = [&](const char *id, const char *instance, ActionValue kind) {
        const auto &d = device(id); DeviceAction a{};
        const auto it = std::find_if(d.controls.begin(), d.controls.end(), [&](const DeviceControl &c) { return c.instance == instance; }); CHECK(it != d.controls.end());
        std::snprintf(a.device_id, sizeof(a.device_id), "%s", d.id.c_str());
        std::snprintf(a.type, sizeof(a.type), "%s", it->type.c_str());
        std::snprintf(a.instance, sizeof(a.instance), "%s", instance); a.kind = kind; return a;
    };
    auto a = action("ha:light.bedroom", "brightness", ActionValue::Number); a.number = 70;
    CHECK(buildHomeAssistantAction(lamp, a, path, body));
    CHECK(path == "/api/services/light/turn_on" && body.find("\"brightness_pct\":70") != std::string::npos);
    a.number = 101; CHECK(!buildHomeAssistantAction(lamp, a, path, body));
    a.number = std::numeric_limits<double>::infinity(); CHECK(!buildHomeAssistantAction(lamp, a, path, body));
    a.number = 30; a.relative = true; CHECK(!buildHomeAssistantAction(lamp, a, path, body));
    a = action("ha:light.bedroom", "rgb", ActionValue::Number); a.number = 0xFF8000;
    CHECK(buildHomeAssistantAction(lamp, a, path, body) && body.find("[255,128,0]") != std::string::npos);
    a = action("ha:vacuum.robot", "return_to_base", ActionValue::Command);
    CHECK(buildHomeAssistantAction(device(a.device_id), a, path, body) && path == "/api/services/vacuum/return_to_base");
    a = action("ha:cover.blinds", "open", ActionValue::Number); a.number = 25;
    CHECK(buildHomeAssistantAction(device(a.device_id), a, path, body) && path == "/api/services/cover/set_cover_position");
    a = action("ha:climate.room", "thermostat", ActionValue::String); std::strcpy(a.text, "heat");
    CHECK(buildHomeAssistantAction(device(a.device_id), a, path, body) && path == "/api/services/climate/set_hvac_mode");
    std::strcpy(a.text, "invalid"); CHECK(!buildHomeAssistantAction(device(a.device_id), a, path, body));
    a = action("ha:scene.night", "activate", ActionValue::Command);
    CHECK(buildHomeAssistantAction(device(a.device_id), a, path, body) && path == "/api/services/scene/turn_on");
    auto offline = lamp; offline.available = false;
    a = action("ha:light.bedroom", "on", ActionValue::Boolean);
    CHECK(!buildHomeAssistantAction(offline, a, path, body));
    auto wrong_source = lamp; wrong_source.source = DeviceSource::Yandex;
    CHECK(!buildHomeAssistantAction(wrong_source, a, path, body));
    std::memset(a.device_id, 'x', sizeof(a.device_id)); CHECK(!buildHomeAssistantAction(lamp, a, path, body));
    const auto original_size = devices.size();
    CHECK(!parseHomeAssistantStates("{}", 2, devices) && devices.size() == original_size);
    const char *duplicates = R"([{"entity_id":"light.a","state":"on"},{"entity_id":"light.a","state":"off"}])";
    CHECK(!parseHomeAssistantStates(duplicates, std::strlen(duplicates), devices) && devices.size() == original_size);
    DeviceManager registry;
    Device yandex; yandex.id = "lamp"; yandex.name = "Yandex";
    CHECK(registry.replaceSource(DeviceSource::Yandex, {yandex}) == ESP_OK);
    CHECK(registry.replaceSource(DeviceSource::HomeAssistant, devices) == ESP_OK);
    CHECK(registry.getDeviceIds().size() == original_size + 1);
    CHECK(registry.clearSource(DeviceSource::Yandex) == ESP_OK && registry.getDeviceById(lamp.id));
    CHECK(registry.getDeviceIds().size() == original_size);
    CHECK(registry.replaceSource(DeviceSource::Yandex, {yandex}) == ESP_OK);
    CHECK(registry.clearSource(DeviceSource::HomeAssistant) == ESP_OK && registry.getDeviceById("lamp"));
    CHECK(registry.replaceSource(DeviceSource::HomeAssistant, {yandex}) == ESP_ERR_INVALID_ARG);
    CHECK(registry.getDeviceIds().size() == 1);
    std::puts("Home Assistant adapter and source isolation tests passed");
}
