#include "Network.hpp"
#include <algorithm>
#include <cstring>
bool validWifiSettings(const WifiSettings &settings) {
    if (settings.ssid.empty() || settings.ssid.size() > 32 || settings.ssid.find_first_of("\r\n\t") != std::string::npos) return false;
    const auto size = settings.password.size();
    if (settings.password.find_first_of("\r\n") != std::string::npos) return false;
    if (!size || (size >= 8 && size <= 63)) return true;
    return size == 64 && std::all_of(settings.password.begin(), settings.password.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
}
bool resolveWifiSettings(const WifiCommand &command, const WifiSettings &current, WifiSettings &updated) {
    if (command.action != WifiAction::Save || !memchr(command.ssid, 0, sizeof(command.ssid)) ||
        !memchr(command.password, 0, sizeof(command.password))) return false;
    WifiSettings parsed{command.ssid, command.open_network ? "" : command.password};
    if (!command.open_network && parsed.password.empty()) {
        if (parsed.ssid != current.ssid || current.password.empty()) return false;
        parsed.password = current.password;
    }
    if (!validWifiSettings(parsed)) return false;
    updated = std::move(parsed); return true;
}

std::vector<WifiNetwork> normalizeWifiNetworks(std::vector<WifiNetwork> networks) {
    networks.erase(std::remove_if(networks.begin(), networks.end(), [](const WifiNetwork &network) {
        return !memchr(network.ssid, 0, sizeof(network.ssid)) || !network.ssid[0] ||
            std::strpbrk(network.ssid, "\r\n\t") || network.channel < 1 || network.channel > 14;
    }), networks.end());
    std::sort(networks.begin(), networks.end(), [](const WifiNetwork &a, const WifiNetwork &b) {
        return a.rssi != b.rssi ? a.rssi > b.rssi : std::strcmp(a.ssid, b.ssid) < 0;
    });
    std::vector<WifiNetwork> result;
    for (const auto &network : networks) {
        if (std::none_of(result.begin(), result.end(), [&](const WifiNetwork &old) {
            return old.security == network.security && !std::strcmp(old.ssid, network.ssid);
        })) result.push_back(network);
        if (result.size() == WIFI_SCAN_LIMIT) break;
    }
    return result;
}
