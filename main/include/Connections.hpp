#pragma once
#include "Device.hpp"
#include <string>
#include <type_traits>

enum class ConnectionAction { Save, Disconnect };
struct ConnectionCommand {
    ConnectionAction action = ConnectionAction::Save;
    char address[160]{};
    char token[1000]{};
};
struct ConnectionSnapshot {
    bool configured = false;
    bool connected = false;
    char address[160]{};
    char message[256]{};
    unsigned devices = 0;
};
static_assert(std::is_trivially_copyable<ConnectionCommand>::value);
static_assert(std::is_trivially_copyable<ConnectionSnapshot>::value);
struct HomeAssistantConfig { std::string address, token; };
bool normalizeHomeAssistantConfig(HomeAssistantConfig &config);
bool loadHomeAssistantConfig(HomeAssistantConfig &config);
bool saveHomeAssistantConfig(const HomeAssistantConfig &config);
