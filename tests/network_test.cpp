#include "Network.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #c); std::exit(1); } } while(false)
int main() {
    struct Step { bool eth_ip, wifi_ip; NetworkPath selected; };
    const Step trace[] = {
        {false, false, NetworkPath::None}, {false, true, NetworkPath::Wifi},
        {true, true, NetworkPath::Ethernet}, {false, true, NetworkPath::Wifi},
        {false, false, NetworkPath::None}, {true, false, NetworkPath::Ethernet},
        {true, true, NetworkPath::Ethernet}, {true, false, NetworkPath::Ethernet},
    };
    for (const auto &step : trace) CHECK(selectNetworkPath(step.eth_ip, step.wifi_ip) == step.selected);
    CHECK(validWifiSettings({std::string(32, 'x'), std::string(63, 'p')}));
    CHECK(!validWifiSettings({std::string(33, 'x'), "password"}));
    CHECK(validWifiSettings({"Открытая сеть", ""}));
    CHECK(!validWifiSettings({"network", "short"}));
    CHECK(!validWifiSettings({"network", "password\r\n"}));
    CHECK(validWifiSettings({"network", std::string(64, 'a')}));
    CHECK(!validWifiSettings({"network", std::string(64, 'z')}));
    WifiSettings current{"home", "existing-password"}, updated;
    WifiCommand cmd;
    std::strcpy(cmd.ssid, "home");
    CHECK(resolveWifiSettings(cmd, current, updated) && updated.password == current.password);
    std::strcpy(cmd.ssid, "another"); CHECK(!resolveWifiSettings(cmd, current, updated));
    CHECK(updated.ssid == "home");
    cmd.open_network = true; CHECK(resolveWifiSettings(cmd, current, updated) && updated.password.empty());
    cmd.open_network = false; std::strcpy(cmd.password, "new-password");
    CHECK(resolveWifiSettings(cmd, current, updated) && updated.ssid == "another" && updated.password == "new-password");
    std::memset(cmd.ssid, 'x', sizeof(cmd.ssid)); CHECK(!resolveWifiSettings(cmd, current, updated));
    std::vector<WifiNetwork> raw;
    auto add = [&](const char *ssid, int rssi, WifiSecurity security = WifiSecurity::Password, int channel = 6) {
        WifiNetwork n; std::snprintf(n.ssid, sizeof(n.ssid), "%s", ssid); n.rssi = rssi; n.security = security; n.channel = channel; raw.push_back(n);
    };
    add("Home", -70); add("Home", -30); add("Guest", -55, WifiSecurity::Open);
    add("Home", -60, WifiSecurity::Open); add("", -10); add("bad\nname", -10); add("5GHz", -10, WifiSecurity::Password, 36);
    WifiNetwork invalid; std::memset(invalid.ssid, 'x', sizeof(invalid.ssid)); invalid.channel = 1; raw.push_back(invalid);
    const auto result = normalizeWifiNetworks(raw);
    CHECK(result.size() == 3 && result[0].rssi == -30 && !std::strcmp(result[0].ssid, "Home"));
    CHECK(result[1].security == WifiSecurity::Open && result[2].security == WifiSecurity::Open);
    raw.clear();
    for (int i = 0; i < 30; ++i) { const auto name = "network" + std::to_string(i); add(name.c_str(), -90 + i); }
    const auto limited = normalizeWifiNetworks(raw); CHECK(limited.size() == WIFI_SCAN_LIMIT);
    CHECK(limited.front().rssi == -61 && limited.back().rssi == -80);
    cmd.action = WifiAction::Scan; CHECK(!resolveWifiSettings(cmd, current, updated));
    std::puts("Network selection and Wi-Fi settings tests passed");
}
