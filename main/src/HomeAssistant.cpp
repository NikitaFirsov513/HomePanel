#include "HomeAssistant.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <cstdio>

namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
std::string str(const cJSON *j, const char *key) {
    const auto *v = cJSON_GetObjectItemCaseSensitive(j, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}
bool number(const cJSON *j, const char *key, double &value) {
    const auto *v = cJSON_GetObjectItemCaseSensitive(j, key);
    if (!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble)) return false;
    value = v->valuedouble; return true;
}
bool has(const cJSON *j, const char *key, const char *value) {
    const cJSON *v = nullptr;
    cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(j, key))
        if (cJSON_IsString(v) && !std::strcmp(v->valuestring, value)) return true;
    return false;
}
bool entityId(const std::string &id) {
    const auto dot = id.find('.');
    return dot != std::string::npos && dot > 0 && dot + 1 < id.size() && id.size() <= 124 &&
        std::all_of(id.begin(), id.end(), [](unsigned char c) { return (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '.'; }) && id.find('.', dot + 1) == std::string::npos;
}
void toggle(Device &d, const std::string &state) {
    DeviceControl c; c.type = "ha.on"; c.instance = "on";
    c.known = state == "on" || state == "off"; c.boolean = state == "on";
    d.controls.push_back(c);
}
void command(Device &d, const char *name) {
    DeviceControl c; c.type = "ha.command"; c.instance = name; c.kind = ControlKind::Command;
    d.controls.push_back(c);
}
void range(Device &d, const cJSON *attrs, const char *instance, const char *attribute,
           double min, double max, double step, double scale = 1) {
    if (!std::isfinite(min) || !std::isfinite(max) || !std::isfinite(step) || step <= 0 ||
        max <= min || (max - min) / step > 10000) return;
    DeviceControl c; c.kind = ControlKind::Range; c.instance = instance; c.type = "ha." + c.instance;
    c.minimum = min; c.maximum = max; c.step = step;
    double value = 0;
    c.known = number(attrs, attribute, value) && value * scale >= min && value * scale <= max;
    c.number = c.known ? value * scale : min;
    d.controls.push_back(c);
}
void mode(Device &d, const cJSON *attrs, const char *instance, const char *attribute, const char *options) {
    DeviceControl c; c.kind = ControlKind::Mode; c.instance = instance; c.type = "ha." + c.instance;
    c.text = str(attrs, attribute);
    const cJSON *entry = nullptr;
    cJSON_ArrayForEach(entry, cJSON_GetObjectItemCaseSensitive(attrs, options)) {
        if (cJSON_IsString(entry) && entry->valuestring[0] && std::strlen(entry->valuestring) < 128 &&
            !std::strchr(entry->valuestring, '\n') && c.options.size() < 64) c.options.push_back({entry->valuestring});
    }
    c.known = std::any_of(c.options.begin(), c.options.end(), [&](const ControlOption &o) { return o.value == c.text; });
    if (!c.options.empty()) d.controls.push_back(c);
}
}

bool normalizeHomeAssistantConfig(HomeAssistantConfig &config) {
    while (!config.address.empty() && config.address.back() == '/') config.address.pop_back();
    const size_t start = config.address.rfind("https://", 0) == 0 ? 8 : config.address.rfind("http://", 0) == 0 ? 7 : 0;
    if (!start || config.address.size() <= start || config.address.size() >= sizeof(ConnectionCommand::address) ||
        config.token.empty() || config.token.size() >= sizeof(ConnectionCommand::token)) return false;
    const auto host = config.address.substr(start);
    if (!std::all_of(host.begin(), host.end(), [](unsigned char c) { return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == ':' || c == '[' || c == ']'; })) return false;
    std::string hostname, port;
    if (host.front() == '[') {
        const auto close = host.find(']');
        if (close == std::string::npos || close < 3) return false;
        hostname = host.substr(1, close - 1);
        if (hostname.find(':') == std::string::npos || !std::all_of(hostname.begin(), hostname.end(), [](unsigned char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == ':';
        })) return false;
        if (close + 1 < host.size()) { if (host[close + 1] != ':') return false; port = host.substr(close + 2); if (port.empty()) return false; }
    } else {
        const auto colon = host.find(':'); hostname = host.substr(0, colon);
        if (hostname.empty() || hostname.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.") != std::string::npos ||
            hostname.front() == '.' || hostname.front() == '-' || hostname.back() == '-' || hostname.find("..") != std::string::npos) return false;
        if (colon != std::string::npos) { port = host.substr(colon + 1); if (port.empty()) return false; }
    }
    if (!port.empty()) {
        if (port.size() > 5) return false;
        unsigned value = 0;
        for (auto c : port) { if (c < '0' || c > '9') return false; value = value * 10 + c - '0'; }
        if (!value || value > 65535) return false;
    }
    return std::all_of(config.token.begin(), config.token.end(), [](unsigned char c) { return c >= 33 && c <= 126; });
}

bool parseHomeAssistantStates(const char *json, size_t length, std::vector<Device> &devices) {
    if (!json) return false;
    Json root(cJSON_ParseWithLength(json, length), cJSON_Delete);
    if (!cJSON_IsArray(root.get())) return false;
    std::vector<Device> parsed;
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, root.get()) {
        const auto id = str(item, "entity_id"), state = str(item, "state");
        if (id.empty() || state.empty()) return false;
        const auto domain = id.substr(0, id.find('.'));
        if (domain != "light" && domain != "switch" && domain != "input_boolean" && domain != "vacuum" &&
            domain != "cover" && domain != "climate" && domain != "sensor" && domain != "binary_sensor" &&
            domain != "scene" && domain != "script" && domain != "button" && domain != "input_button") continue;
        if (id.size() > 124) continue; // Queued commands carry at most 127 bytes including the source prefix.
        if (!entityId(id) || parsed.size() >= 512) return false;
        const auto *attrs = cJSON_GetObjectItemCaseSensitive(item, "attributes");
        Device d; d.source = DeviceSource::HomeAssistant; d.native_id = id; d.id = "ha:" + id;
        if (std::any_of(parsed.begin(), parsed.end(), [&](const Device &old) { return old.id == d.id; })) return false;
        d.name = str(attrs, "friendly_name"); if (d.name.empty()) d.name = id;
        d.available = state != "unavailable" && (state != "unknown" || domain == "scene" || domain == "script" || domain == "button" || domain == "input_button");
        d.type = "devices.types.other";
        if (domain == "light") d.type = "devices.types.light";
        if (domain == "switch" || domain == "input_boolean") d.type = "devices.types.socket";
        if (domain == "vacuum") d.type = "devices.types.vacuum_cleaner";
        if (domain == "cover") d.type = "devices.types.openable.curtain";
        if (domain == "climate") d.type = "devices.types.thermostat";
        if (domain == "sensor" || domain == "binary_sensor") d.type = "devices.types.sensor";
        if (domain == "sensor" || domain == "binary_sensor") {
            auto value = state;
            if (domain == "binary_sensor") value = state == "on" ? "Сработал" : state == "off" ? "Не сработал" : state;
            const auto unit = str(attrs, "unit_of_measurement");
            // The panel font has ASCII and Cyrillic; normalize common unit glyphs.
            const auto display_unit = unit == "°C" ? "C" : unit == "°F" ? "F" :
                std::all_of(unit.begin(), unit.end(), [](unsigned char c) { return c >= 32 && c <= 126; }) ? unit : "";
            d.readings.push_back("Показание: " + value + (display_unit.empty() ? "" : " " + display_unit));
        } else d.readings.push_back("Состояние: " + state);
        if (!d.available) { parsed.push_back(std::move(d)); continue; }
        double features = 0; number(attrs, "supported_features", features);
        const auto flags = features >= 0 && features <= 0x7FFFFFFF ? static_cast<unsigned>(features) : 0u;
        if (domain == "light" || domain == "switch" || domain == "input_boolean") toggle(d, state);
        if (domain == "light") {
            const char *modes[] = {"brightness", "color_temp", "hs", "xy", "rgb", "rgbw", "rgbww"};
            bool dimmable = false; for (auto m : modes) dimmable |= has(attrs, "supported_color_modes", m);
            if (dimmable) range(d, attrs, "brightness", "brightness", 1, 100, 1, 100.0 / 255);
            if (has(attrs, "supported_color_modes", "color_temp")) {
                double min = 0, max = 0;
                if (number(attrs, "min_color_temp_kelvin", min) && number(attrs, "max_color_temp_kelvin", max))
                    range(d, attrs, "temperature_k", "color_temp_kelvin", min, max, 1);
            }
            bool color = false; for (auto m : {"hs", "xy", "rgb", "rgbw", "rgbww"}) color |= has(attrs, "supported_color_modes", m);
            if (color) {
                DeviceControl c; c.type = "ha.rgb"; c.instance = "rgb"; c.kind = ControlKind::Rgb; c.maximum = 16777215;
                const auto *rgb=cJSON_GetObjectItemCaseSensitive(attrs,"rgb_color");
                if(cJSON_IsArray(rgb) && cJSON_GetArraySize(rgb)==3) {
                    unsigned packed=0;bool valid=true;
                    for(int i=0;i<3;++i) {const auto *part=cJSON_GetArrayItem(rgb,i);
                        if(!cJSON_IsNumber(part)||!std::isfinite(part->valuedouble)||part->valuedouble<0||part->valuedouble>255) {valid=false;break;}
                        packed=(packed<<8)|unsigned(part->valuedouble);
                    }
                    c.known=valid;if(valid)c.number=packed;
                }
                d.controls.push_back(c);
            }
        }
        if (domain == "vacuum") {
            if (flags & 8192) command(d, "start");
            if (flags & 4) command(d, "pause_cleaning");
            if (flags & 8) command(d, "stop");
            if (flags & 16) command(d, "return_to_base");
            if (flags & 32) mode(d, attrs, "fan_speed", "fan_speed", "fan_speed_list");
        }
        if (domain == "cover") {
            if (flags & 1) command(d, "open_cover");
            if (flags & 2) command(d, "close_cover");
            if (flags & 8) command(d, "stop_cover");
            if (flags & 4) range(d, attrs, "open", "current_position", 0, 100, 1);
        }
        if (domain == "climate") {
            double min = 7, max = 35, step = 1;
            number(attrs, "min_temp", min); number(attrs, "max_temp", max); number(attrs, "target_temp_step", step);
            if (flags & 1) range(d, attrs, "temperature", "temperature", min, max, step);
            mode(d, attrs, "thermostat", "hvac_mode", "hvac_modes");
            for (auto &c : d.controls) if (c.instance == "thermostat") { c.text = state; c.known = true; }
            if (flags & 8) mode(d, attrs, "fan_speed", "fan_mode", "fan_modes");
        }
        if (domain == "scene" || domain == "script") command(d, "activate");
        if (domain == "button" || domain == "input_button") command(d, "press");
        if (parsed.size() >= 512) return false;
        parsed.push_back(std::move(d));
    }
    devices.swap(parsed); return true;
}

bool buildHomeAssistantAction(const Device &d, const DeviceAction &a, std::string &path, std::string &json) {
    path.clear(); json.clear();
    if (d.source != DeviceSource::HomeAssistant || !d.available || !entityId(d.native_id) ||
        d.id != "ha:" + d.native_id || !memchr(a.device_id, 0, sizeof(a.device_id)) ||
        !memchr(a.type, 0, sizeof(a.type)) || !memchr(a.instance, 0, sizeof(a.instance)) ||
        !memchr(a.text, 0, sizeof(a.text)) || d.id != a.device_id || a.relative) return false;
    const auto it = std::find_if(d.controls.begin(), d.controls.end(), [&](const DeviceControl &c) { return c.type == a.type && c.instance == a.instance; });
    if (it == d.controls.end()) return false;
    const auto &c = *it;
    const auto domain = d.native_id.substr(0, d.native_id.find('.'));
    std::string service;
    Json body(cJSON_CreateObject(), cJSON_Delete);
    if (!body || !cJSON_AddStringToObject(body.get(), "entity_id", d.native_id.c_str())) return false;
    if (c.kind == ControlKind::Toggle && a.kind == ActionValue::Boolean && c.instance == "on" &&
        (domain == "light" || domain == "switch" || domain == "input_boolean")) service = a.boolean ? "turn_on" : "turn_off";
    else if (c.kind == ControlKind::Command && a.kind == ActionValue::Command) {
        if (domain == "vacuum" && (c.instance == "start" || c.instance == "stop" || c.instance == "return_to_base")) service = c.instance;
        else if (domain == "vacuum" && c.instance == "pause_cleaning") service = "pause";
        else if (domain == "cover" && (c.instance == "open_cover" || c.instance == "close_cover" || c.instance == "stop_cover")) service = c.instance;
        else if ((domain == "scene" || domain == "script") && c.instance == "activate") service = "turn_on";
        else if ((domain == "button" || domain == "input_button") && c.instance == "press") service = "press";
    } else if (c.kind == ControlKind::Range && a.kind == ActionValue::Number && std::isfinite(a.number) && a.number >= c.minimum && a.number <= c.maximum) {
        const char *parameter = nullptr; double value = a.number;
        if (domain == "light" && c.instance == "brightness") { service = "turn_on"; parameter = "brightness_pct"; }
        if (domain == "light" && c.instance == "temperature_k") { service = "turn_on"; parameter = "color_temp_kelvin"; }
        if (domain == "cover" && c.instance == "open") { service = "set_cover_position"; parameter = "position"; }
        if (domain == "climate" && c.instance == "temperature") { service = "set_temperature"; parameter = "temperature"; }
        if (parameter && !cJSON_AddNumberToObject(body.get(), parameter, value)) return false;
    } else if (c.kind == ControlKind::Mode && a.kind == ActionValue::String &&
        std::any_of(c.options.begin(), c.options.end(), [&](const ControlOption &o) { return o.value == a.text; })) {
        const char *parameter = nullptr;
        if (domain == "vacuum" && c.instance == "fan_speed") { service = "set_fan_speed"; parameter = "fan_speed"; }
        if (domain == "climate" && c.instance == "thermostat") { service = "set_hvac_mode"; parameter = "hvac_mode"; }
        if (domain == "climate" && c.instance == "fan_speed") { service = "set_fan_mode"; parameter = "fan_mode"; }
        if (parameter && !cJSON_AddStringToObject(body.get(), parameter, a.text)) return false;
    } else if (domain == "light" && c.kind == ControlKind::Rgb && a.kind == ActionValue::Number &&
        std::isfinite(a.number) && a.number >= 0 && a.number <= 16777215 && std::floor(a.number) == a.number) {
        const auto rgb = static_cast<unsigned>(a.number);
        const int components[] = {static_cast<int>((rgb >> 16) & 255), static_cast<int>((rgb >> 8) & 255), static_cast<int>(rgb & 255)};
        auto *array = cJSON_CreateIntArray(components, 3); if (!array) return false;
        if (!cJSON_AddItemToObject(body.get(), "rgb_color", array)) { cJSON_Delete(array); return false; }
        service = "turn_on";
    }
    if (service.empty()) return false;
    char *encoded = cJSON_PrintUnformatted(body.get()); if (!encoded) return false;
    json = encoded; cJSON_free(encoded);
    path = "/api/services/" + domain + "/" + service; return true;
}
