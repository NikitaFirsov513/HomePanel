#pragma once
#include "Page.hpp"
#include "lvgl.h"
#include "DeviceManager.hpp"
class HomePage : public Page
{
public:
    void create() override;
    void create(DeviceManager *device_manager);
    void update(const std::vector<Device> &devices);

private:
    lv_obj_t *list_ = nullptr;
};
