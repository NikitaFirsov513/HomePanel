#pragma once
#include <string>
#include <vector>
#include "DeviceOnOff.hpp"
#include "DeviceRange.hpp"
#include "DeviceMode.hpp"
#include "DeviceColorSetting.hpp"

class Device
{
public:
    std::string id;
    std::string name;
    std::string type;

    DeviceOnOff Power;
    DeviceColorSetting ColorSetting;

    std::vector<DeviceMode> Modes;
    std::vector<DeviceRange> Ranges;

public:
    Device() = default;
    ~Device() = default;
};
