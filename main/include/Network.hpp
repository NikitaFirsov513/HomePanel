#pragma once
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

enum class NetworkPath { None, Ethernet, Wifi };
inline NetworkPath selectNetworkPath(bool ethernet_ip, bool wifi_ip) {
    return ethernet_ip ? NetworkPath::Ethernet : wifi_ip ? NetworkPath::Wifi : NetworkPath::None;
}
inline const char *networkPathName(NetworkPath path) {
    return path == NetworkPath::Ethernet ? "Провод" : path == NetworkPath::Wifi ? "Wi-Fi" : "Нет сети";
}
enum class WifiAction { Save, Disconnect, Scan };
enum class WifiSecurity { Open, Password, Unsupported };
enum class WifiScanState { Idle, Scanning, Ready, Error };
constexpr size_t WIFI_SCAN_LIMIT = 20;
struct WifiNetwork {
    char ssid[33]{};
    int8_t rssi = -127;
    uint8_t channel = 0;
    WifiSecurity security = WifiSecurity::Unsupported;
};
inline const char *wifiSecurityName(WifiSecurity security) {
    return security == WifiSecurity::Open ? "Открытая" : security == WifiSecurity::Password ? "Пароль" : "Не поддерживается";
}
struct WifiScanSnapshot {
    WifiScanState state = WifiScanState::Idle;
    uint32_t generation = 0;
    uint8_t count = 0;
    WifiNetwork networks[WIFI_SCAN_LIMIT]{};
    char message[160]{};
};
struct WifiCommand {
    WifiAction action = WifiAction::Save;
    char ssid[33]{};
    char password[65]{};
    bool open_network = false;
};
struct NetworkSnapshot {
    NetworkPath active = NetworkPath::None;
    bool ethernet_ready = false, wifi_ready = false, wifi_configured = false, wifi_open = false, busy = false;
    char ssid[33]{}, ethernet_ip[16]{}, wifi_ip[16]{}, message[192]{};
    char saved_password[65]{};
    uint32_t generation = 0;
    WifiScanSnapshot scan;
};
static_assert(std::is_trivially_copyable<WifiCommand>::value);
static_assert(std::is_trivially_copyable<NetworkSnapshot>::value);
struct WifiSettings { std::string ssid, password; };
bool validWifiSettings(const WifiSettings &settings);
bool resolveWifiSettings(const WifiCommand &command, const WifiSettings &current, WifiSettings &updated);
bool loadWifiSettings(WifiSettings &settings);
bool saveWifiSettings(const WifiSettings &settings);
std::vector<WifiNetwork> normalizeWifiNetworks(std::vector<WifiNetwork> networks);
