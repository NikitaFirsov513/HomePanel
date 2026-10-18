#pragma once
#include "Connections.hpp"
#include "DeviceAction.hpp"

// Transport-independent adapter. Only this layer knows HA entities and service names.
bool parseHomeAssistantStates(const char *json, size_t length, std::vector<Device> &devices);
bool buildHomeAssistantAction(const Device &device, const DeviceAction &action,
                              std::string &path, std::string &json);
