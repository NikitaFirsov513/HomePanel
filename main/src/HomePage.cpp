#include "HomePage.hpp"
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

lv_obj_t *label(lv_obj_t *parent, const char *text)
{
    auto *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_width(obj, LV_PCT(100));
    return obj;
}
lv_obj_t *button(lv_obj_t *parent, const char *text)
{
    auto *obj = lv_button_create(parent);
    lv_obj_set_height(obj, 52);
    auto *caption = lv_label_create(obj);
    lv_label_set_text(caption, text);
    lv_obj_center(caption);
    return obj;
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
    lv_obj_set_style_text_color(status_, lv_color_hex(error ? 0xB42318 : 0x344054), 0);
    if (finished) {
        busy_ = false;
        for (auto *obj : widgets_) lv_obj_set_disabled(obj, false);
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
        if (b->control.kind == ControlKind::Mode) {
            if (index >= b->control.options.size()) return;
            const auto &value = b->control.options[index].value;
            if (value.size() >= sizeof(b->action.text)) return;
            snprintf(b->action.text, sizeof(b->action.text), "%s", value.c_str());
        } else {
            static const uint32_t colors[] = {0xFF0000, 0xFF8000, 0xFFFF00, 0x00FF00, 0x00FFFF, 0x0000FF, 0x8000FF, 0xFF00FF};
            static const double hues[] = {0, 30, 60, 120, 180, 240, 270, 300};
            if (index >= sizeof(colors) / sizeof(colors[0])) return;
            b->action.number = b->control.kind == ControlKind::Rgb ? colors[index] : hues[index];
        }
    }
    b->page->submit(b->action);
}

void HomePage::toggleDevice(const std::string &id)
{
    expanded_id_ = expanded_id_ == id ? "" : id;
    for (const auto &panel : panels_) {
        lv_obj_set_hidden(panel.second, panel.first != expanded_id_);
    }
}

void HomePage::create()
{
    if (root_) return;
    root_ = lv_obj_create(nullptr);
    lv_obj_set_style_text_font(root_, &font_roboto_20, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0xF2F4F7), 0);
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root_, 16, 0);
    lv_obj_set_scrollable(root_, false);
    auto *header = lv_obj_create(root_);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    auto *heading = lv_label_create(header);
    lv_label_set_text(heading, "Мой дом");
    auto *refresh = button(header, "Обновить");
    auto *refresh_binding = new Binding;
    refresh_binding->action.kind = ActionValue::Refresh;
    bind(refresh, refresh_binding, LV_EVENT_CLICKED);
    status_ = label(root_, "Устройства готовы к управлению");
    list_ = lv_obj_create(root_);
    lv_obj_set_width(list_, LV_PCT(100));
    lv_obj_set_flex_grow(list_, 1);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_, 16, 0);
    lv_obj_set_style_border_width(list_, 0, 0);
    lv_obj_set_style_bg_opa(list_, LV_OPA_TRANSP, 0);
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
    // Do not tear down a dropdown or a slider while the user is using it.
    for (auto *obj : widgets_) {
        if (lv_obj_has_state(obj, LV_STATE_PRESSED)) return;
        if (lv_obj_check_type(obj, &lv_dropdown_class) && lv_dropdown_is_open(obj)) return;
    }
    const int32_t scroll_y = lv_obj_get_scroll_y(list_);
    widgets_.resize(1); // The refresh button lives outside the list.
    const bool first_render = panels_.empty() && lv_obj_get_child_count(list_) == 0;
    panels_.clear();
    lv_obj_clean(list_);
    if (devices.empty()) { label(list_, "Устройств пока нет"); return; }
    if (first_render) {
        for (const auto &device : devices)
            if (!device.controls.empty()) { expanded_id_ = device.id; break; }
    }
    for (const auto &device : devices) {
        auto *card = lv_obj_create(list_);
        lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(card, 18, 0);
        lv_obj_set_style_pad_row(card, 14, 0);
        if (!device.controls.empty() && device.id.size() < sizeof(DeviceAction::device_id)) {
            auto *heading = button(card, (device.name + "  /  Управление").c_str());
            lv_obj_set_width(heading, LV_PCT(100));
            auto *b = new Binding;
            b->expand = true;
            snprintf(b->action.device_id, sizeof(b->action.device_id), "%s", device.id.c_str());
            bind(heading, b, LV_EVENT_CLICKED);
        } else label(card, device.name.c_str());
        if (!device.room.empty()) label(card, device.room.c_str());
        for (const auto &reading : device.readings) {
            auto text = reading;
            const auto colon = text.find(':');
            if (colon != std::string::npos) text = std::string(title(text.substr(0, colon))) + text.substr(colon);
            label(card, text.c_str());
        }
        if (device.controls.empty()) {
            label(card, device.type.find("smart_speaker") != std::string::npos
                ? "Яндекс API не предоставляет управление этой колонкой"
                : "Нет доступных команд управления");
        }
        auto *body = lv_obj_create(card);
        lv_obj_set_size(body, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(body, 4, 0);
        lv_obj_set_style_pad_row(body, 14, 0);
        lv_obj_set_style_border_width(body, 0, 0);
        panels_.push_back({device.id, body});
        lv_obj_set_hidden(body, device.id != expanded_id_ || device.controls.empty());
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
                lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
                lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
                for (int on = 0; on <= 1; ++on) {
                    const bool vacuum = device.type.find("vacuum_cleaner") != std::string::npos && c.instance == "on";
                    const char *text = c.instance == "pause" ? (on ? "Пауза" : "Продолжить") :
                        vacuum ? (on ? "Начать уборку" : "Остановить") : (on ? "Включить" : "Выключить");
                    auto *obj = button(row, text);
                    lv_obj_set_flex_grow(obj, 1);
                    if (c.known && c.boolean == bool(on)) lv_obj_set_style_bg_color(obj, lv_color_hex(0x067647), 0);
                    auto *b = make(); b->action.boolean = on;
                    bind(obj, b, LV_EVENT_CLICKED);
                }
            } else if (c.kind == ControlKind::Range) {
                auto *caption = label(body, "");
                if (c.known) lv_label_set_text_fmt(caption, "%s: %g", title(c.instance), c.number);
                else lv_label_set_text_fmt(caption, "%s: неизвестно", title(c.instance));
                if (c.relative_only) {
                    auto *row = lv_obj_create(body);
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
                    lv_obj_set_style_margin_all(obj, 12, 0);
                    const auto ticks = static_cast<int32_t>(std::floor((c.maximum - c.minimum) / c.step + 1e-7));
                    lv_slider_set_range(obj, 0, std::max<int32_t>(1, ticks));
                    lv_slider_set_value(obj, std::clamp<int32_t>(static_cast<int32_t>(std::round((c.number - c.minimum) / c.step)), 0, ticks), LV_ANIM_OFF);
                    auto *b = make(); b->slider = true; b->caption = caption; b->action.kind = ActionValue::Number;
                    bind(obj, b, LV_EVENT_RELEASED);
                }
            } else {
                label(body, title(c.instance));
                auto *obj = lv_dropdown_create(body);
                lv_obj_set_width(obj, LV_PCT(100));
                std::string options = "Выберите...";
                uint32_t selected = 0;
                if (c.kind == ControlKind::Mode) {
                    for (size_t i = 0; i < c.options.size(); ++i) {
                        options += "\n"; options += title(c.options[i].value);
                        if (c.known && c.text == c.options[i].value) selected = i + 1;
                    }
                } else options += "\nКрасный\nОранжевый\nЖёлтый\nЗелёный\nГолубой\nСиний\nФиолетовый\nРозовый";
                lv_dropdown_set_options(obj, options.c_str());
                lv_dropdown_set_selected(obj, selected);
                auto *b = make(); b->dropdown = true;
                b->action.kind = c.kind == ControlKind::Mode ? ActionValue::String :
                                 c.kind == ControlKind::Rgb ? ActionValue::Number : ActionValue::Hsv;
                bind(obj, b, LV_EVENT_VALUE_CHANGED);
                lv_obj_set_style_text_font(lv_dropdown_get_list(obj), &font_roboto_20, 0);
            }
        }
    }
    lv_obj_update_layout(list_);
    lv_obj_scroll_to_y(list_, scroll_y, LV_ANIM_OFF);
}
