#include "HomePage.hpp"

LV_FONT_DECLARE(font_roboto_20);

void HomePage::create()
{
    if (root_) return;
    root_ = lv_obj_create(nullptr);
    list_ = lv_obj_create(root_);
    lv_obj_set_style_text_font(list_, &font_roboto_20, LV_PART_MAIN);
    lv_obj_set_size(list_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_, 15, 0);
    lv_obj_set_style_pad_all(list_, 20, 0);
    lv_obj_add_flag(list_, LV_OBJ_FLAG_SCROLLABLE);
}

void HomePage::create(DeviceManager *device_manager)
{
    if (!device_manager) return;
    std::vector<Device> devices;
    if (device_manager->getDevices(devices) != ESP_OK) return;
    update(devices);
}

void HomePage::update(const std::vector<Device> &devices)
{
    create();
    const int32_t scroll_y = lv_obj_get_scroll_y(list_);
    lv_obj_clean(list_);
    if (devices.empty()) {
        lv_label_set_text(lv_label_create(list_), "No devices");
        return;
    }
    for (const auto &device : devices) {
        lv_obj_t *label = lv_label_create(list_);
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_text_fmt(label, "ID: %s\nName: %s\nType: %s",
                             device.id.c_str(), device.name.c_str(), device.type.c_str());
    }
    lv_obj_update_layout(list_);
    lv_obj_scroll_to_y(list_, scroll_y, LV_ANIM_OFF);
}
