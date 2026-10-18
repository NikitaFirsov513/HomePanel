#include "HomePage.hpp"
#include "DeviceIcon.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" { LV_FONT_DECLARE(font_roboto_20); }

namespace {
const char *title(const std::string &key)
{
    struct Label { const char *key; const char *text; };
    static const Label labels[] = {
        {"on", "Питание"}, {"brightness", "Яркость, %"}, {"temperature_k", "Температура света, K"},
        {"temperature", "Температура"}, {"volume", "Громкость"}, {"open", "Открытие, %"},
        {"pause", "Пауза"}, {"mute", "Без звука"}, {"backlight", "Подсветка"},
        {"cleanup_mode", "Режим уборки"}, {"work_speed", "Мощность / скорость"},
        {"fan_speed", "Скорость вентилятора"}, {"program", "Программа"},
        {"thermostat", "Режим климата"}, {"input_source", "Источник"},
        {"rgb", "Цвет"}, {"hsv", "Цвет"}, {"scene", "Световая сцена"},
        {"auto", "Авто"}, {"quiet", "Тихий"}, {"low", "Низкий"}, {"medium", "Средний"},
        {"high", "Высокий"}, {"turbo", "Турбо"}, {"eco", "Эко"}, {"normal", "Обычный"},
        {"smart", "Умный"}, {"predefined", "Заданный"}, {"express", "Быстрый"},
        {"full", "Полная уборка"}, {"wet_cleaning", "Влажная уборка"},
        {"dry_cleaning", "Сухая уборка"}, {"mixed_cleaning", "Сухая и влажная уборка"},
        {"battery_level", "Заряд, %"}, {"water_level", "Уровень воды"}
    };
    for (const auto &label : labels) if (key == label.key) return label.text;
    return key.c_str();
}

constexpr uint32_t INK = 0x202735;
constexpr uint32_t MUTED = 0x788191;
constexpr uint32_t ACCENT = 0x6854D9;

void surface(lv_obj_t *obj, uint32_t background, int radius = 18)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(background), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_scrollable(obj, false);
}
lv_obj_t *label(lv_obj_t *parent, const char *text)
{
    auto *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_width(obj, LV_PCT(100));
    lv_obj_set_style_text_color(obj, lv_color_hex(INK), 0);
    return obj;
}
lv_obj_t *button(lv_obj_t *parent, const char *text)
{
    auto *obj = lv_button_create(parent);
    surface(obj, 0xF0EDF9, 12);
    lv_obj_set_style_text_color(obj, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xE3DEF5), LV_STATE_PRESSED);
    lv_obj_set_style_pad_hor(obj, 16, 0);
    lv_obj_set_height(obj, 44);
    auto *caption = lv_label_create(obj);
    lv_label_set_text(caption, text);
    lv_obj_center(caption);
    return obj;
}
std::string summary(const Device &device)
{
    const auto power = std::find_if(device.controls.begin(), device.controls.end(),
        [](const DeviceControl &c) { return c.instance == "on"; });
    if (power == device.controls.end()) return device.controls.empty() ? "Только просмотр" : "Доступно управление";
    if (!power->known) return "Состояние неизвестно";
    const bool vacuum = device.type.find("vacuum_cleaner") != std::string::npos;
    std::string text = vacuum ? (power->boolean ? "Уборка" : "Остановлен") : (power->boolean ? "Включено" : "Выключено");
    const auto brightness = std::find_if(device.controls.begin(), device.controls.end(),
        [](const DeviceControl &c) { return c.instance == "brightness" && c.known; });
    if (brightness != device.controls.end() && power->boolean) {
        char number[32]; snprintf(number, sizeof(number), "  /  %g%%", brightness->number);
        text += number;
    }
    return text;
}
}

struct HomePage::Binding {
    HomePage *page = nullptr;
    DeviceAction action{};
    DeviceControl control;
    lv_obj_t *caption = nullptr;
    // Sliders send only when released; dropdown index 0 is a prompt.
    bool slider = false;
    bool dropdown = false;
    bool changed = false;
    bool expand = false;
};

void HomePage::bind(lv_obj_t *widget, Binding *binding, lv_event_code_t code)
{
    binding->page = this;
    widgets_.push_back(widget);
    lv_obj_add_event_cb(widget, controlEvent, code, binding);
    if (binding->slider) lv_obj_add_event_cb(widget, controlEvent, LV_EVENT_VALUE_CHANGED, binding);
    lv_obj_add_event_cb(widget, [](lv_event_t *event) {
        delete static_cast<Binding *>(lv_event_get_user_data(event));
    }, LV_EVENT_DELETE, binding);
}

void HomePage::setStatus(const char *text, bool error, bool finished)
{
    create();
    lv_label_set_text(status_, text);
    lv_obj_set_style_text_color(status_, lv_color_hex(error ? 0xBC3E4B : MUTED), 0);
    lv_label_set_text(detail_status_, text);
    lv_obj_set_style_text_color(detail_status_, lv_color_hex(error ? 0xBC3E4B : MUTED), 0);
    editor_.house().setStatus(text, error);
    if (finished) {
        busy_ = false;
        for (auto *obj : widgets_) lv_obj_set_disabled(obj, false);
        if (close_) lv_obj_set_disabled(close_, false);
    }
}

void HomePage::submit(const DeviceAction &action)
{
    if (busy_) return;
    if (!handler_ || !handler_(action)) {
        setStatus("Не удалось отправить команду. Повторите попытку.", true);
        return;
    }
    busy_ = true;
    setStatus(action.kind == ActionValue::Refresh ? "Обновление устройств..." : "Отправка команды...", false, false);
    for (auto *obj : widgets_) lv_obj_set_disabled(obj, true);
    if (close_) lv_obj_set_disabled(close_, true);
}

void HomePage::controlEvent(lv_event_t *event)
{
    auto *b = static_cast<Binding *>(lv_event_get_user_data(event));
    if (b->page->busy_) return;
    if (b->expand) { b->page->toggleDevice(b->action.device_id); return; }
    auto *obj = static_cast<lv_obj_t *>(lv_event_get_target(event));
    if (b->slider) {
        const double value = b->control.minimum + lv_slider_get_value(obj) * b->control.step;
        lv_label_set_text_fmt(b->caption, "%s: %g", title(b->control.instance), value);
        if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) { b->changed = true; return; }
        if (!b->changed) return;
        b->changed = false;
        b->action.number = value;
    } else if (b->dropdown) {
        const auto selected = lv_dropdown_get_selected(obj);
        if (!selected) return;
        const auto index = selected - 1;
        if (index >= b->control.options.size()) return;
        const auto &value = b->control.options[index].value;
        if (value.size() >= sizeof(b->action.text)) return;
        snprintf(b->action.text, sizeof(b->action.text), "%s", value.c_str());
    }
    b->page->submit(b->action);
}


void HomePage::closeDetails()
{
    if (busy_) return;
    expanded_id_.clear();
    widgets_.resize(card_widget_count_);
    lv_obj_clean(details_);
    lv_obj_set_hidden(overlay_, true);
}

void HomePage::toggleDevice(const std::string &id)
{
    if (busy_) return;
    const auto it = std::find_if(devices_.begin(), devices_.end(), [&](const Device &d) { return d.id == id; });
    if (it != devices_.end()) showDetails(*it);
}

void HomePage::showDetails(const Device &device)
{
    widgets_.resize(card_widget_count_);
    lv_obj_clean(details_);
    if (expanded_id_ != device.id) {
        lv_label_set_text(detail_status_, "Изменения применяются сразу");
        lv_obj_set_style_text_color(detail_status_, lv_color_hex(MUTED), 0);
    }
    expanded_id_ = device.id;
    lv_label_set_text(detail_title_, device.name.c_str());
    if (!device.room.empty()) {
        auto *room = label(details_, device.room.c_str());
        lv_obj_set_style_text_color(room, lv_color_hex(MUTED), 0);
    }
    for (const auto &reading : device.readings) {
        auto text = reading;
        const auto colon = text.find(':');
        if (colon != std::string::npos) text = std::string(title(text.substr(0, colon))) + text.substr(colon);
        label(details_, text.c_str());
    }
    if (device.controls.empty()) {
        label(details_, "Для этого устройства управление недоступно");
    } else createControls(device, details_);
    lv_obj_scroll_to_y(details_, 0, LV_ANIM_OFF);
    lv_obj_set_hidden(overlay_, false);
    lv_obj_move_foreground(overlay_);
}

void HomePage::create()
{
    if (root_) return;
    root_ = lv_obj_create(nullptr);
    surface(root_, 0xF3F4F7, 0);
    lv_obj_set_style_text_font(root_, &font_roboto_20, 0);
    lv_obj_set_style_text_color(root_, lv_color_hex(INK), 0);
    lv_obj_set_style_pad_all(root_, 20, 0);
    lv_obj_set_style_pad_row(root_, 12, 0);
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
    auto *header = lv_obj_create(root_);
    surface(header, 0xF3F4F7, 0);
    lv_obj_set_size(header, LV_PCT(100), 52);
    auto *heading = label(header, "Устройства");
    lv_obj_set_width(heading, 150);
    lv_obj_align(heading, LV_ALIGN_LEFT_MID, 0, 0);
    count_ = label(header, "");
    lv_obj_set_style_text_color(count_, lv_color_hex(MUTED), 0);
    lv_obj_set_size(count_, 300, LV_SIZE_CONTENT);
    lv_obj_align(count_, LV_ALIGN_LEFT_MID, 154, 0);
    auto *refresh = button(header, "Обновить");
    lv_obj_set_width(refresh, 148);
    lv_obj_align(refresh, LV_ALIGN_RIGHT_MID, 0, 0);
    auto *refresh_binding = new Binding;
    refresh_binding->action.kind = ActionValue::Refresh;
    bind(refresh, refresh_binding, LV_EVENT_CLICKED);
    auto *plan = button(header, "План дома");
    lv_obj_set_width(plan, 152);
    lv_obj_align(plan, LV_ALIGN_RIGHT_MID, -160, 0);
    lv_obj_set_width(count_, 230);
    lv_obj_add_event_cb(plan, [](lv_event_t *event) {
        auto *page = static_cast<HomePage *>(lv_event_get_user_data(event));
        if (!page->busy_) page->editor_.show(page->root_);
    }, LV_EVENT_CLICKED, this);
    editor_.setDeviceHandler([](void *context, const std::string &id) {
        auto *page = static_cast<HomePage *>(context);
        if (id.rfind("yandex:", 0) == 0) page->toggleDevice(id.substr(7));
    }, this);
    auto *house = button(header, "Дом");
    lv_obj_set_width(house, 112);
    lv_obj_align(house, LV_ALIGN_RIGHT_MID, -320, 0);
    lv_obj_add_event_cb(house, [](lv_event_t *event) {
        auto *page = static_cast<HomePage *>(lv_event_get_user_data(event));
        if (!page->busy_) page->editor_.showHouse(page->root_);
    }, LV_EVENT_CLICKED, this);
    status_ = label(root_, "Устройства готовы к управлению");
    lv_obj_set_style_text_color(status_, lv_color_hex(MUTED), 0);
    lv_label_set_long_mode(status_, LV_LABEL_LONG_WRAP);
    list_ = lv_obj_create(root_);
    surface(list_, 0xF3F4F7, 0);
    lv_obj_set_width(list_, LV_PCT(100));
    lv_obj_set_flex_grow(list_, 1);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(list_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(list_, 12, 0);
    lv_obj_set_style_pad_column(list_, 12, 0);
    lv_obj_set_scrollable(list_, true);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_style_width(list_, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(list_, lv_color_hex(0xC8CAD5), LV_PART_SCROLLBAR);

    overlay_ = lv_obj_create(root_);
    surface(overlay_, 0x111827, 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_30, 0);
    lv_obj_set_floating(overlay_, true);
    const int screen_width = lv_display_get_horizontal_resolution(lv_obj_get_display(root_));
    const int screen_height = lv_display_get_vertical_resolution(lv_obj_get_display(root_));
    lv_obj_set_size(overlay_, screen_width, screen_height);
    // Floating objects are positioned relative to the parent's content origin.
    lv_obj_set_pos(overlay_, -20, -20);
    lv_obj_add_event_cb(overlay_, [](lv_event_t *event) {
        if (lv_event_get_target(event) == lv_event_get_current_target(event))
            static_cast<HomePage *>(lv_event_get_user_data(event))->closeDetails();
    }, LV_EVENT_CLICKED, this);
    auto *panel = lv_obj_create(overlay_);
    surface(panel, 0xFFFFFF, 22);
    lv_obj_set_size(panel, LV_PCT(screen_width >= 700 ? 48 : 96), LV_PCT(96));
    lv_obj_align(panel, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_set_style_pad_all(panel, 20, 0);
    lv_obj_set_style_pad_row(panel, 20, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    auto *top = lv_obj_create(panel);
    surface(top, 0xFFFFFF, 0);
    lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top, 12, 0);
    detail_title_ = label(top, "");
    lv_obj_set_width(detail_title_, 0);
    lv_obj_set_flex_grow(detail_title_, 1);
    lv_label_set_long_mode(detail_title_, LV_LABEL_LONG_WRAP);
    close_ = button(top, "Готово");
    lv_obj_add_event_cb(close_, [](lv_event_t *event) {
        static_cast<HomePage *>(lv_event_get_user_data(event))->closeDetails();
    }, LV_EVENT_CLICKED, this);
    detail_status_ = label(panel, "Изменения применяются сразу");
    lv_obj_set_style_text_color(detail_status_, lv_color_hex(MUTED), 0);
    details_ = lv_obj_create(panel);
    surface(details_, 0xFFFFFF, 0);
    lv_obj_set_width(details_, LV_PCT(100));
    lv_obj_set_flex_grow(details_, 1);
    lv_obj_set_style_pad_right(details_, 10, 0);
    lv_obj_set_style_pad_row(details_, 16, 0);
    lv_obj_set_flex_flow(details_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(details_, true);
    lv_obj_set_scroll_dir(details_, LV_DIR_VER);
    lv_obj_set_hidden(overlay_, true);
    // Open the saved house immediately, including before network/device loading.
    // Subsequent model updates preserve whichever section the user opened.
    editor_.showHouse(root_);
}

void HomePage::create(DeviceManager *device_manager)
{
    if (!device_manager) return;
    std::vector<Device> devices;
    if (device_manager->getDevices(devices) == ESP_OK) update(devices);
}

void HomePage::update(const std::vector<Device> &devices)
{
    create();
    if (busy_) return;
    for (auto *obj : widgets_) {
        if (lv_obj_has_state(obj, LV_STATE_PRESSED)) return;
        if (lv_obj_check_type(obj, &lv_dropdown_class) && lv_dropdown_is_open(obj)) return;
    }
    const int32_t scroll_y = lv_obj_get_scroll_y(list_);
    const int32_t detail_scroll_y = lv_obj_get_scroll_y(details_);
    widgets_.resize(1);
    lv_obj_clean(details_);
    lv_obj_clean(list_);
    devices_ = devices;
    editor_.setDevices(devices_);
    lv_label_set_text_fmt(count_, "Устройств: %u", static_cast<unsigned>(devices.size()));
    lv_obj_update_layout(root_);
    const int width = lv_obj_get_content_width(list_);
    const int card_width = width >= 700 ? (width - 12) / 2 : width;
    if (devices.empty()) label(list_, "Добавьте устройства в Дом с Алисой");
    for (const auto &device : devices) {
        auto *card = lv_obj_create(list_);
        surface(card, 0xFFFFFF);
        lv_obj_set_size(card, card_width, 132);
        lv_obj_set_style_bg_color(card, lv_color_hex(0xEBE8F5), LV_STATE_PRESSED);
        auto make = [&]() {
            auto *b = new Binding;
            snprintf(b->action.device_id, sizeof(b->action.device_id), "%s", device.id.c_str());
            return b;
        };
        if (device.id.size() < sizeof(DeviceAction::device_id)) {
            auto *b = make(); b->expand = true;
            bind(card, b, LV_EVENT_CLICKED);
        }
        const auto icon = deviceIconForType(device.type);
        const uint32_t icon_color = icon == DeviceIcon::Bulb ? 0xB8862E : icon == DeviceIcon::Vacuum ? 0x2F8B83 : ACCENT;
        const uint32_t icon_bg = icon == DeviceIcon::Bulb ? 0xFFF4DC : icon == DeviceIcon::Vacuum ? 0xE5F5F1 : 0xF0ECFC;
        auto *tile = lv_obj_create(card);
        surface(tile, icon_bg, 14);
        lv_obj_set_size(tile, 48, 48);
        lv_obj_set_pos(tile, 16, 16);
        lv_obj_set_clickable(tile, false);
        lv_obj_center(createDeviceIcon(tile, icon, icon_color));
        auto *name = label(card, device.name.c_str());
        lv_obj_set_pos(name, 78, 18);
        lv_obj_set_width(name, card_width - 156);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        auto *room = label(card, device.room.empty() ? "Без комнаты" : device.room.c_str());
        lv_obj_set_style_text_color(room, lv_color_hex(MUTED), 0);
        lv_obj_set_pos(room, 78, 48);
        lv_obj_set_width(room, card_width - 156);
        lv_label_set_long_mode(room, LV_LABEL_LONG_DOT);
        const auto power = std::find_if(device.controls.begin(), device.controls.end(),
            [](const DeviceControl &c) { return c.kind == ControlKind::Toggle && c.instance == "on"; });
        const bool active = power != device.controls.end() && power->known && power->boolean;
        auto *dot = lv_obj_create(card);
        surface(dot, active ? 0x3D9F7B : 0xBEC3CF, LV_RADIUS_CIRCLE);
        lv_obj_set_size(dot, 7, 7);
        lv_obj_set_pos(dot, 18, 98);
        lv_obj_set_clickable(dot, false);
        auto *state = label(card, summary(device).c_str());
        lv_obj_set_style_text_color(state, lv_color_hex(active ? 0x2D8669 : MUTED), 0);
        lv_obj_set_pos(state, 34, 89);
        lv_obj_set_width(state, card_width - 55);
        lv_label_set_long_mode(state, LV_LABEL_LONG_DOT);
        if (power != device.controls.end() && device.id.size() < sizeof(DeviceAction::device_id)) {
            auto *quick = lv_button_create(card);
            surface(quick, active ? 0xE3F3EC : 0xF2F3F7, LV_RADIUS_CIRCLE);
            lv_obj_set_size(quick, 48, 48);
            lv_obj_align(quick, LV_ALIGN_TOP_RIGHT, -16, 16);
            lv_obj_center(createDeviceIcon(quick, DeviceIcon::Power, active ? 0x2D8669 : 0x7C8392));
            auto *b = make();
            snprintf(b->action.type, sizeof(b->action.type), "%s", power->type.c_str());
            snprintf(b->action.instance, sizeof(b->action.instance), "%s", power->instance.c_str());
            b->action.boolean = !active;
            bind(quick, b, LV_EVENT_CLICKED);
        }
    }
    card_widget_count_ = widgets_.size();
    const auto selected = std::find_if(devices_.begin(), devices_.end(),
        [&](const Device &d) { return d.id == expanded_id_; });
    if (selected != devices_.end()) {
        showDetails(*selected);
        lv_obj_update_layout(details_);
        lv_obj_scroll_to_y(details_, detail_scroll_y, LV_ANIM_OFF);
    } else {
        expanded_id_.clear();
        lv_obj_set_hidden(overlay_, true);
    }
    lv_obj_update_layout(list_);
    lv_obj_scroll_to_y(list_, scroll_y, LV_ANIM_OFF);
}

void HomePage::createControls(const Device &device, lv_obj_t *body)
{
    for (const auto &c : device.controls) {
        if (device.id.size() >= sizeof(DeviceAction::device_id) ||
            c.type.size() >= sizeof(DeviceAction::type) || c.instance.size() >= sizeof(DeviceAction::instance)) continue;
        auto make = [&]() {
            auto *b = new Binding;
            b->control = c;
            snprintf(b->action.device_id, sizeof(b->action.device_id), "%s", device.id.c_str());
            snprintf(b->action.type, sizeof(b->action.type), "%s", c.type.c_str());
            snprintf(b->action.instance, sizeof(b->action.instance), "%s", c.instance.c_str());
            return b;
        };
        if (c.kind == ControlKind::Toggle) {
            const std::string state = c.known ? (c.boolean ? "Вкл." : "Выкл.") : "неизвестно";
            label(body, (std::string(title(c.instance)) + ": " + state).c_str());
            auto *row = lv_obj_create(body);
            surface(row, 0xFFFFFF, 0);
            lv_obj_set_style_pad_column(row, 8, 0);
            lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            for (int on = 0; on <= 1; ++on) {
                const bool vacuum = device.type.find("vacuum_cleaner") != std::string::npos && c.instance == "on";
                const char *text = c.instance == "pause" ? (on ? "Пауза" : "Продолжить") :
                    vacuum ? (on ? "Начать уборку" : "Остановить") : (on ? "Включить" : "Выключить");
                auto *obj = button(row, text);
                lv_obj_set_flex_grow(obj, 1);
                if (c.known && c.boolean == bool(on)) {
                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xD9EFE5), 0);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0x2D8669), 0);
                }
                auto *b = make(); b->action.boolean = on;
                bind(obj, b, LV_EVENT_CLICKED);
            }
        } else if (c.kind == ControlKind::Range) {
            auto *caption = label(body, "");
            if (c.known) lv_label_set_text_fmt(caption, "%s: %g", title(c.instance), c.number);
            else lv_label_set_text_fmt(caption, "%s: неизвестно", title(c.instance));
            if (c.relative_only) {
                auto *row = lv_obj_create(body);
                surface(row, 0xFFFFFF, 0);
                lv_obj_set_style_pad_column(row, 8, 0);
                lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
                lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
                for (int sign : {-1, 1}) {
                    auto *obj = button(row, sign < 0 ? "Уменьшить" : "Увеличить");
                    auto *b = make(); b->action.kind = ActionValue::Number;
                    b->action.relative = true; b->action.number = sign * c.step;
                    bind(obj, b, LV_EVENT_CLICKED);
                }
            } else {
                auto *obj = lv_slider_create(body);
                lv_obj_set_width(obj, LV_PCT(94));
                lv_obj_set_style_margin_all(obj, 10, 0);
                lv_obj_set_height(obj, 8);
                lv_obj_set_style_bg_color(obj, lv_color_hex(0xEAE7F5), LV_PART_MAIN);
                lv_obj_set_style_bg_color(obj, lv_color_hex(ACCENT), LV_PART_INDICATOR);
                lv_obj_set_style_bg_color(obj, lv_color_hex(ACCENT), LV_PART_KNOB);
                lv_obj_set_style_pad_all(obj, 5, LV_PART_KNOB);
                const auto ticks = static_cast<int32_t>(std::floor((c.maximum - c.minimum) / c.step + 1e-7));
                lv_slider_set_range(obj, 0, std::max<int32_t>(1, ticks));
                lv_slider_set_value(obj, std::clamp<int32_t>(static_cast<int32_t>(std::round((c.number - c.minimum) / c.step)), 0, ticks), LV_ANIM_OFF);
                auto *b = make(); b->slider = true; b->caption = caption; b->action.kind = ActionValue::Number;
                bind(obj, b, LV_EVENT_RELEASED);
            }
        } else if (c.kind == ControlKind::Rgb || c.kind == ControlKind::Hsv) {
            label(body, "Цвет");
            auto *row = lv_obj_create(body);
            surface(row, 0xFFFFFF, 0);
            lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
            lv_obj_set_style_pad_column(row, 8, 0);
            lv_obj_set_style_pad_row(row, 8, 0);
            static const uint32_t colors[] = {0xFF0000, 0xFF8000, 0xFFFF00, 0x00FF00, 0x00FFFF, 0x0000FF, 0x8000FF, 0xFF00FF};
            static const double hues[] = {0, 30, 60, 120, 180, 240, 270, 300};
            for (size_t i = 0; i < 8; ++i) {
                auto *swatch = lv_button_create(row);
                surface(swatch, colors[i], LV_RADIUS_CIRCLE);
                lv_obj_set_size(swatch, 40, 40);
                lv_obj_set_style_border_width(swatch, 3, 0);
                lv_obj_set_style_border_color(swatch, lv_color_hex(0xF0F1F5), 0);
                if (c.kind == ControlKind::Rgb && c.known && c.number == colors[i]) {
                    lv_obj_set_style_outline_width(swatch, 2, 0);
                    lv_obj_set_style_outline_color(swatch, lv_color_hex(INK), 0);
                }
                auto *b = make();
                b->action.kind = c.kind == ControlKind::Rgb ? ActionValue::Number : ActionValue::Hsv;
                b->action.number = c.kind == ControlKind::Rgb ? colors[i] : hues[i];
                bind(swatch, b, LV_EVENT_CLICKED);
            }
        } else {
            label(body, title(c.instance));
            auto *obj = lv_dropdown_create(body);
            lv_obj_set_width(obj, LV_PCT(100));
            std::string options = "Выберите...";
            uint32_t selected = 0;
            for (size_t i = 0; i < c.options.size(); ++i) {
                options += "\n"; options += title(c.options[i].value);
                if (c.known && c.text == c.options[i].value) selected = i + 1;
            }
            lv_dropdown_set_options(obj, options.c_str());
            surface(obj, 0xF4F3F8, 12);
            lv_obj_set_height(obj, 48);
            lv_obj_set_style_pad_all(obj, 10, 0);
            lv_obj_set_style_pad_right(obj, 40, 0);
            lv_dropdown_set_symbol(obj, nullptr);
            lv_obj_align(createDeviceIcon(obj, DeviceIcon::Chevron, MUTED, 32), LV_ALIGN_RIGHT_MID, 0, 0);
            lv_dropdown_set_selected(obj, selected);
            auto *b = make(); b->dropdown = true;
            b->action.kind = ActionValue::String;
            bind(obj, b, LV_EVENT_VALUE_CHANGED);
            lv_obj_set_style_text_font(lv_dropdown_get_list(obj), &font_roboto_20, 0);
        }
    }
}
