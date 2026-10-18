#include "FloorPlanEditor.hpp"
#include "PlanStorage.hpp"
#include "DeviceIcon.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

extern "C" { LV_FONT_DECLARE(font_roboto_20); }
namespace {
constexpr uint32_t INK = 0x242B3A, MUTED = 0x7C8495, PURPLE = 0x6854D9;
enum Op { Undo = 10, Save, Close, ZoomOut, ZoomIn, Fit, Rename, Delete,
          WidthDown = 20, WidthUp, HeightDown, HeightUp,
          LevelBase = 30, NameKey = 40, NameAccept, NameCancel, NameClear, NameBackspace, NamePreset, OpenHouse = 50 };
void surface(lv_obj_t *obj, uint32_t color = 0xFFFFFF, int radius = 14)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_scrollable(obj, false);
}
lv_obj_t *label(lv_obj_t *parent, const char *text, uint32_t color = INK)
{
    auto *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_width(obj, LV_PCT(100));
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    return obj;
}
std::string key(const Device &d) { return "yandex:" + d.id; }
const char *levels[] = {"Снизу", "Центр", "Сверху"};
}

struct FloorPlanEditor::Action { FloorPlanEditor *editor; int code; std::string arg; };
FloorPlanEditor::~FloorPlanEditor() { if (root_) lv_obj_delete(root_); }

lv_obj_t *FloorPlanEditor::actionButton(lv_obj_t *parent, const char *text, int code, const std::string &arg)
{
    auto *obj = lv_button_create(parent);
    surface(obj, 0xF0EDF9, 10);
    lv_obj_set_style_pad_hor(obj, 10, 0);
    lv_obj_set_height(obj, 44);
    lv_obj_set_style_text_color(obj, lv_color_hex(PURPLE), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xE0DAF6), LV_STATE_PRESSED);
    auto *caption = lv_label_create(obj); lv_label_set_text(caption, text); lv_obj_center(caption);
    auto *action = new Action{this, code, arg};
    lv_obj_add_event_cb(obj, [](lv_event_t *event) {
        const auto *a = static_cast<Action *>(lv_event_get_user_data(event));
        auto *editor = a->editor;
        const int code = a->code;
        const std::string arg = a->arg; // perform() can delete this button.
        editor->perform(code, arg);
    }, LV_EVENT_CLICKED, action);
    lv_obj_add_event_cb(obj, [](lv_event_t *event) {
        delete static_cast<Action *>(lv_event_get_user_data(event));
    }, LV_EVENT_DELETE, action);
    return obj;
}

void FloorPlanEditor::create(lv_obj_t *parent)
{
    root_ = lv_obj_create(parent);
    surface(root_, 0xF3F4F7, 0);
    lv_obj_set_style_text_font(root_, &font_roboto_20, 0);
    lv_obj_set_floating(root_, true);
    lv_obj_set_size(root_, lv_display_get_horizontal_resolution(lv_obj_get_display(parent)),
                           lv_display_get_vertical_resolution(lv_obj_get_display(parent)));
    lv_obj_set_pos(root_, -lv_obj_get_style_pad_left(parent, LV_PART_MAIN), -lv_obj_get_style_pad_top(parent, LV_PART_MAIN));
    lv_obj_set_style_pad_all(root_, 12, 0);
    lv_obj_set_style_pad_top(root_, 76, 0);
    lv_obj_set_style_pad_row(root_, 10, 0);
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
    auto *header = lv_obj_create(root_); surface(header, 0xF3F4F7, 0);
    lv_obj_set_size(header, LV_PCT(100), 44);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 10, 0);
    auto *heading = label(header, "Конструктор"); lv_obj_set_width(heading, 170);
    saved_label_ = label(header, "План сохранён", MUTED); lv_obj_set_width(saved_label_, 0); lv_obj_set_flex_grow(saved_label_, 1);
    undo_ = actionButton(header, "Отменить", Undo);
    actionButton(header, "Сохранить", Save);
    auto *toolbar = lv_obj_create(root_); surface(toolbar, 0xF3F4F7, 0);
    lv_obj_set_size(toolbar, LV_PCT(100), 44);
    lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(toolbar, 6, 0);
    const char *tools[] = {"Выбор", "+ Комната", "Устройство", "Дверь", "Окно", "Обзор"};
    for (int i = 0; i < 6; ++i) tool_buttons_.push_back(actionButton(toolbar, tools[i], i));
    lv_obj_set_style_text_color(tool_buttons_[2], lv_color_hex(0xA5ACBA), LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(tool_buttons_[2], lv_color_hex(0xEAEDF2), LV_STATE_DISABLED);
    actionButton(toolbar, "-", ZoomOut);
    zoom_label_ = label(toolbar, "100%", MUTED); lv_obj_set_width(zoom_label_, 65); lv_obj_set_style_pad_top(zoom_label_, 10, 0);
    actionButton(toolbar, "+", ZoomIn);
    actionButton(toolbar, "Весь план", Fit);
    auto *body = lv_obj_create(root_); surface(body, 0xF3F4F7, 0);
    lv_obj_set_width(body, LV_PCT(100)); lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(body, 12, 0);
    canvas_ = lv_obj_create(body); surface(canvas_, 0xFFFFFF);
    lv_obj_set_height(canvas_, LV_PCT(100)); lv_obj_set_flex_grow(canvas_, 1); lv_obj_set_width(canvas_, 0);
    lv_obj_set_press_lock(canvas_, true);
    lv_obj_add_event_cb(canvas_, canvasEvent, LV_EVENT_ALL, this);
    sidebar_ = lv_obj_create(body); surface(sidebar_);
    lv_obj_set_size(sidebar_, 242, LV_PCT(100)); lv_obj_set_style_pad_all(sidebar_, 14, 0);
    lv_obj_set_style_pad_row(sidebar_, 12, 0); lv_obj_set_flex_flow(sidebar_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(sidebar_, true); lv_obj_set_scroll_dir(sidebar_, LV_DIR_VER);
    status_ = label(root_, "Комната: потяните по сетке. Шаг сетки - 0.5 м.", MUTED);
    std::string json;
    const auto loaded = loadFloorPlan(json);
    const bool load_error = loaded == PlanLoadResult::Error || (loaded == PlanLoadResult::Loaded && !plan_.load(json));
    refreshSidebar();
    setTool(plan_.document().rooms.empty() ? Tool::Room : Tool::Select);
    if (load_error) message("Не удалось прочитать сохранённый план. Сохранение заменит его.", true);
    lv_obj_update_layout(root_);
}

void FloorPlanEditor::show(lv_obj_t *parent)
{
    if (!root_) create(parent);
    house_.hide();
    lv_obj_set_hidden(root_, false); lv_obj_move_foreground(root_);
    refreshSidebar();
}
void FloorPlanEditor::showHouse(lv_obj_t *parent)
{
    if (!root_) create(parent);
    hide();
    house_.setHandlers(this, [](void *context, const std::string &id) {
        auto *editor = static_cast<FloorPlanEditor *>(context);
        if (editor->device_handler_) editor->device_handler_(editor->device_context_, id);
    }, [](void *context) {
        auto *editor = static_cast<FloorPlanEditor *>(context);
        editor->show(lv_obj_get_parent(editor->root_));
    });
    house_.show(parent, plan_.document(), devices_);
}
void FloorPlanEditor::hide()
{
    gesture_ = Gesture::None;
    if (name_popup_) { lv_obj_delete(name_popup_); name_popup_ = nullptr; name_input_ = nullptr; }
    if (root_) lv_obj_set_hidden(root_, true);
}
void FloorPlanEditor::message(const char *text, bool error)
{
    lv_label_set_text(status_, text);
    lv_obj_set_style_text_color(status_, lv_color_hex(error ? 0xBE4052 : MUTED), 0);
}
void FloorPlanEditor::setDevices(const std::vector<Device> &devices)
{
    devices_ = authenticated_ ? devices : std::vector<Device>{};
    // Invalidate an open picker and an in-progress placement immediately when
    // its cloud source changes. Saved placements are independent of live devices.
    if (device_dropdown_) lv_dropdown_close(device_dropdown_);
    if (!deviceAvailable(pending_device_)) pending_device_.clear();
    if (gesture_ == Gesture::MoveDevice && !deviceAvailable(selected_device_)) gesture_ = Gesture::None;
    if (!authenticated_) { option_ids_.clear(); gesture_ = Gesture::None; }
    if (house_.root()) house_.update(plan_.document(), devices_);
    if (root_ && gesture_ == Gesture::None) refreshSidebar();
    if (tool_buttons_.size() > 2) lv_obj_set_disabled(tool_buttons_[2], !authenticated_ || devices_.empty());
    if (canvas_) lv_obj_invalidate(canvas_);
}
bool FloorPlanEditor::deviceAvailable(const std::string &id) const
{
    return authenticated_ && std::any_of(devices_.begin(), devices_.end(), [&](const Device &d) { return key(d) == id; });
}
void FloorPlanEditor::setAuthenticated(bool value)
{
    if (authenticated_ == value) return;
    authenticated_ = value;
    if (!value) setDevices({});
    else if (root_) refreshSidebar();
}
void FloorPlanEditor::setTool(Tool tool)
{
    if (tool == Tool::Device && (!authenticated_ || devices_.empty())) {
        message(authenticated_ ? "Обновите список устройств Яндекса" : "Сначала войдите в Яндекс", true); return;
    }
    tool_ = tool; gesture_ = Gesture::None;
    for (size_t i = 0; i < tool_buttons_.size(); ++i) {
        const bool selected = i == static_cast<size_t>(tool);
        lv_obj_set_style_bg_color(tool_buttons_[i], lv_color_hex(selected ? PURPLE : 0xF0EDF9), 0);
        lv_obj_set_style_text_color(tool_buttons_[i], lv_color_hex(selected ? 0xFFFFFF : PURPLE), 0);
    }
    const char *tips[] = {"Тяните комнату или устройство. Размер комнаты - за правый нижний угол.",
        "Потяните по свободной сетке, чтобы нарисовать комнату. Шаг - 0.5 м.",
        "Выберите устройство справа, затем коснитесь нужного места в комнате.",
        "Коснитесь стены комнаты, чтобы добавить дверь.", "Коснитесь стены комнаты, чтобы добавить окно.",
        "Тяните план для перемещения обзора. Кнопки - и + меняют масштаб."};
    message(tips[static_cast<int>(tool)]);
    if (sidebar_) refreshSidebar();
}
void FloorPlanEditor::clearSelection() { selected_room_ = selected_opening_ = 0; selected_device_.clear(); }
void FloorPlanEditor::changed(const char *text)
{
    lv_label_set_text(saved_label_, plan_.dirty() ? "Есть изменения" : "План сохранён");
    lv_obj_set_style_text_color(saved_label_, lv_color_hex(plan_.dirty() ? PURPLE : MUTED), 0);
    lv_obj_set_disabled(undo_, !plan_.canUndo());
    refreshSidebar(); lv_obj_invalidate(canvas_); message(text);
    if (house_.root()) house_.update(plan_.document(), devices_);
}
void FloorPlanEditor::refreshSidebar()
{
    device_dropdown_ = nullptr;
    lv_obj_clean(sidebar_);
    const auto *placement = plan_.placement(selected_device_);
    const auto *room = plan_.room(selected_room_);
    if (placement) {
        const bool available = deviceAvailable(placement->device_id);
        label(sidebar_, placement->name.c_str(), available ? INK : MUTED);
        label(sidebar_, available ? "Доступно в Яндексе" : !authenticated_ ? "Требуется вход в Яндекс" : "Нет в текущем аккаунте", available ? 0x2D8669 : 0xAE6342);
        room = plan_.room(placement->room_id);
        if (room) label(sidebar_, room->name.c_str(), MUTED);
    } else if (selected_opening_) {
        const auto &items = plan_.document().openings;
        const auto it = std::find_if(items.begin(), items.end(), [&](const PlanOpening &o) { return o.id == selected_opening_; });
        label(sidebar_, it != items.end() && it->window ? "Окно" : "Дверь");
    } else if (room) {
        label(sidebar_, room->name.c_str());
        char size[64]; snprintf(size, sizeof(size), "%g x %g м", room->rect.w * 0.5, room->rect.h * 0.5);
        label(sidebar_, size, MUTED);
        auto *rename = actionButton(sidebar_, "Название", Rename); lv_obj_set_width(rename, LV_PCT(100));
        for (int axis = 0; axis < 2; ++axis) {
            auto *row = lv_obj_create(sidebar_); surface(row); lv_obj_set_size(row, LV_PCT(100), 44);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(row, 6, 0);
            auto *text = label(row, axis ? "Длина" : "Ширина"); lv_obj_set_width(text, 100); lv_obj_set_style_pad_top(text, 10, 0);
            lv_obj_set_width(actionButton(row, "-", axis ? HeightDown : WidthDown), 44);
            lv_obj_set_width(actionButton(row, "+", axis ? HeightUp : WidthUp), 44);
        }
    } else label(sidebar_, "Свойства");
    if (placement || tool_ == Tool::Device) {
        label(sidebar_, "Высота", MUTED);
        auto *row = lv_obj_create(sidebar_); surface(row); lv_obj_set_size(row, LV_PCT(100), 44);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(row, 3, 0);
        for (int i = 0; i < 3; ++i) {
            auto *obj = actionButton(row, levels[i], LevelBase + i);
            lv_obj_set_disabled(obj, !authenticated_ || (placement && !deviceAvailable(placement->device_id)));
            lv_obj_set_width(obj, 0); lv_obj_set_flex_grow(obj, 1); lv_obj_set_style_pad_hor(obj, 2, 0);
            if (i == static_cast<int>(placement ? placement->level : pending_level_)) {
                lv_obj_set_style_bg_color(obj, lv_color_hex(PURPLE), 0); lv_obj_set_style_text_color(obj, lv_color_hex(0xFFFFFF), 0);
            }
        }
    }
    if (room || placement || selected_opening_) {
        auto *remove = actionButton(sidebar_, placement ? "Убрать с плана" : "Удалить", Delete);
        lv_obj_set_width(remove, LV_PCT(100)); lv_obj_set_style_text_color(remove, lv_color_hex(0xBE4052), 0);
    }
    if (tool_ == Tool::Device) {
        label(sidebar_, "Устройство", MUTED);
        option_ids_.clear();
        std::string options = "Выберите..."; uint32_t selected = 0;
        for (const auto &device : devices_) {
            option_ids_.push_back(key(device));
            options += "\n" + device.name;
            if (key(device) == pending_device_) selected = option_ids_.size();
        }
        device_dropdown_ = lv_dropdown_create(sidebar_);
        lv_obj_set_width(device_dropdown_, LV_PCT(100)); lv_obj_set_height(device_dropdown_, 48);
        lv_dropdown_set_options(device_dropdown_, options.c_str()); lv_dropdown_set_selected(device_dropdown_, selected);
        lv_obj_set_disabled(device_dropdown_, !authenticated_ || devices_.empty());
        lv_dropdown_set_symbol(device_dropdown_, nullptr);
        lv_obj_set_style_text_font(lv_dropdown_get_list(device_dropdown_), &font_roboto_20, 0);
        lv_obj_add_event_cb(device_dropdown_, [](lv_event_t *event) {
            auto *editor = static_cast<FloorPlanEditor *>(lv_event_get_user_data(event));
            const auto index = lv_dropdown_get_selected(static_cast<lv_obj_t *>(lv_event_get_target(event)));
            editor->pending_device_ = index && index <= editor->option_ids_.size() ? editor->option_ids_[index - 1] : "";
            editor->clearSelection();
            editor->message("Коснитесь нужного места внутри комнаты.");
        }, LV_EVENT_VALUE_CHANGED, this);
        if (devices_.empty()) label(sidebar_, authenticated_ ? "Нет устройств в текущем аккаунте" : "Добавление доступно после входа в Яндекс", MUTED);
    }
    if (!room && !placement && !selected_opening_ && tool_ != Tool::Device)
        label(sidebar_, "Выберите объект на плане. Комнаты и устройства можно перетаскивать пальцем.", MUTED);
    lv_obj_set_disabled(undo_, !plan_.canUndo());
    if (tool_buttons_.size() > 2) lv_obj_set_disabled(tool_buttons_[2], !authenticated_ || devices_.empty());
}

void FloorPlanEditor::perform(int action, const std::string &arg)
{
    if (action >= 0 && action < 6) { setTool(static_cast<Tool>(action)); return; }
    if (action == Close) { hide(); return; }
    if (action == OpenHouse) { showHouse(lv_obj_get_parent(root_)); return; }
    if (action == Save) {
        const auto json = plan_.serialize();
        if (!saveFloorPlan(json)) { message("Не удалось сохранить план. Изменения остались в памяти.", true); return; }
        plan_.markSaved(); changed("План сохранён на устройстве"); return;
    }
    if (action == Undo) {
        if (plan_.undo()) { clearSelection(); changed("Последнее действие отменено"); }
        return;
    }
    if (action == ZoomOut || action == ZoomIn) { zoom(action == ZoomIn ? 4 : -4); return; }
    if (action == Fit) { fit(); return; }
    if (action == Rename) { openNameEditor(); return; }
    if (action == Delete) {
        bool ok = false;
        if (!selected_device_.empty()) ok = plan_.removeDevice(selected_device_);
        else if (selected_opening_) ok = plan_.removeOpening(selected_opening_);
        else ok = plan_.removeRoom(selected_room_);
        if (ok) { clearSelection(); changed("Объект убран с плана. Доступна отмена."); }
        return;
    }
    if (action >= LevelBase && action < LevelBase + 3) {
        pending_level_ = static_cast<PlanLevel>(action - LevelBase);
        if (!selected_device_.empty()) plan_.setLevel(selected_device_, pending_level_);
        changed("Высота размещения выбрана"); return;
    }
    if (action >= WidthDown && action <= HeightUp) {
        const auto *room = plan_.room(selected_room_); if (!room) return;
        auto rect = room->rect;
        if (action == WidthDown) --rect.w;
        if (action == WidthUp) ++rect.w;
        if (action == HeightDown) --rect.h;
        if (action == HeightUp) ++rect.h;
        if (!plan_.changeRoom(room->id, rect)) message("Недостаточно места или достигнут минимальный размер", true);
        else changed("Размер комнаты изменён");
        return;
    }
    if (action == NameKey && name_input_) { lv_textarea_add_text(name_input_, arg.c_str()); return; }
    if (action == NameClear && name_input_) { lv_textarea_set_text(name_input_, ""); return; }
    if (action == NameBackspace && name_input_) { lv_textarea_delete_char(name_input_); return; }
    if (action == NamePreset && name_input_) { lv_textarea_set_text(name_input_, arg.c_str()); return; }
    if (action == NameAccept && name_input_) {
        const std::string name = lv_textarea_get_text(name_input_);
        if (!plan_.renameRoom(selected_room_, name)) {
            lv_label_set_text(lv_obj_get_child(name_popup_, 0), "Введите название до 32 символов");
            lv_obj_set_style_text_color(lv_obj_get_child(name_popup_, 0), lv_color_hex(0xBE4052), 0);
            return;
        }
    }
    if (action == NameAccept || action == NameCancel) {
        if (name_popup_) lv_obj_delete(name_popup_);
        name_popup_ = name_input_ = nullptr;
        changed(action == NameAccept ? "Название комнаты изменено" : "Редактирование названия отменено");
    }
}

void FloorPlanEditor::openNameEditor()
{
    const auto *room = plan_.room(selected_room_); if (!room || name_popup_) return;
    name_popup_ = lv_obj_create(root_); surface(name_popup_, 0xF3F4F7, 0);
    lv_obj_set_floating(name_popup_, true); lv_obj_set_pos(name_popup_, -12, -12);
    lv_obj_set_size(name_popup_, lv_obj_get_width(root_), lv_obj_get_height(root_));
    lv_obj_set_style_pad_all(name_popup_, 20, 0); lv_obj_set_style_pad_row(name_popup_, 12, 0);
    lv_obj_set_flex_flow(name_popup_, LV_FLEX_FLOW_COLUMN);
    label(name_popup_, "Название комнаты");
    name_input_ = lv_textarea_create(name_popup_); lv_obj_set_width(name_input_, LV_PCT(100));
    lv_textarea_set_one_line(name_input_, true); lv_textarea_set_max_length(name_input_, 32);
    lv_textarea_set_text(name_input_, room->name.c_str());
    lv_obj_add_event_cb(name_input_, [](lv_event_t *event) {
        auto *editor = static_cast<FloorPlanEditor *>(lv_event_get_user_data(event));
        auto *heading = lv_obj_get_child(editor->name_popup_, 0);
        lv_label_set_text(heading, "Название комнаты");
        lv_obj_set_style_text_color(heading, lv_color_hex(INK), 0);
    }, LV_EVENT_VALUE_CHANGED, this);
    auto *presets = lv_obj_create(name_popup_); surface(presets, 0xF3F4F7, 0);
    lv_obj_set_size(presets, LV_PCT(100), 44); lv_obj_set_flex_flow(presets, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(presets, 8, 0);
    for (const char *name : {"Гостиная", "Кухня", "Спальня", "Ванная", "Коридор", "Детская"}) actionButton(presets, name, NamePreset, name);
    const char *letters[] = {"й", "ц", "у", "к", "е", "н", "г", "ш", "щ", "з", "х", "ф", "ы", "в", "а", "п", "р", "о", "л", "д", "ж", "э", "я", "ч", "с", "м", "и", "т", "ь", "б", "ю", "ъ", "ё", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
    auto *keys = lv_obj_create(name_popup_); surface(keys, 0xF3F4F7, 0);
    lv_obj_set_size(keys, LV_PCT(100), 200); lv_obj_set_flex_flow(keys, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(keys, 6, 0); lv_obj_set_style_pad_column(keys, 6, 0);
    const int width = (lv_obj_get_width(root_) - 40 - 10 * 6) / 11;
    for (const auto *letter : letters) lv_obj_set_width(actionButton(keys, letter, NameKey, letter), width);
    auto *bottom = lv_obj_create(name_popup_); surface(bottom, 0xF3F4F7, 0);
    lv_obj_set_size(bottom, LV_PCT(100), 44); lv_obj_set_flex_flow(bottom, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bottom, 12, 0);
    actionButton(bottom, "Пробел", NameKey, " "); actionButton(bottom, "Стереть", NameBackspace);
    actionButton(bottom, "Очистить", NameClear); actionButton(bottom, "Отмена", NameCancel); actionButton(bottom, "Готово", NameAccept);
}

lv_point_t FloorPlanEditor::gridPoint(double x, double y) const
{
    return {pan_x_ + static_cast<int>(std::lround(x * scale_)), pan_y_ + static_cast<int>(std::lround(y * scale_))};
}
const PlanRoom *FloorPlanEditor::roomAt(double x, double y) const
{
    const auto &rooms = plan_.document().rooms;
    for (auto it = rooms.rbegin(); it != rooms.rend(); ++it)
        if (x >= it->rect.x && y >= it->rect.y && x <= it->rect.x + it->rect.w && y <= it->rect.y + it->rect.h) return &*it;
    return nullptr;
}
PlanRect FloorPlanEditor::displayRect(const PlanRoom &room) const
{
    if ((gesture_ == Gesture::MoveRoom || gesture_ == Gesture::ResizeRoom) && room.id == selected_room_) return draft_rect_;
    return room.rect;
}
lv_point_t FloorPlanEditor::devicePoint(const PlanPlacement &device) const
{
    const auto *room = plan_.room(device.room_id); if (!room) return {-1000, -1000};
    const auto r = displayRect(*room);
    return gridPoint(r.x + r.w * device.u / 1000.0, r.y + r.h * device.v / 1000.0);
}
PlanPlacement FloorPlanEditor::placementAt(const Device &device, const PlanRoom &room, double x, double y) const
{
    PlanPlacement p;
    p.device_id = key(device); p.name = device.name; p.type = device.type; p.room_id = room.id; p.level = pending_level_;
    p.u = std::clamp(static_cast<int>(std::lround((x - room.rect.x) * 1000 / room.rect.w)), 100, 900);
    p.v = std::clamp(static_cast<int>(std::lround((y - room.rect.y) * 1000 / room.rect.h)), 100, 900);
    return p;
}
void FloorPlanEditor::zoom(int delta)
{
    const int next = std::clamp(scale_ + delta, 8, 32);
    const double cx = (lv_obj_get_width(canvas_) * 0.5 - pan_x_) / scale_;
    const double cy = (lv_obj_get_height(canvas_) * 0.5 - pan_y_) / scale_;
    scale_ = next;
    pan_x_ = static_cast<int>(lv_obj_get_width(canvas_) * 0.5 - cx * scale_);
    pan_y_ = static_cast<int>(lv_obj_get_height(canvas_) * 0.5 - cy * scale_);
    lv_label_set_text_fmt(zoom_label_, "%d%%", scale_ * 100 / 16); lv_obj_invalidate(canvas_);
}
void FloorPlanEditor::fit()
{
    int x0 = 0, y0 = 0, x1 = FloorPlan::WIDTH, y1 = FloorPlan::HEIGHT;
    if (!plan_.document().rooms.empty()) {
        x0 = FloorPlan::WIDTH; y0 = FloorPlan::HEIGHT; x1 = y1 = 0;
        for (const auto &r : plan_.document().rooms) {
            x0 = std::min(x0, r.rect.x); y0 = std::min(y0, r.rect.y);
            x1 = std::max(x1, r.rect.x + r.rect.w); y1 = std::max(y1, r.rect.y + r.rect.h);
        }
    }
    scale_ = std::clamp<int>(std::min((lv_obj_get_width(canvas_) - 40) / (x1 - x0),
                                (lv_obj_get_height(canvas_) - 40) / (y1 - y0)), 8, 32);
    pan_x_ = (lv_obj_get_width(canvas_) - (x1 - x0) * scale_) / 2 - x0 * scale_;
    pan_y_ = (lv_obj_get_height(canvas_) - (y1 - y0) * scale_) / 2 - y0 * scale_;
    lv_label_set_text_fmt(zoom_label_, "%d%%", scale_ * 100 / 16); lv_obj_invalidate(canvas_);
}

void FloorPlanEditor::pointerDown(int x, int y)
{
    if (name_popup_) return;
    down_x_ = x; down_y_ = y;
    start_gx_ = (x - pan_x_) / static_cast<double>(scale_);
    start_gy_ = (y - pan_y_) / static_cast<double>(scale_);
    draft_valid_ = true; gesture_ = Gesture::None;
    if (tool_ == Tool::Pan) {
        gesture_ = Gesture::Pan; original_pan_x_ = pan_x_; original_pan_y_ = pan_y_; return;
    }
    if (tool_ == Tool::Room) {
        clearSelection();
        if (start_gx_ < 0 || start_gx_ > FloorPlan::WIDTH || start_gy_ < 0 || start_gy_ > FloorPlan::HEIGHT) {
            message("Рисуйте внутри границ плана", true); return;
        }
        gesture_ = Gesture::NewRoom;
        draft_rect_ = {static_cast<int>(std::lround(start_gx_)), static_cast<int>(std::lround(start_gy_)), 8, 6};
        draft_valid_ = plan_.canPlace(draft_rect_);
        lv_obj_invalidate(canvas_); return;
    }
    if (tool_ == Tool::Device) {
        if (!authenticated_) { message("Сначала войдите в Яндекс", true); return; }
        const auto it = std::find_if(devices_.begin(), devices_.end(), [&](const Device &d) { return key(d) == pending_device_; });
        const auto *room = roomAt(start_gx_, start_gy_);
        if (it == devices_.end()) { message("Сначала выберите устройство справа", true); return; }
        if (!room) { message("Устройство должно находиться внутри комнаты", true); return; }
        const auto p = placementAt(*it, *room, std::round(start_gx_), std::round(start_gy_));
        if (!plan_.placeDevice(p)) { message("Не удалось разместить устройство", true); return; }
        clearSelection(); selected_device_ = p.device_id;
        setTool(Tool::Select); changed("Устройство размещено. Выберите высоту справа."); return;
    }
    if (tool_ == Tool::Door || tool_ == Tool::Window) {
        const auto *room = roomAt(start_gx_, start_gy_);
        if (!room) { message("Коснитесь стены внутри комнаты", true); return; }
        const double distances[] = {std::abs(start_gy_ - room->rect.y), std::abs(start_gx_ - room->rect.x - room->rect.w),
                                    std::abs(start_gy_ - room->rect.y - room->rect.h), std::abs(start_gx_ - room->rect.x)};
        const auto edge = static_cast<int>(std::min_element(distances, distances + 4) - distances);
        if (distances[edge] > 1.25) { message("Подвиньте касание ближе к стене", true); return; }
        const int offset = std::clamp(static_cast<int>(std::lround((edge % 2 ?
            (start_gy_ - room->rect.y) / room->rect.h : (start_gx_ - room->rect.x) / room->rect.w) * 1000)), 50, 950);
        const auto id = plan_.addOpening(room->id, static_cast<PlanEdge>(edge), offset, tool_ == Tool::Window);
        if (!id) { message("Проём уже есть рядом или достигнут лимит", true); return; }
        clearSelection(); selected_opening_ = id;
        changed(tool_ == Tool::Window ? "Окно добавлено" : "Дверь добавлена"); return;
    }
    // Prefer a selected room's resize handle to objects immediately beside it.
    if (const auto *room = plan_.room(selected_room_)) {
        const auto end = gridPoint(room->rect.x + room->rect.w, room->rect.y + room->rect.h);
        if (std::abs(end.x - x) <= 20 && std::abs(end.y - y) <= 20) {
            original_rect_ = draft_rect_ = room->rect; gesture_ = Gesture::ResizeRoom; return;
        }
    }
    clearSelection();
    for (const auto &p : plan_.document().devices) {
        const auto pos = devicePoint(p);
        if ((pos.x - x) * (pos.x - x) + (pos.y - y) * (pos.y - y) <= 22 * 22) {
            selected_device_ = p.device_id; draft_device_ = p; pending_level_ = p.level;
            gesture_ = deviceAvailable(p.device_id) ? Gesture::MoveDevice : Gesture::None;
            if (gesture_ == Gesture::None) message(authenticated_ ? "Устройство отсутствует в текущем аккаунте Яндекса." :
                "Сохранённая привязка. Войдите в Яндекс, чтобы проверить устройство.");
            refreshSidebar(); lv_obj_invalidate(canvas_); return;
        }
    }
    for (const auto &o : plan_.document().openings) {
        const auto *r = plan_.room(o.room_id); if (!r) continue;
        const bool horizontal = o.edge == PlanEdge::Top || o.edge == PlanEdge::Bottom;
        const auto pos = gridPoint(horizontal ? r->rect.x + r->rect.w * o.offset / 1000.0 :
                                       r->rect.x + (o.edge == PlanEdge::Right ? r->rect.w : 0),
                                   horizontal ? r->rect.y + (o.edge == PlanEdge::Bottom ? r->rect.h : 0) :
                                       r->rect.y + r->rect.h * o.offset / 1000.0);
        if (std::abs(pos.x - x) <= 16 && std::abs(pos.y - y) <= 16) {
            selected_opening_ = o.id; refreshSidebar(); lv_obj_invalidate(canvas_); return;
        }
    }
    if (const auto *room = roomAt(start_gx_, start_gy_)) {
        selected_room_ = room->id; original_rect_ = draft_rect_ = room->rect; gesture_ = Gesture::MoveRoom;
    }
    refreshSidebar(); lv_obj_invalidate(canvas_);
}

void FloorPlanEditor::pointerMove(int x, int y)
{
    const double gx = (x - pan_x_) / static_cast<double>(scale_);
    const double gy = (y - pan_y_) / static_cast<double>(scale_);
    if (gesture_ == Gesture::Pan) {
        pan_x_ = std::clamp<int>(original_pan_x_ + x - down_x_, -FloorPlan::WIDTH * scale_ + 80, lv_obj_get_width(canvas_) - 80);
        pan_y_ = std::clamp<int>(original_pan_y_ + y - down_y_, -FloorPlan::HEIGHT * scale_ + 80, lv_obj_get_height(canvas_) - 80);
    } else if (gesture_ == Gesture::NewRoom) {
        if (std::abs(x - down_x_) + std::abs(y - down_y_) <= 4) return;
        const int x0 = static_cast<int>(std::lround(start_gx_)), y0 = static_cast<int>(std::lround(start_gy_));
        const int x1 = static_cast<int>(std::lround(gx)), y1 = static_cast<int>(std::lround(gy));
        draft_rect_ = {std::min(x0, x1), std::min(y0, y1), std::abs(x1 - x0), std::abs(y1 - y0)};
        draft_valid_ = plan_.canPlace(draft_rect_);
    } else if (gesture_ == Gesture::MoveRoom || gesture_ == Gesture::ResizeRoom) {
        const int dx = static_cast<int>(std::lround(gx - start_gx_)), dy = static_cast<int>(std::lround(gy - start_gy_));
        draft_rect_ = original_rect_;
        if (gesture_ == Gesture::MoveRoom) { draft_rect_.x += dx; draft_rect_.y += dy; }
        else { draft_rect_.w += dx; draft_rect_.h += dy; }
        draft_valid_ = plan_.canPlace(draft_rect_, selected_room_);
    } else if (gesture_ == Gesture::MoveDevice) {
        const auto *room = roomAt(gx, gy);
        draft_valid_ = room != nullptr;
        if (room) {
            draft_device_.room_id = room->id;
            draft_device_.u = std::clamp(static_cast<int>(std::lround((std::round(gx) - room->rect.x) * 1000 / room->rect.w)), 100, 900);
            draft_device_.v = std::clamp(static_cast<int>(std::lround((std::round(gy) - room->rect.y) * 1000 / room->rect.h)), 100, 900);
        }
    }
    lv_obj_invalidate(canvas_);
}
void FloorPlanEditor::pointerUp(int x, int y)
{
    const auto gesture = gesture_;
    if (gesture == Gesture::None) return;
    const bool moved = std::abs(x - down_x_) + std::abs(y - down_y_) > 4;
    if (moved) pointerMove(x, y);
    gesture_ = Gesture::None;
    if (gesture == Gesture::Pan) { lv_obj_invalidate(canvas_); return; }
    if (!draft_valid_) {
        refreshSidebar(); lv_obj_invalidate(canvas_);
        message(gesture == Gesture::MoveDevice ? "Переместите устройство внутрь комнаты" :
            "Комнаты не должны пересекаться. Минимальный размер - 1.5 x 1.5 м.", true); return;
    }
    if (gesture == Gesture::NewRoom) {
        const auto id = plan_.addRoom(draft_rect_);
        if (!id) { message("Не удалось добавить комнату: нет места или достигнут лимит", true); lv_obj_invalidate(canvas_); return; }
        selected_room_ = id; setTool(Tool::Select); changed("Комната добавлена. Название и размеры - справа.");
    } else if (gesture == Gesture::MoveRoom || gesture == Gesture::ResizeRoom) {
        if (moved && !plan_.changeRoom(selected_room_, draft_rect_)) message("Недостаточно места для комнаты", true);
        else changed(moved ? "Комната изменена" : "Комната выбрана. Размер можно менять за угол.");
    } else if (gesture == Gesture::MoveDevice) {
        if (moved) plan_.placeDevice(draft_device_);
        changed(moved ? "Устройство перемещено" : "Устройство выбрано. Высота - справа.");
    }
}

void FloorPlanEditor::canvasEvent(lv_event_t *event)
{
    auto *editor = static_cast<FloorPlanEditor *>(lv_event_get_user_data(event));
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_DRAW_MAIN) { editor->draw(lv_event_get_layer(event)); return; }
    if (code == LV_EVENT_PRESS_LOST) {
        editor->gesture_ = Gesture::None; lv_obj_invalidate(editor->canvas_); return;
    }
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED) return;
    const auto *input = lv_indev_active(); if (!input) return;
    lv_point_t point; lv_indev_get_point(input, &point);
    lv_area_t area; lv_obj_get_coords(editor->canvas_, &area);
    const int x = point.x - area.x1, y = point.y - area.y1;
    if (code == LV_EVENT_PRESSED) editor->pointerDown(x, y);
    else if (code == LV_EVENT_PRESSING) editor->pointerMove(x, y);
    else editor->pointerUp(x, y);
}

void FloorPlanEditor::draw(lv_layer_t *layer)
{
    lv_area_t canvas; lv_obj_get_coords(canvas_, &canvas);
    auto line = [&](int x1, int y1, int x2, int y2, uint32_t color, int width = 1) {
        lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
        d.color = lv_color_hex(color); d.width = width;
        d.p1.x = canvas.x1 + x1; d.p1.y = canvas.y1 + y1;
        d.p2.x = canvas.x1 + x2; d.p2.y = canvas.y1 + y2;
        lv_draw_line(layer, &d);
    };
    auto rect = [&](int x, int y, int w, int h, uint32_t fill, uint32_t border, int border_width = 1, int radius = 0) {
        if (w <= 0 || h <= 0) return;
        lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
        d.bg_color = lv_color_hex(fill); d.border_color = lv_color_hex(border); d.border_width = border_width; d.radius = radius;
        lv_area_t area = {canvas.x1 + x, canvas.y1 + y, canvas.x1 + x + w - 1, canvas.y1 + y + h - 1};
        lv_draw_rect(layer, &d, &area);
    };
    auto text = [&](int x, int y, int w, const std::string &value, uint32_t color = INK) {
        if (w < 8) return;
        lv_draw_label_dsc_t d; lv_draw_label_dsc_init(&d);
        d.text = value.c_str(); d.text_local = 1; d.font = &font_roboto_20; d.color = lv_color_hex(color);
        lv_area_t area = {canvas.x1 + x, canvas.y1 + y, canvas.x1 + x + w - 1, canvas.y1 + y + 24};
        lv_draw_label(layer, &d, &area);
    };
    for (int x = 0; x <= FloorPlan::WIDTH; ++x) {
        const auto a = gridPoint(x, 0), b = gridPoint(x, FloorPlan::HEIGHT);
        line(a.x, a.y, b.x, b.y, x % 5 ? 0xEEF0F5 : 0xDCE0EB);
    }
    for (int y = 0; y <= FloorPlan::HEIGHT; ++y) {
        const auto a = gridPoint(0, y), b = gridPoint(FloorPlan::WIDTH, y);
        line(a.x, a.y, b.x, b.y, y % 5 ? 0xEEF0F5 : 0xDCE0EB);
    }
    const uint32_t colors[] = {0xF0ECFB, 0xE8F5F1, 0xFFF4DF, 0xE9F1FC, 0xFBEDEF};
    for (const auto &room : plan_.document().rooms) {
        const auto r = displayRect(room); const auto pos = gridPoint(r.x, r.y);
        const bool selected = room.id == selected_room_;
        const bool invalid = selected && !draft_valid_ && gesture_ != Gesture::None;
        rect(pos.x, pos.y, r.w * scale_, r.h * scale_, invalid ? 0xFCE6E9 : colors[(room.id - 1) % 5],
             invalid ? 0xC74359 : selected ? PURPLE : 0xA9B1C1, selected ? 3 : 2);
        text(pos.x + 8, pos.y + 6, r.w * scale_ - 16, room.name);
        if (r.h * scale_ > 72) {
            char size[48]; snprintf(size, sizeof(size), "%g x %g м", r.w * 0.5, r.h * 0.5);
            text(pos.x + 8, pos.y + r.h * scale_ - 30, r.w * scale_ - 16, size, MUTED);
        }
        if (selected) rect(pos.x + r.w * scale_ - 8, pos.y + r.h * scale_ - 8, 16, 16, PURPLE, 0xFFFFFF, 2, 4);
    }
    if (gesture_ == Gesture::NewRoom) {
        const auto pos = gridPoint(draft_rect_.x, draft_rect_.y);
        rect(pos.x, pos.y, draft_rect_.w * scale_, draft_rect_.h * scale_, draft_valid_ ? 0xF0ECFB : 0xFCE6E9,
             draft_valid_ ? PURPLE : 0xC74359, 3);
    }
    for (const auto &o : plan_.document().openings) {
        const auto *room = plan_.room(o.room_id); if (!room) continue;
        const auto r = displayRect(*room);
        const bool horizontal = o.edge == PlanEdge::Top || o.edge == PlanEdge::Bottom;
        const auto pos = gridPoint(horizontal ? r.x + r.w * o.offset / 1000.0 : r.x + (o.edge == PlanEdge::Right ? r.w : 0),
                                   horizontal ? r.y + (o.edge == PlanEdge::Bottom ? r.h : 0) : r.y + r.h * o.offset / 1000.0);
        const uint32_t color = selected_opening_ == o.id ? PURPLE : o.window ? 0x4099C8 : 0xB78B55;
        const int half = std::min(scale_, (horizontal ? r.w : r.h) * scale_ / 5);
        line(pos.x - (horizontal ? half : 0), pos.y - (horizontal ? 0 : half),
             pos.x + (horizontal ? half : 0), pos.y + (horizontal ? 0 : half), 0xFFFFFF, 8);
        line(pos.x - (horizontal ? half : 0), pos.y - (horizontal ? 0 : half),
             pos.x + (horizontal ? half : 0), pos.y + (horizontal ? 0 : half), color, o.window ? 4 : 3);
        if (!o.window) line(pos.x - (horizontal ? half : 0), pos.y - (horizontal ? 0 : half),
                           pos.x, pos.y + (horizontal ? half : 0), color, 2);
    }
    for (const auto &saved : plan_.document().devices) {
        const auto &p = gesture_ == Gesture::MoveDevice && saved.device_id == selected_device_ ? draft_device_ : saved;
        const auto pos = devicePoint(p);
        const bool selected = p.device_id == selected_device_;
        const bool available = deviceAvailable(p.device_id);
        rect(pos.x - 20, pos.y - 20, 40, 40, available ? 0xFFFFFF : 0xEEF0F4, selected ? PURPLE : 0xD1D5E0, selected ? 3 : 1, 20);
        lv_area_t area = {canvas.x1 + pos.x - 18, canvas.y1 + pos.y - 18, canvas.x1 + pos.x + 17, canvas.y1 + pos.y + 17};
        drawDeviceIcon(layer, deviceIconForType(p.type), area, lv_color_hex(!available ? 0xADB4C3 : selected ? PURPLE : 0x68738B));
        const uint32_t level_colors[] = {0x3E9C86, PURPLE, 0xCE983D};
        rect(pos.x + 11, pos.y - 20, 9, 9, available ? level_colors[static_cast<int>(p.level)] : 0xADB4C3, 0xFFFFFF, 1, 5);
        if (!available) text(pos.x - 56, pos.y + 22, 118, authenticated_ ? "Недоступно" : "Нет входа", MUTED);
    }
    if (plan_.document().rooms.empty() && gesture_ != Gesture::NewRoom) {
        text(30, 48, lv_obj_get_width(canvas_) - 60, "Нарисуйте первую комнату", PURPLE);
    }
}
