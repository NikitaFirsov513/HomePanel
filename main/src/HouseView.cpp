#include "HouseView.hpp"
#include "DeviceIcon.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>

// Keep draw commands in canvas coordinates. Panning only translates these
// commands; room sorting, opening segmentation and text measurement stay cached.
struct HouseScene {
    using Command = std::function<void(lv_layer_t *, int, int)>;
    std::vector<Command> commands;
    double offset_x = 0, offset_y = 0;
};

extern "C" { LV_FONT_DECLARE(font_roboto_20); }
namespace {
constexpr uint32_t INK = 0x293246, MUTED = 0x7C879B, ACCENT = 0x6854D9;
enum Op { Edit, Close, Rotate, ZoomOut, ZoomIn, Fit, Walls, FilterBase = 20, PickBase = 100, ClosePicker = 200 };
const char *levelNames[] = {"Снизу", "Середина", "Сверху"};
void surface(lv_obj_t *obj, uint32_t color, int radius = 14)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0); lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0); lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0); lv_obj_set_style_radius(obj, radius, 0); lv_obj_set_scrollable(obj, false);
}
lv_obj_t *label(lv_obj_t *parent, const char *text, uint32_t color = INK)
{
    auto *obj = lv_label_create(parent); lv_label_set_text(obj, text);
    lv_obj_set_width(obj, LV_PCT(100)); lv_obj_set_style_text_color(obj, lv_color_hex(color), 0); return obj;
}
struct Painter {
    HouseScene &scene;
    lv_area_t area;
    const HouseProjection &projection;
    void line(HousePoint a, HousePoint b, uint32_t color, int width = 1, int opacity = 255, bool dashed = false) const {
        lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
        d.p1.x = area.x1 + std::lround(a.x); d.p1.y = area.y1 + std::lround(a.y);
        d.p2.x = area.x1 + std::lround(b.x); d.p2.y = area.y1 + std::lround(b.y);
        d.color = lv_color_hex(color); d.width = width; d.opa = opacity; d.round_start = d.round_end = 1;
        if (dashed) { d.dash_width = 3; d.dash_gap = 5; }
        scene.commands.emplace_back([d](lv_layer_t *layer, int x, int y) mutable {
            auto moved = d;
            moved.p1.x += x; moved.p2.x += x; moved.p1.y += y; moved.p2.y += y;
            lv_draw_line(layer, &moved);
        });
    }
    void polygon(const std::array<HousePoint, 4> &p, uint32_t color, int opacity = 255) const {
        for (int i = 0; i < 2; ++i) {
            lv_draw_triangle_dsc_t d; lv_draw_triangle_dsc_init(&d);
            const int indices[] = {0, i + 1, i + 2};
            for (int j = 0; j < 3; ++j) {
                d.p[j].x = area.x1 + std::lround(p[indices[j]].x);
                d.p[j].y = area.y1 + std::lround(p[indices[j]].y);
            }
            d.color = lv_color_hex(color); d.opa = opacity;
            scene.commands.emplace_back([d](lv_layer_t *layer, int x, int y) mutable {
                auto moved = d;
                for (auto &point : moved.p) { point.x += x; point.y += y; }
                lv_draw_triangle(layer, &moved);
            });
        }
    }
    std::array<HousePoint, 4> quad(const std::array<HousePosition, 4> &points, uint32_t color, int opacity = 255) const {
        std::array<HousePoint, 4> p;
        for (int i = 0; i < 4; ++i) p[i] = projection.project(points[i]);
        polygon(p, color, opacity); return p;
    }
    void rect(double x, double y, int w, int h, uint32_t fill, int opacity = 255, int radius = 10,
              uint32_t border = 0, int border_width = 0) const {
        lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
        d.bg_color = lv_color_hex(fill); d.bg_opa = opacity; d.radius = radius;
        d.border_color = lv_color_hex(border); d.border_width = border_width;
        const int px = std::lround(x), py = std::lround(y);
        lv_area_t a = {area.x1 + px, area.y1 + py, area.x1 + px + w - 1, area.y1 + py + h - 1};
        scene.commands.emplace_back([d, a](lv_layer_t *layer, int x, int y) mutable {
            const lv_area_t moved = {a.x1 + x, a.y1 + y, a.x2 + x, a.y2 + y};
            lv_draw_rect(layer, &d, &moved);
        });
    }
    void text(double x, double y, int w, const std::string &text, uint32_t color = INK) const {
        lv_draw_label_dsc_t d; lv_draw_label_dsc_init(&d);
        d.text = text.c_str(); d.text_local = 1; d.font = &font_roboto_20; d.color = lv_color_hex(color);
        const int px = std::lround(x), py = std::lround(y);
        lv_area_t a = {area.x1 + px, area.y1 + py, area.x1 + px + w - 1, area.y1 + py + 24};
        scene.commands.emplace_back([d, a, text](lv_layer_t *layer, int x, int y) mutable {
            const lv_area_t moved = {a.x1 + x, a.y1 + y, a.x2 + x, a.y2 + y};
            d.text = text.c_str();
            lv_draw_label(layer, &d, &moved);
        });
    }
    void icon(DeviceIcon icon, lv_area_t a, uint32_t color) const {
        scene.commands.emplace_back([icon, a, color](lv_layer_t *layer, int x, int y) {
            const lv_area_t moved = {a.x1 + x, a.y1 + y, a.x2 + x, a.y2 + y};
            drawDeviceIcon(layer, icon, moved, lv_color_hex(color));
        });
    }
};
}
struct HouseView::Action { HouseView *view; int code; };
HouseView::HouseView() = default;
HouseView::~HouseView() { if (root_) lv_obj_delete(root_); }
void HouseView::setHandlers(void *context, DeviceHandler device, EditHandler edit)
{
    context_ = context; device_handler_ = device; edit_handler_ = edit;
}
lv_obj_t *HouseView::button(lv_obj_t *parent, const char *text, int action)
{
    auto *obj = lv_button_create(parent); surface(obj, 0xEFEDF8, 10);
    lv_obj_set_height(obj, 44); lv_obj_set_style_pad_hor(obj, 14, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xDDD6F5), LV_STATE_PRESSED);
    auto *caption = lv_label_create(obj); lv_label_set_text(caption, text); lv_obj_center(caption);
    auto *a = new Action{this, action};
    lv_obj_add_event_cb(obj, [](lv_event_t *event) {
        auto *a = static_cast<Action *>(lv_event_get_user_data(event));
        auto *view = a->view; const int code = a->code; view->perform(code);
    }, LV_EVENT_CLICKED, a);
    lv_obj_add_event_cb(obj, [](lv_event_t *event) { delete static_cast<Action *>(lv_event_get_user_data(event)); }, LV_EVENT_DELETE, a);
    return obj;
}
void HouseView::create(lv_obj_t *parent)
{
    root_ = lv_obj_create(parent); surface(root_, 0xF3F4F8, 0);
    lv_obj_set_style_text_font(root_, &font_roboto_20, 0); lv_obj_set_floating(root_, true);
    lv_obj_set_size(root_, lv_display_get_horizontal_resolution(lv_obj_get_display(parent)), lv_display_get_vertical_resolution(lv_obj_get_display(parent)));
    lv_obj_set_pos(root_, -lv_obj_get_style_pad_left(parent, LV_PART_MAIN), -lv_obj_get_style_pad_top(parent, LV_PART_MAIN));
    lv_obj_set_style_pad_all(root_, 16, 0); lv_obj_set_style_pad_row(root_, 10, 0); lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
    auto *header = lv_obj_create(root_); surface(header, 0xF3F4F8, 0); lv_obj_set_size(header, LV_PCT(100), 44);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(header, 12, 0);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    auto *title = label(header, "Мой дом"); lv_obj_set_width(title, 130);
    auto *subtitle = label(header, "Объёмный вид", MUTED); lv_obj_set_width(subtitle, 0); lv_obj_set_flex_grow(subtitle, 1);
    button(header, "Редактор 2D", Edit); button(header, "К списку", Close);
    auto *tools = lv_obj_create(root_); surface(tools, 0xF3F4F8, 0); lv_obj_set_size(tools, LV_PCT(100), 44);
    lv_obj_set_flex_flow(tools, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(tools, 7, 0);
    for (int i = 0; i < 4; ++i) filters_.push_back(button(tools, i == 0 ? "Все уровни" : levelNames[i - 1], FilterBase + i));
    walls_button_ = button(tools, "Стены: 27%", Walls);
    button(tools, "Поворот", Rotate); button(tools, "-", ZoomOut); button(tools, "+", ZoomIn); button(tools, "Вписать", Fit);
    canvas_ = lv_obj_create(root_); surface(canvas_, 0xF8F9FC, 20);
    lv_obj_set_width(canvas_, LV_PCT(100)); lv_obj_set_flex_grow(canvas_, 1);
    lv_obj_set_press_lock(canvas_, true); lv_obj_add_event_cb(canvas_, event, LV_EVENT_ALL, this);
    status_ = label(root_, "Нажмите на устройство для управления. Тяните дом, чтобы переместить вид.", MUTED);
    perform(FilterBase);
    lv_obj_update_layout(root_);
}
void HouseView::show(lv_obj_t *parent, const FloorPlan::Document &plan, const std::vector<Device> &devices)
{
    if (!root_) create(parent);
    update(plan, devices); lv_obj_set_hidden(root_, false); lv_obj_move_foreground(root_);
    lv_obj_update_layout(root_); fit();
}
void HouseView::update(const FloorPlan::Document &plan, const std::vector<Device> &devices)
{
    plan_ = plan; devices_ = devices;
    walls_ = houseWalls(plan_); apertures_.clear();
    for (const auto &wall : walls_) apertures_.push_back(wallApertures(wall, plan_));
    scene_dirty_ = true;
    if (canvas_) lv_obj_invalidate(canvas_);
}
void HouseView::hide()
{
    pressed_ = false;
    if (chooser_) { lv_obj_delete(chooser_); chooser_ = nullptr; }
    if (root_) lv_obj_set_hidden(root_, true);
}
const PlanRoom *HouseView::room(uint32_t id) const
{
    for (const auto &r : plan_.rooms) if (r.id == id) return &r;
    return nullptr;
}
const Device *HouseView::device(const std::string &id) const
{
    for (const auto &d : devices_) if ("yandex:" + d.id == id) return &d;
    return nullptr;
}
bool HouseView::visible(const PlanPlacement &p) const { return level_filter_ < 0 || level_filter_ == static_cast<int>(p.level); }
HousePoint HouseView::devicePoint(const std::string &id) const
{
    for (const auto &p : plan_.devices) if (p.device_id == id) {
        const auto *r = room(p.room_id); if (r) return projection_.project(HouseProjection::devicePosition(*r, p));
    }
    return {-10000, -10000};
}
void HouseView::fit()
{
    scene_dirty_ = true;
    projection_.fit(plan_, lv_obj_get_width(canvas_), lv_obj_get_height(canvas_)); lv_obj_invalidate(canvas_);
}
void HouseView::zoom(double factor)
{
    scene_dirty_ = true;
    const double next = std::clamp(projection_.scale * factor, 2.0, 90.0), ratio = next / projection_.scale;
    projection_.offset_x = lv_obj_get_width(canvas_) * 0.5 + (projection_.offset_x - lv_obj_get_width(canvas_) * 0.5) * ratio;
    projection_.offset_y = lv_obj_get_height(canvas_) * 0.5 + (projection_.offset_y - lv_obj_get_height(canvas_) * 0.5) * ratio;
    projection_.scale = next; lv_obj_invalidate(canvas_);
}
void HouseView::perform(int action)
{
    if (action == Edit) { hide(); if (edit_handler_) edit_handler_(context_); return; }
    if (action == Close) { hide(); return; }
    if (action == Rotate) { projection_.rotation = (projection_.rotation + 1) % 4; fit(); return; }
    if (action == ZoomIn || action == ZoomOut) { zoom(action == ZoomIn ? 1.2 : 1 / 1.2); return; }
    if (action == Fit) { fit(); return; }
    if (action == Walls) {
        scene_dirty_ = true;
        wall_opacity_ = wall_opacity_ == 70 ? 110 : wall_opacity_ == 110 ? 0 : 70;
        auto *caption = lv_obj_get_child(walls_button_, 0);
        lv_label_set_text_fmt(caption, "Стены: %d%%", wall_opacity_ * 100 / 255);
        lv_obj_invalidate(canvas_); return;
    }
    if (action >= FilterBase && action < FilterBase + 4) {
        scene_dirty_ = true;
        level_filter_ = action - FilterBase - 1;
        for (int i = 0; i < 4; ++i) {
            const bool active = i == action - FilterBase;
            lv_obj_set_style_bg_color(filters_[i], lv_color_hex(active ? ACCENT : 0xEFEDF8), 0);
            lv_obj_set_style_text_color(filters_[i], lv_color_hex(active ? 0xFFFFFF : ACCENT), 0);
        }
        lv_obj_invalidate(canvas_); return;
    }
    if (action == ClosePicker) { if (chooser_) { lv_obj_delete(chooser_); chooser_ = nullptr; } return; }
    if (action >= PickBase && action < PickBase + static_cast<int>(plan_.devices.size())) chooseDevice(action - PickBase);
}
void HouseView::chooseDevice(size_t index)
{
    if (index >= plan_.devices.size()) return;
    const auto p = plan_.devices[index];
    if (chooser_) { lv_obj_delete(chooser_); chooser_ = nullptr; }
    selected_ = p.device_id;
    scene_dirty_ = true;
    const auto *current = device(p.device_id);
    const auto *r = room(p.room_id);
    const std::string name = current ? current->name : p.name;
    lv_label_set_text_fmt(status_, "%s / %s / %s", name.c_str(), r ? r->name.c_str() : "", levelNames[static_cast<int>(p.level)]);
    lv_obj_invalidate(canvas_);
    if (current && device_handler_) device_handler_(context_, p.device_id);
    else lv_label_set_text(status_, "Устройство сохранено на плане, но не найдено в текущем списке Яндекса");
}
void HouseView::showChooser(const std::vector<size_t> &indices)
{
    if (chooser_) lv_obj_delete(chooser_);
    chooser_ = lv_obj_create(root_); surface(chooser_, 0xFFFFFF, 18); lv_obj_set_floating(chooser_, true);
    lv_obj_set_size(chooser_, 440, 340); lv_obj_center(chooser_);
    lv_obj_set_style_border_width(chooser_, 1, 0); lv_obj_set_style_border_color(chooser_, lv_color_hex(0xDADDE8), 0);
    lv_obj_set_style_pad_all(chooser_, 16, 0); lv_obj_set_style_pad_row(chooser_, 10, 0);
    lv_obj_set_flex_flow(chooser_, LV_FLEX_FLOW_COLUMN); lv_obj_set_scrollable(chooser_, true);
    label(chooser_, "Устройства рядом");
    for (const auto index : indices) {
        const auto &p = plan_.devices[index];
        auto *obj = button(chooser_, (p.name + " / " + levelNames[static_cast<int>(p.level)]).c_str(), PickBase + index);
        lv_obj_set_width(obj, LV_PCT(100));
    }
    button(chooser_, "Закрыть", ClosePicker);
}
void HouseView::pointerDown(int x, int y)
{
    if (chooser_) return;
    pressed_ = true; dragged_ = false; down_x_ = x; down_y_ = y;
    original_x_ = projection_.offset_x; original_y_ = projection_.offset_y;
}
void HouseView::pointerMove(int x, int y)
{
    if (!pressed_) return;
    if (std::abs(x - down_x_) + std::abs(y - down_y_) <= 8 && !dragged_) return;
    dragged_ = true;
    projection_.offset_x = std::clamp(original_x_ + x - down_x_, -2000.0, 2000.0);
    projection_.offset_y = std::clamp(original_y_ + y - down_y_, -2000.0, 2000.0);
    lv_obj_invalidate(canvas_);
}
void HouseView::pointerUp(int x, int y)
{
    if (!pressed_) return;
    pointerMove(x, y); pressed_ = false;
    if (dragged_) return;
    std::vector<std::pair<double, size_t>> hits;
    for (size_t i = 0; i < plan_.devices.size(); ++i) {
        const auto &p = plan_.devices[i]; if (!visible(p)) continue;
        const auto point = devicePoint(p.device_id);
        const double distance = (point.x - x) * (point.x - x) + (point.y - y) * (point.y - y);
        if (distance <= 26 * 26) hits.push_back({distance, i});
    }
    if (hits.empty()) return;
    std::sort(hits.begin(), hits.end());
    // Close markers get an explicit picker rather than an ambiguous command.
    if (hits.size() > 1 && hits[1].first - hits[0].first < 300) {
        std::vector<size_t> indices; for (const auto &hit : hits) indices.push_back(hit.second);
        showChooser(indices);
    } else chooseDevice(hits[0].second);
}
void HouseView::event(lv_event_t *event)
{
    auto *view = static_cast<HouseView *>(lv_event_get_user_data(event));
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_SIZE_CHANGED) { view->scene_dirty_ = true; return; }
    if (code == LV_EVENT_DRAW_MAIN) { view->draw(lv_event_get_layer(event)); return; }
    if (code == LV_EVENT_PRESS_LOST) { view->pressed_ = false; return; }
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED) return;
    const auto *input = lv_indev_active(); if (!input) return;
    lv_point_t p; lv_indev_get_point(input, &p); lv_area_t area; lv_obj_get_coords(view->canvas_, &area);
    const int x = p.x - area.x1, y = p.y - area.y1;
    if (code == LV_EVENT_PRESSED) view->pointerDown(x, y);
    else if (code == LV_EVENT_PRESSING) view->pointerMove(x, y);
    else view->pointerUp(x, y);
}

void HouseView::draw(lv_layer_t *layer)
{
    if (!scene_ || scene_dirty_) buildScene();
    lv_area_t area; lv_obj_get_coords(canvas_, &area);
    const int x = area.x1 + std::lround(projection_.offset_x - scene_->offset_x);
    const int y = area.y1 + std::lround(projection_.offset_y - scene_->offset_y);
    for (auto &command : scene_->commands) command(layer, x, y);
}

void HouseView::buildScene()
{
    if (!scene_) scene_ = std::make_unique<HouseScene>();
    scene_->commands.clear();
    scene_->offset_x = projection_.offset_x; scene_->offset_y = projection_.offset_y;
    scene_dirty_ = false;
    Painter paint{*scene_, {0, 0, 0, 0}, projection_};
    if (plan_.rooms.empty()) {
        paint.text(36, 60, lv_obj_get_width(canvas_) - 72, "Создайте план дома в редакторе 2D", ACCENT);
        paint.text(36, 100, lv_obj_get_width(canvas_) - 72, "Комнаты и уровни устройств появятся здесь автоматически", MUTED);
        return;
    }
    std::vector<const PlanRoom *> rooms;
    for (const auto &r : plan_.rooms) rooms.push_back(&r);
    std::sort(rooms.begin(), rooms.end(), [&](const PlanRoom *a, const PlanRoom *b) {
        return projection_.depth(a->rect.x + a->rect.w * 0.5, a->rect.y + a->rect.h * 0.5) <
               projection_.depth(b->rect.x + b->rect.w * 0.5, b->rect.y + b->rect.h * 0.5);
    });
    const uint32_t floor_colors[] = {0xEFECFA, 0xE5F2EF, 0xFCF0DB, 0xE6EEFA, 0xF8E8EF};
    for (const auto *r : rooms) {
        const double x = r->rect.x, y = r->rect.y, x1 = x + r->rect.w, y1 = y + r->rect.h;
        auto shadow = std::array<HousePoint, 4>{projection_.project({x, y, -0.2}), projection_.project({x1, y, -0.2}),
                                               projection_.project({x1, y1, -0.2}), projection_.project({x, y1, -0.2})};
        for (auto &point : shadow) { point.y += 9; point.x += 4; }
        paint.polygon(shadow, 0x7B88A4, 24);
        paint.quad({HousePosition{x, y1, -0.35}, {x1, y1, -0.35}, {x1, y1, 0}, {x, y1, 0}}, 0xC7CDDA);
        paint.quad({HousePosition{x1, y, -0.35}, {x1, y1, -0.35}, {x1, y1, 0}, {x1, y, 0}}, 0xC7CDDA);
        uint32_t floor = floor_colors[(r->id - 1) % 5];
        bool lit = false;
        for (const auto &p : plan_.devices) {
            const auto *d = device(p.device_id);
            if (p.room_id != r->id || !d || d->type.find("light") == std::string::npos) continue;
            for (const auto &c : d->controls) if (c.instance == "on" && c.known && c.boolean) lit = true;
        }
        if (lit) floor = 0xFFF1CC;
        const auto points = paint.quad({HousePosition{x, y, 0}, {x1, y, 0}, {x1, y1, 0}, {x, y1, 0}}, floor);
        for (int i = 0; i < 4; ++i) paint.line(points[i], points[(i + 1) % 4], 0xBCC5D7, 1);
        // Fine floor seams follow the room's own axes through camera rotation.
        for (double gx = x + 2; gx < x1; gx += 2)
            paint.line(projection_.project({gx, y, 0}), projection_.project({gx, y1, 0}), 0xFFFFFF, 1, 95);
        for (double gy = y + 2; gy < y1; gy += 2)
            paint.line(projection_.project({x, gy, 0}), projection_.project({x1, gy, 0}), 0xFFFFFF, 1, 95);
    }
    std::vector<size_t> order;
    for (size_t i = 0; i < walls_.size(); ++i) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const auto &wa = walls_[a]; const auto &wb = walls_[b];
        return projection_.depth((wa.x1 + wa.x2) * 0.5, (wa.y1 + wa.y2) * 0.5) <
               projection_.depth((wb.x1 + wb.x2) * 0.5, (wb.y1 + wb.y2) * 0.5);
    });
    for (const auto index : order) {
        if (!wall_opacity_) break;
        const auto &wall = walls_[index];
        const bool vertical = wall.x1 == wall.x2;
        const double length = vertical ? wall.y2 - wall.y1 : wall.x2 - wall.x1;
        auto position = [&](double along, double height) {
            return HousePosition{wall.x1 + (vertical ? 0 : along), wall.y1 + (vertical ? along : 0), height};
        };
        auto panel = [&](double from, double to, double bottom, double top, uint32_t fill, int opacity) {
            if (to - from < 0.001 || top <= bottom) return;
            paint.quad({position(from, bottom), position(to, bottom), position(to, top), position(from, top)}, fill, opacity);
        };
        std::vector<double> cuts{0, length};
        for (const auto &o : apertures_[index]) { cuts.push_back(o.from); cuts.push_back(o.to); }
        std::sort(cuts.begin(), cuts.end());
        cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
        for (size_t segment = 1; segment < cuts.size(); ++segment) {
            const double from = cuts[segment - 1], to = cuts[segment], middle = (from + to) * 0.5;
            const auto opening = std::find_if(apertures_[index].begin(), apertures_[index].end(),
                [&](const HouseAperture &o) { return middle > o.from && middle < o.to; });
            if (opening == apertures_[index].end()) panel(from, to, 0, HouseProjection::WALL_HEIGHT, 0xADC0DE, wall_opacity_);
            else if (opening->window) {
                panel(from, to, 0, 1.8, 0xADC0DE, wall_opacity_);
                panel(from, to, 4.3, HouseProjection::WALL_HEIGHT, 0xADC0DE, wall_opacity_);
                const auto window = paint.quad({position(from, 1.8), position(to, 1.8), position(to, 4.3), position(from, 4.3)}, 0x8BC4E4, wall_opacity_ / 2);
                for (int edge = 0; edge < 4; ++edge) paint.line(window[edge], window[(edge + 1) % 4], 0x73A4C8, 2, 160);
                paint.line(projection_.project(position(middle, 1.8)), projection_.project(position(middle, 4.3)), 0x73A4C8, 1, 150);
            } else {
                panel(from, to, 4.2, HouseProjection::WALL_HEIGHT, 0xADC0DE, wall_opacity_);
                paint.line(projection_.project(position(from, 0)), projection_.project(position(from, 4.2)), 0xB69B75, 2, 180);
                paint.line(projection_.project(position(to, 0)), projection_.project(position(to, 4.2)), 0xB69B75, 2, 180);
                paint.line(projection_.project(position(from, 4.2)), projection_.project(position(to, 4.2)), 0xB69B75, 2, 180);
            }
        }
        const auto a = projection_.project(position(0, HouseProjection::WALL_HEIGHT));
        const auto b = projection_.project(position(length, HouseProjection::WALL_HEIGHT));
        paint.line(a, b, 0xA4B3CE, 2, 190);
        paint.line(projection_.project(position(0, 0)), a, 0xA4B3CE, 1, 140);
        paint.line(projection_.project(position(length, 0)), b, 0xA4B3CE, 1, 140);
    }
    for (const auto *r : rooms) {
        const auto anchor = projection_.project({r->rect.x + r->rect.w * 0.5, r->rect.y + r->rect.h * 0.5, 0});
        lv_point_t size;
        lv_text_get_size(&size, r->name.c_str(), &font_roboto_20, 0, 0, 220, LV_TEXT_FLAG_NONE);
        const int width = std::min<int>(220, size.x);
        paint.rect(anchor.x - width * 0.5 - 8, anchor.y + 16, width + 16, 29, 0xFFFFFF, 205, 9);
        paint.text(anchor.x - width * 0.5, anchor.y + 18, width, r->name, 0x68758C);
    }
    std::vector<const PlanPlacement *> pins;
    for (const auto &p : plan_.devices) if (visible(p) && room(p.room_id)) pins.push_back(&p);
    std::stable_sort(pins.begin(), pins.end(), [&](const PlanPlacement *a, const PlanPlacement *b) {
        const auto pa = HouseProjection::devicePosition(*room(a->room_id), *a);
        const auto pb = HouseProjection::devicePosition(*room(b->room_id), *b);
        return projection_.depth(pa.x, pa.y) + pa.z * 0.2 < projection_.depth(pb.x, pb.y) + pb.z * 0.2;
    });
    const uint32_t tiers[] = {0x329A87, ACCENT, 0xC69332};
    for (const auto *p : pins) {
        const auto pos = HouseProjection::devicePosition(*room(p->room_id), *p);
        const auto anchor = projection_.project(pos), foot = projection_.project({pos.x, pos.y, 0});
        const uint32_t tier = tiers[static_cast<int>(p->level)];
        const auto *d = device(p->device_id);
        const bool selected = p->device_id == selected_;
        bool active = false;
        if (d) for (const auto &c : d->controls) if (c.instance == "on" && c.known && c.boolean) active = true;
        paint.rect(foot.x - 10, foot.y - 4, 20, 8, tier, 35, 6, tier, 1);
        if (foot.y - anchor.y > 22) paint.line({anchor.x, anchor.y + 19}, {foot.x, foot.y - 4}, tier, 1, 170, true);
        paint.rect(anchor.x - 21, anchor.y - 17, 44, 44, 0x8190AA, 25, 22);
        const bool light = p->type.find("light") != std::string::npos;
        const uint32_t background = active && light ? 0xFFF3D6 : selected ? 0xF1EDFF : 0xFFFFFF;
        paint.rect(anchor.x - 22, anchor.y - 22, 44, 44, background, 255, 22, selected ? ACCENT : 0xCFD7E5, selected ? 3 : 1);
        const uint32_t ink = !d ? 0xA1A9BA : light ? 0xB58A35 : ACCENT;
        lv_area_t icon = {static_cast<int>(std::lround(anchor.x)) - 18, static_cast<int>(std::lround(anchor.y)) - 18,
                         static_cast<int>(std::lround(anchor.x)) + 17, static_cast<int>(std::lround(anchor.y)) + 17};
        paint.icon(deviceIconForType(p->type), icon, ink);
        paint.rect(anchor.x + 11, anchor.y - 23, 12, 12, tier, 255, 6, 0xFFFFFF, 2);
        if (active) paint.rect(anchor.x + 12, anchor.y + 12, 9, 9, 0x36A37D, 255, 5, 0xFFFFFF, 1);
    }
}
