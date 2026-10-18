#include "HomePage.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cmath>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)

static int calls = 0;
static DeviceAction last{};
static bool accept = true;
static bool send(const DeviceAction &action) { if (!accept) return false; ++calls; last = action; return true; }
static lv_obj_t *find(lv_obj_t *root, const lv_obj_class_t *type)
{
    if (lv_obj_check_type(root, type)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        if (auto *found = find(lv_obj_get_child(root, i), type)) return found;
    return nullptr;
}
static lv_obj_t *textButton(lv_obj_t *root, const char *text)
{
    if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text))
        return lv_obj_get_parent(root);
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        if (auto *found = textButton(lv_obj_get_child(root, i), text)) return found;
    return nullptr;
}
static std::vector<uint8_t> pixels(1024 * 600 * 3);
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data)
{
    const int width = area->x2 - area->x1 + 1;
    for (int y = area->y1; y <= area->y2; ++y) {
        memcpy(pixels.data() + (y * 1024 + area->x1) * 3, data, width * 3);
        data += width * 3;
    }
    lv_display_flush_ready(display);
}
static void snapshot(lv_display_t *display, const char *path)
{
    lv_obj_update_layout(lv_screen_active());
    lv_refr_now(display);
    FILE *image = fopen(path, "wb"); CHECK(image);
    fprintf(image, "P6\n1024 600\n255\n");
    for (size_t i = 0; i < pixels.size(); i += 3) {
        const uint8_t rgb[] = {pixels[i + 2], pixels[i + 1], pixels[i]};
        fwrite(rgb, 1, 3, image);
    }
    fclose(image);
}
int main()
{
    lv_init();
    auto *display = lv_display_create(1024, 600);
    std::vector<uint8_t> buffer(1024 * 600 * 3);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);
    lv_display_set_buffers(display, buffer.data(), nullptr, buffer.size(), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush);
    {
        HomePage page;
        page.setActionHandler(send);
        Device lamp; lamp.id = "lamp"; lamp.name = "Лампочка в гостиной"; lamp.room = "Гостиная"; lamp.type = "devices.types.light";
        DeviceControl power; power.type = "devices.capabilities.on_off"; power.instance = "on"; power.known = true;
        DeviceControl brightness; brightness.type = "devices.capabilities.range"; brightness.instance = "brightness";
        brightness.kind = ControlKind::Range; brightness.minimum = 1; brightness.maximum = 100; brightness.number = 40; brightness.known = true;
        DeviceControl color; color.type = "devices.capabilities.color_setting"; color.instance = "rgb"; color.kind = ControlKind::Rgb;
        lamp.controls = {power, brightness, color};
        Device vacuum; vacuum.id = "vacuum"; vacuum.name = "Пылесос"; vacuum.type = "devices.types.vacuum_cleaner";
        DeviceControl mode; mode.type = "devices.capabilities.mode"; mode.instance = "work_speed";
        mode.kind = ControlKind::Mode; mode.options = {{"quiet"}, {"turbo"}}; mode.text = "quiet"; mode.known = true;
        vacuum.controls = {power, mode};
        Device speaker; speaker.id = "speaker"; speaker.name = "Алиса"; speaker.type = "devices.types.smart_speaker.yandex.station";
        Device lamp2 = lamp; lamp2.id = "lamp2"; lamp2.name = "Люстра"; lamp2.room = "Кухня";
        lamp2.controls[0].boolean = true;
        Device lamp3 = lamp; lamp3.id = "lamp3"; lamp3.name = "Настольная лампа"; lamp3.room = "Спальня";
        Device speaker2 = speaker; speaker2.id = "speaker2"; speaker2.name = "Алиса Мини"; speaker2.room = "Кухня";
        speaker.room = "Гостиная"; vacuum.room = "Весь дом";
        const std::vector<Device> devices = {lamp, lamp2, lamp3, vacuum, speaker, speaker2};
        page.update(devices);
        lv_screen_load(page.root());
        auto *card = textButton(page.root(), lamp.name.c_str()); CHECK(card);
        lv_obj_send_event(card, LV_EVENT_CLICKED, nullptr);
        CHECK(calls == 0); // Opening details never sends a device command.
        auto *on = textButton(page.root(), "Включить"); CHECK(on);
        lv_obj_send_event(on, LV_EVENT_CLICKED, nullptr);
        CHECK(calls == 1 && page.busy() && last.boolean && !strcmp(last.device_id, "lamp"));
        lv_obj_send_event(on, LV_EVENT_CLICKED, nullptr);
        CHECK(calls == 1);
        page.update({}); // Do not destroy widgets while a request is outstanding.
        CHECK(textButton(page.root(), "Включить") == on);
        page.setStatus("Команда выполнена", false);
        page.update(devices);
        auto *slider = find(page.root(), &lv_slider_class); CHECK(slider);
        lv_slider_set_value(slider, 69, LV_ANIM_OFF);
        lv_obj_send_event(slider, LV_EVENT_VALUE_CHANGED, nullptr);
        CHECK(calls == 1);
        lv_obj_send_event(slider, LV_EVENT_RELEASED, nullptr);
        CHECK(calls == 2 && last.number == 70 && page.busy());
        page.setStatus("Ошибка устройства", true);
        page.update(devices);
        // Color is a row of swatches. Find the button painted pure red.
        lv_obj_t *swatch = nullptr;
        auto locateRed = [&](auto &&self, lv_obj_t *obj) -> void {
            if (lv_obj_check_type(obj, &lv_button_class) &&
                (lv_color_to_u32(lv_obj_get_style_bg_color(obj, LV_PART_MAIN)) & 0xFFFFFF) == 0xFF0000) swatch = obj;
            for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i) self(self, lv_obj_get_child(obj, i));
        };
        locateRed(locateRed, page.root()); CHECK(swatch);
        lv_obj_send_event(swatch, LV_EVENT_CLICKED, nullptr);
        CHECK(calls == 3 && last.number == 0xFF0000);
        page.setStatus("Команда выполнена", false);
        page.update(devices);
        auto *close = textButton(page.root(), "Готово"); CHECK(close);
        snapshot(display, "device-details.ppm");
        lv_area_t close_area; lv_obj_get_coords(close, &close_area);
        CHECK(close_area.x1 >= 0 && close_area.x2 < 1024 && close_area.y1 >= 0 && close_area.y2 < 600);
        lv_obj_send_event(close, LV_EVENT_CLICKED, nullptr);
        auto *refresh = textButton(page.root(), "Обновить"); CHECK(refresh);
        accept = false;
        lv_obj_send_event(refresh, LV_EVENT_CLICKED, nullptr);
        CHECK(!page.busy() && calls == 3);
        accept = true;
        lv_obj_send_event(refresh, LV_EVENT_CLICKED, nullptr);
        CHECK(page.busy() && calls == 4 && last.kind == ActionValue::Refresh);
        page.setStatus("Состояния обновлены", false);
        page.update(devices);
        card = textButton(page.root(), lamp.name.c_str()); CHECK(card);
        auto *quick = find(card, &lv_button_class); CHECK(quick);
        lv_obj_send_event(quick, LV_EVENT_CLICKED, nullptr);
        CHECK(calls == 5 && last.boolean && !strcmp(last.device_id, "lamp"));
        page.setStatus("Команда выполнена", false);
        page.update(devices);
        auto *vacuum_card = textButton(page.root(), vacuum.name.c_str()); CHECK(vacuum_card);
        lv_obj_send_event(vacuum_card, LV_EVENT_CLICKED, nullptr);
        auto *dropdown = find(page.root(), &lv_dropdown_class); CHECK(dropdown);
        lv_dropdown_set_selected(dropdown, 2);
        lv_obj_send_event(dropdown, LV_EVENT_VALUE_CHANGED, nullptr);
        CHECK(calls == 6 && !strcmp(last.device_id, "vacuum") && !strcmp(last.text, "turbo"));
        page.setStatus("Команда выполнена", false);
        page.update(devices);
        close = textButton(page.root(), "Готово");
        lv_obj_send_event(close, LV_EVENT_CLICKED, nullptr);
        for (int i = 0; i < 30; ++i) page.update(devices); // Exercise binding deletion and recreation.
        lv_obj_update_layout(page.root());
        // Six device cards must fit on the 1024 x 600 panel without scrolling.
        auto *last_card = textButton(page.root(), speaker2.name.c_str()); CHECK(last_card);
        CHECK(lv_obj_get_y(last_card) + lv_obj_get_height(last_card) <= lv_obj_get_content_height(lv_obj_get_parent(last_card)));
        snapshot(display, "home-page.ppm");

        auto *plan_button = textButton(page.root(), "План дома"); CHECK(plan_button);
        lv_obj_send_event(plan_button, LV_EVENT_CLICKED, nullptr);
        auto &editor = page.editor();
        CHECK(editor.root() && !lv_obj_has_flag(editor.root(), LV_OBJ_FLAG_HIDDEN));
        const int calls_before_editor = calls;
        auto click = [&](const char *text) {
            auto *obj = textButton(editor.root(), text); CHECK(obj);
            lv_obj_send_event(obj, LV_EVENT_CLICKED, nullptr);
            lv_obj_update_layout(editor.root());
        };
        auto drag = [&](double x0, double y0, double x1, double y1) {
            const auto a = editor.gridPoint(x0, y0), b = editor.gridPoint(x1, y1);
            editor.pointerDown(a.x, a.y); editor.pointerMove(b.x, b.y); editor.pointerUp(b.x, b.y);
            lv_obj_update_layout(editor.root());
        };
        auto tap = [&](double x, double y) {
            const auto p = editor.gridPoint(x, y);
            editor.pointerDown(p.x, p.y); editor.pointerUp(p.x, p.y);
            lv_obj_update_layout(editor.root());
        };
        // Exercise LVGL's actual pointer dispatch, not just the editor methods.
        auto *touch_input = lv_indev_create();
        lv_indev_set_type(touch_input, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(touch_input, display);
        lv_indev_data_t touch{};
        lv_indev_set_user_data(touch_input, &touch);
        lv_indev_set_read_cb(touch_input, [](lv_indev_t *input, lv_indev_data_t *data) {
            *data = *static_cast<lv_indev_data_t *>(lv_indev_get_user_data(input));
        });
        auto touchAt = [&](double x, double y, lv_indev_state_t state) {
            const auto p = editor.gridPoint(x, y);
            lv_area_t area; lv_obj_get_coords(editor.canvas(), &area);
            touch.point = {area.x1 + p.x, area.y1 + p.y}; touch.state = state;
            lv_tick_inc(35); lv_indev_read(touch_input);
        };
        touchAt(2, 2, LV_INDEV_STATE_PRESSED);
        touchAt(12, 10, LV_INDEV_STATE_PRESSED);
        touchAt(12, 10, LV_INDEV_STATE_RELEASED);
        lv_indev_delete(touch_input);
        lv_obj_update_layout(editor.root());
        CHECK(editor.plan().document().rooms.size() == 1);
        const auto room_a = editor.plan().document().rooms[0].id;
        click("Название"); click("Очистить"); click("Готово");
        CHECK(find(editor.root(), &lv_textarea_class)); // Empty name keeps the keyboard open.
        click("к"); click("у"); click("х"); click("н"); click("я");
        CHECK(!strcmp(lv_textarea_get_text(find(editor.root(), &lv_textarea_class)), "кухня"));
        click("Гостиная"); snapshot(display, "floor-plan-keyboard.ppm"); click("Готово");
        CHECK(editor.plan().room(room_a)->name == "Гостиная");
        click("+ Комната"); drag(12, 2, 22, 10);
        CHECK(editor.plan().document().rooms.size() == 2);
        const auto room_b = editor.plan().document().rooms[1].id;
        click("Название"); click("Кухня"); click("Готово");
        click("+ Комната"); drag(2, 10, 12, 18);
        CHECK(editor.plan().document().rooms.size() == 3);
        click("Название"); click("Спальня"); click("Готово");
        click("+ Комната"); drag(4, 4, 8, 8); // Reject a room drawn on another room.
        CHECK(editor.plan().document().rooms.size() == 3);
        click("Выбор"); tap(3, 3);
        drag(12, 10, 11, 10); // Resize using the selected corner.
        CHECK(editor.plan().room(room_a)->rect.w == 9);
        click("Отменить"); CHECK(editor.plan().room(room_a)->rect.w == 10);
        click("Выбор"); drag(3, 3, 2, 3);
        CHECK(editor.plan().room(room_a)->rect.x == 1);
        click("Отменить"); CHECK(editor.plan().room(room_a)->rect.x == 2);
        click("Выбор"); drag(3, 3, 4, 4); // Reject moving through adjoining rooms.
        CHECK(editor.plan().room(room_a)->rect.x == 2);
        click("Дверь"); tap(7, 2.1);
        click("Окно"); tap(2.1, 6);
        CHECK(editor.plan().document().openings.size() == 2);
        click("Отменить"); CHECK(editor.plan().document().openings.size() == 1);
        click("Окно"); tap(2.1, 6);
        click("Устройство");
        dropdown = find(editor.root(), &lv_dropdown_class); CHECK(dropdown);
        lv_dropdown_set_selected(dropdown, 1); lv_obj_send_event(dropdown, LV_EVENT_VALUE_CHANGED, nullptr);
        tap(7, 6);
        CHECK(editor.plan().placement("yandex:lamp"));
        click("Сверху"); CHECK(editor.plan().placement("yandex:lamp")->level == PlanLevel::Ceiling);
        drag(7, 6, 17, 6);
        CHECK(editor.plan().placement("yandex:lamp")->room_id == room_b);
        click("Устройство");
        dropdown = find(editor.root(), &lv_dropdown_class); CHECK(dropdown);
        lv_dropdown_set_selected(dropdown, 4); lv_obj_send_event(dropdown, LV_EVENT_VALUE_CHANGED, nullptr);
        tap(7, 14); click("Снизу");
        CHECK(editor.plan().placement("yandex:vacuum")->level == PlanLevel::Floor);
        click("Устройство");
        dropdown = find(editor.root(), &lv_dropdown_class); CHECK(dropdown);
        lv_dropdown_set_selected(dropdown, 5); lv_obj_send_event(dropdown, LV_EVENT_VALUE_CHANGED, nullptr);
        tap(7, 6); click("Центр");
        CHECK(editor.plan().document().devices.size() == 3);
        click("Сохранить"); CHECK(!editor.plan().dirty());
        const auto saved = editor.plan().serialize();
        {
            FloorPlanEditor reopened;
            reopened.setDevices(devices); reopened.show(page.root());
            CHECK(reopened.plan().serialize() == saved && !reopened.plan().dirty());
            reopened.hide();
        }
        CHECK(calls == calls_before_editor); // Editing must never control real devices.
        click("Выбор"); tap(17, 6); click("Весь план");
        snapshot(display, "floor-plan-editor.ppm");
        click("К списку");
        CHECK(lv_obj_has_flag(editor.root(), LV_OBJ_FLAG_HIDDEN));

        // The house view uses this same unsaved/saved model and real device UI.
        auto *house_button = textButton(page.root(), "Дом"); CHECK(house_button);
        lv_obj_send_event(house_button, LV_EVENT_CLICKED, nullptr);
        auto &house = editor.house();
        CHECK(house.root() && !lv_obj_has_flag(house.root(), LV_OBJ_FLAG_HIDDEN));
        auto houseClick = [&](const char *text) {
            auto *obj = textButton(house.root(), text); CHECK(obj);
            lv_obj_send_event(obj, LV_EVENT_CLICKED, nullptr); lv_obj_update_layout(house.root());
        };
        const auto before_house = editor.plan().serialize();
        const auto lamp_point = house.devicePoint("yandex:lamp");
        house.pointerDown(int(lamp_point.x), int(lamp_point.y)); house.pointerUp(int(lamp_point.x), int(lamp_point.y));
        on = textButton(page.root(), "Включить"); CHECK(on);
        CHECK(calls == calls_before_editor);
        lv_obj_send_event(on, LV_EVENT_CLICKED, nullptr);
        CHECK(calls == calls_before_editor + 1 && !strcmp(last.device_id, "lamp") && last.boolean);
        page.setStatus("Команда выполнена", false); page.update(devices);
        close = textButton(page.root(), "Готово"); CHECK(close); lv_obj_send_event(close, LV_EVENT_CLICKED, nullptr);
        houseClick("Сверху"); CHECK(house.levelFilter() == 2);
        const auto floor_point = house.devicePoint("yandex:vacuum");
        house.pointerDown(int(floor_point.x), int(floor_point.y)); house.pointerUp(int(floor_point.x), int(floor_point.y));
        CHECK(!textButton(page.root(), "Начать уборку")); // Filtered floor objects are not clickable.
        houseClick("Все уровни"); CHECK(house.levelFilter() == -1);
        const auto original_point = house.devicePoint("yandex:lamp");
        house.pointerDown(20, 20); house.pointerMove(55, 40); house.pointerUp(55, 40);
        CHECK(std::abs(house.devicePoint("yandex:lamp").x - original_point.x - 35) < 0.01);
        houseClick("Вписать");
        houseClick("Поворот"); CHECK(house.projection().rotation == 1);
        snapshot(display, "house-rotated.ppm");
        for (int i = 0; i < 3; ++i) houseClick("Поворот");
        CHECK(house.projection().rotation == 0);
        houseClick("Стены: 27%"); CHECK(house.wallOpacity() == 110);
        houseClick("Стены: 43%"); CHECK(house.wallOpacity() == 0);
        houseClick("Стены: 0%"); CHECK(house.wallOpacity() == 70);
        CHECK(editor.plan().serialize() == before_house); // Camera/filter controls never edit the plan.
        snapshot(display, "house-perspective.ppm");
        houseClick("Редактор 2D"); CHECK(!lv_obj_has_flag(editor.root(), LV_OBJ_FLAG_HIDDEN));
        click("Смотреть дом"); CHECK(!lv_obj_has_flag(house.root(), LV_OBJ_FLAG_HIDDEN));
        houseClick("К списку"); CHECK(lv_obj_has_flag(house.root(), LV_OBJ_FLAG_HIDDEN));

        // Same XY at three heights stays distinct in all camera directions;
        // exact overlapping markers provide an explicit device picker.
        FloorPlan stacked;
        const auto stacked_room = stacked.addRoom({2, 2, 10, 8});
        PlanPlacement pin;
        pin.device_id = "yandex:lamp"; pin.name = lamp.name; pin.type = lamp.type;
        pin.room_id = stacked_room; pin.level = PlanLevel::Floor; CHECK(stacked.placeDevice(pin));
        pin.device_id = "yandex:lamp2"; pin.name = lamp2.name; pin.level = PlanLevel::Middle; CHECK(stacked.placeDevice(pin));
        pin.device_id = "yandex:lamp3"; pin.name = lamp3.name; pin.level = PlanLevel::Ceiling; CHECK(stacked.placeDevice(pin));
        std::string picked;
        HouseView isolated;
        isolated.setHandlers(&picked, [](void *context, const std::string &id) { *static_cast<std::string *>(context) = id; }, nullptr);
        isolated.show(page.root(), stacked.document(), devices);
        auto clickView = [&](const char *text) {
            auto *obj = textButton(isolated.root(), text); CHECK(obj); lv_obj_send_event(obj, LV_EVENT_CLICKED, nullptr);
        };
        for (int angle = 0; angle < 4; ++angle) {
            const auto lower = isolated.devicePoint("yandex:lamp"), middle = isolated.devicePoint("yandex:lamp2"), upper = isolated.devicePoint("yandex:lamp3");
            CHECK(lower.y > middle.y && middle.y > upper.y);
            for (const auto *id : {"yandex:lamp", "yandex:lamp2", "yandex:lamp3"}) {
                const auto point = isolated.devicePoint(id); picked.clear();
                isolated.pointerDown(int(point.x), int(point.y)); isolated.pointerUp(int(point.x), int(point.y));
                CHECK(picked == id);
            }
            clickView("Поворот");
        }
        snapshot(display, "house-levels.ppm");
        // A translated cached scene must match a fresh render at the same
        // camera position, including translucent walls and device hit targets.
        const auto before_pan_pixels = pixels;
        isolated.pointerDown(10, 10); isolated.pointerMove(47, 33); isolated.pointerUp(47, 33);
        lv_refr_now(display);
        const auto cached_pan_pixels = pixels;
        CHECK(cached_pan_pixels != before_pan_pixels);
        isolated.update(stacked.document(), devices);
        lv_refr_now(display);
        CHECK(pixels == cached_pan_pixels);
        const auto moved_pin = isolated.devicePoint("yandex:lamp3"); picked.clear();
        isolated.pointerDown(int(moved_pin.x), int(moved_pin.y)); isolated.pointerUp(int(moved_pin.x), int(moved_pin.y));
        CHECK(picked == "yandex:lamp3");
        pin.device_id = "yandex:lamp2"; pin.name = lamp2.name; pin.level = PlanLevel::Floor; CHECK(stacked.placeDevice(pin));
        isolated.update(stacked.document(), devices);
        const auto shared = isolated.devicePoint("yandex:lamp"); picked.clear();
        isolated.pointerDown(int(shared.x), int(shared.y)); isolated.pointerUp(int(shared.x), int(shared.y));
        CHECK(picked.empty() && textButton(isolated.root(), "Устройства рядом"));
        clickView((lamp2.name + " / Снизу").c_str()); CHECK(picked == "yandex:lamp2");
        isolated.hide();


    }
    lv_display_delete(display);
    lv_deinit();
    puts("HomePage interaction tests passed");
}
