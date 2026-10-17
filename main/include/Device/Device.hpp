#pragma once
#include <string>
#include <vector>
#include "DeviceOnOff.hpp"
#include "DeviceRange.hpp"
#include "DeviceMode.hpp"
#include "DeviceColorSetting.hpp"

enum class ControlKind { Toggle, Range, Mode, Rgb, Hsv };

struct ControlOption {
    std::string value;
};

struct DeviceControl {
    std::string type;
    std::string instance;
    ControlKind kind = ControlKind::Toggle;
    bool known = false;
    bool boolean = false;
    bool relative_only = false;
    double number = 0;
    double minimum = 0;
    double maximum = 100;
    double step = 1;
    std::string text;
    std::vector<ControlOption> options;
};

class Device
{
public:
    std::string id;
    std::string name;
    std::string type;
    std::string room;
    std::vector<DeviceControl> controls;
    std::vector<std::string> readings;

    DeviceOnOff Power;
    DeviceColorSetting ColorSetting;

    std::vector<DeviceMode> Modes;
    std::vector<DeviceRange> Ranges;

public:
    Device() = default;
    ~Device() = default;
};
