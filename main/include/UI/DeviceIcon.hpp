#pragma once
#include "lvgl.h"
#include <string>

enum class DeviceIcon { Bulb, Vacuum, Speaker, Socket, Generic, Power, Close, Refresh, Chevron };
DeviceIcon deviceIconForType(const std::string &type);
lv_obj_t *createDeviceIcon(lv_obj_t *parent, DeviceIcon icon, uint32_t color, int size = 44);
void drawDeviceIcon(lv_layer_t *layer, DeviceIcon icon, const lv_area_t &area, lv_color_t color);
