#pragma once
#include <cstdint>

enum class DeviceLoadState : uint8_t { Starting, NoAccounts, Offline, Loading, Ready, Error };

inline DeviceLoadState deviceLoadState(bool restored, bool configured, bool online,
                                     bool in_flight, bool loaded, bool failed) {
    if (!restored) return DeviceLoadState::Starting;
    if (!configured) return DeviceLoadState::NoAccounts;
    if (!online) return DeviceLoadState::Offline;
    if (in_flight) return DeviceLoadState::Loading;
    if (failed) return DeviceLoadState::Error;
    return loaded ? DeviceLoadState::Ready : DeviceLoadState::Loading;
}
