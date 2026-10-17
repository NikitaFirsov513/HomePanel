#include "DeviceIcon.hpp"
#include <cstdint>

DeviceIcon deviceIconForType(const std::string &type)
{
    if (type.find("light") != std::string::npos) return DeviceIcon::Bulb;
    if (type.find("vacuum_cleaner") != std::string::npos) return DeviceIcon::Vacuum;
    if (type.find("smart_speaker") != std::string::npos) return DeviceIcon::Speaker;
    if (type.find("socket") != std::string::npos || type.find("switch") != std::string::npos) return DeviceIcon::Socket;
    return DeviceIcon::Generic;
}

namespace {
void drawIcon(lv_event_t *event)
{
    auto *obj = static_cast<lv_obj_t *>(lv_event_get_target(event));
    auto *layer = lv_event_get_layer(event);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    const auto icon = static_cast<DeviceIcon>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    const auto color = lv_obj_get_style_text_color(obj, LV_PART_MAIN);
    // All paths use the same 32 x 32 design coordinates; no font or image assets.
    const int ox = area.x1 + (lv_obj_get_width(obj) - 32) / 2;
    const int oy = area.y1 + (lv_obj_get_height(obj) - 32) / 2;
    auto line = [&](int x1, int y1, int x2, int y2) {
        lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
        d.color = color; d.width = 2; d.round_start = 1; d.round_end = 1;
        d.p1.x = ox + x1; d.p1.y = oy + y1; d.p2.x = ox + x2; d.p2.y = oy + y2;
        lv_draw_line(layer, &d);
    };
    auto arc = [&](int x, int y, int radius, int start, int end) {
        lv_draw_arc_dsc_t d; lv_draw_arc_dsc_init(&d);
        d.color = color; d.width = 2; d.rounded = 1;
        d.center = {ox + x, oy + y}; d.radius = radius; d.start_angle = start; d.end_angle = end;
        lv_draw_arc(layer, &d);
    };
    auto rect = [&](int x, int y, int w, int h, int radius) {
        lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
        d.bg_opa = LV_OPA_TRANSP; d.border_color = color; d.border_width = 2; d.radius = radius;
        lv_area_t r = {ox + x, oy + y, ox + x + w - 1, oy + y + h - 1};
        lv_draw_rect(layer, &d, &r);
    };
    switch (icon) {
    case DeviceIcon::Bulb:
        arc(16, 12, 8, 145, 395); line(10, 17, 12, 23); line(22, 17, 20, 23);
        line(12, 23, 20, 23); line(12, 27, 20, 27); line(14, 30, 18, 30);
        line(16, 0, 16, 1); line(3, 5, 5, 7); line(27, 7, 29, 5);
        break;
    case DeviceIcon::Vacuum:
        arc(16, 17, 13, 0, 360); arc(16, 12, 4, 0, 360);
        line(7, 24, 25, 24); line(16, 26, 16, 27); break;
    case DeviceIcon::Speaker:
        rect(6, 2, 20, 28, 7); line(10, 9, 22, 9); arc(16, 21, 5, 0, 360);
        line(13, 5, 19, 5); break;
    case DeviceIcon::Socket:
        rect(3, 3, 26, 26, 8); arc(16, 16, 9, 0, 360);
        line(12, 14, 12, 17); line(20, 14, 20, 17); line(16, 23, 16, 25); break;
    case DeviceIcon::Power:
        arc(16, 17, 10, 310, 590); line(16, 3, 16, 15); break;
    case DeviceIcon::Close:
        line(9, 9, 23, 23); line(23, 9, 9, 23); break;
    case DeviceIcon::Refresh:
        arc(16, 16, 11, 60, 330); line(26, 5, 26, 12); line(19, 12, 26, 12); break;
    case DeviceIcon::Chevron:
        line(10, 13, 16, 19); line(16, 19, 22, 13); break;
    default:
        rect(5, 5, 22, 22, 6); line(11, 12, 21, 12); line(11, 19, 17, 19); break;
    }
}
}

lv_obj_t *createDeviceIcon(lv_obj_t *parent, DeviceIcon icon, uint32_t color, int size)
{
    auto *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, size, size);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_clickable(obj, false);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_add_event_cb(obj, drawIcon, LV_EVENT_DRAW_MAIN,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(icon)));
    return obj;
}
