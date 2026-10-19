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
static int auth_calls = 0;
static AuthAction auth_action = AuthAction::Begin;
static bool authCommand(AuthAction action) { ++auth_calls; auth_action = action; return true; }
static int connection_calls = 0;
static ConnectionCommand last_connection;
static bool connectionCommand(const ConnectionCommand &command) { ++connection_calls; last_connection = command; return true; }
static WifiCommand last_wifi;
static int wifi_calls = 0;
static bool wifiCommand(const WifiCommand &command) { ++wifi_calls; last_wifi = command; return true; }
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
// Every visible account label must be renderable by the font actually used on the panel.
static void checkGlyphs(lv_obj_t *obj)
{
    if (!lv_obj_is_visible(obj)) return;
    if (lv_obj_check_type(obj, &lv_label_class)) {
        const auto *p = reinterpret_cast<const unsigned char *>(lv_label_get_text(obj));
        const auto *font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
        while (*p) {
            uint32_t cp = *p++;
            if (cp >= 0xC0) {
                unsigned remaining = cp < 0xE0 ? 1 : cp < 0xF0 ? 2 : 3;
                cp &= (1u << (6 - remaining)) - 1;
                while (remaining--) { CHECK((*p & 0xC0) == 0x80); cp = (cp << 6) | (*p++ & 0x3F); }
            }
            if (cp == '\n' || cp == '\r') continue;
            lv_font_glyph_dsc_t glyph{};
            CHECK(lv_font_get_glyph_dsc(font, &glyph, cp, 0) && !glyph.is_placeholder);
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i) checkGlyphs(lv_obj_get_child(obj, i));
}
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
        page.setAuthHandler(authCommand);
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
        CHECK(!textButton(page.root(), lamp.name.c_str())); // Cloud data is gated by login.
        auto &default_house = page.editor().house();
        CHECK(default_house.root() && lv_obj_is_visible(default_house.root()));
        auto *connections_button = textButton(page.root(), "Подключения"); CHECK(connections_button);
        lv_obj_send_event(connections_button, LV_EVENT_CLICKED, nullptr);
        snapshot(display, "connections.ppm");
        auto *account_button = textButton(page.root(), "Открыть аккаунт"); CHECK(account_button);
        lv_obj_send_event(account_button, LV_EVENT_CLICKED, nullptr);
        auto *account_title = textButton(page.root(), "Аккаунт Яндекса"); CHECK(account_title);
        checkGlyphs(account_title);
        snapshot(display, "yandex-signed-out.ppm");
        auto *login = textButton(account_title, "Войти по QR"); CHECK(login);
        lv_obj_send_event(login, LV_EVENT_CLICKED, nullptr); CHECK(auth_calls == 1 && auth_action == AuthAction::Begin);
        AuthSnapshot waiting; waiting.state = AuthState::Waiting; waiting.seconds_left = 295;
        snprintf(waiting.user_code, sizeof(waiting.user_code), "1234567");
        snprintf(waiting.verification_url, sizeof(waiting.verification_url), "https://ya.ru/device");
        snprintf(waiting.message, sizeof(waiting.message), "Сканируйте QR, введите код и разрешите доступ");
        page.updateAuth(waiting);
        auto *qr = find(account_title, &lv_qrcode_class); CHECK(qr && lv_obj_is_visible(qr));
        CHECK(textButton(account_title, "Код: 1234567"));
        checkGlyphs(account_title);
        snapshot(display, "yandex-login.ppm");
        auto *cancel_login = textButton(account_title, "Отменить вход"); CHECK(cancel_login);
        lv_obj_send_event(cancel_login, LV_EVENT_CLICKED, nullptr); CHECK(auth_calls == 2 && auth_action == AuthAction::Cancel);
        AuthSnapshot connected; connected.state = AuthState::Authorized; connected.authorized = true;
        snprintf(connected.message, sizeof(connected.message), "Аккаунт подключён"); page.updateAuth(connected);
        CHECK(!lv_obj_is_visible(qr));
        checkGlyphs(account_title);
        snapshot(display, "yandex-connected.ppm");
        auto *restart = textButton(account_title, "Войти заново"); CHECK(restart && lv_obj_is_visible(restart));
        lv_obj_send_event(restart, LV_EVENT_CLICKED, nullptr); CHECK(auth_calls == 3 && auth_action == AuthAction::Restart);
        page.updateAuth(waiting);
        CHECK(lv_obj_is_visible(qr) && !lv_obj_is_visible(restart));
        page.updateAuth(connected);
        auto *logout = textButton(account_title, "Выйти"); CHECK(logout && lv_obj_is_visible(logout));
        lv_obj_send_event(logout, LV_EVENT_CLICKED, nullptr); CHECK(auth_calls == 4 && auth_action == AuthAction::Logout);
        page.updateAuth(AuthSnapshot{});
        AuthSnapshot failed; failed.state = AuthState::Error;
        snprintf(failed.message, sizeof(failed.message), "Код устарел. Запросите новый код входа.");
        page.updateAuth(failed);
        checkGlyphs(account_title);
        snapshot(display, "yandex-error.ppm");
        page.updateAuth(AuthSnapshot{});
        auto *home_tab = textButton(page.root(), "Дом"); CHECK(home_tab);
        lv_obj_send_event(home_tab, LV_EVENT_CLICKED, nullptr);
        CHECK(!lv_obj_is_visible(account_title) && lv_obj_is_visible(default_house.root()));
        CHECK(lv_obj_has_flag(page.editor().root(), LV_OBJ_FLAG_HIDDEN));
        page.setStatus("Нет подключения к сети", true);
        CHECK(textButton(default_house.root(), "Нет подключения к сети"));
        page.updateAuth(connected); page.update(devices);
        CHECK(textButton(page.root(), "Подключено: 1 из 2"));
        auto *open_list = textButton(page.root(), "Устройства"); CHECK(open_list);
        lv_obj_send_event(open_list, LV_EVENT_CLICKED, nullptr);
        CHECK(!lv_obj_is_visible(default_house.root()));
        page.update(devices); // A refresh must preserve the selected list screen.
        CHECK(!lv_obj_is_visible(default_house.root()));
        auto *open_home = textButton(page.root(), "Дом"); CHECK(open_home);
        lv_obj_send_event(open_home, LV_EVENT_CLICKED, nullptr);
        page.update(devices); // A refresh must also preserve the house screen.
        CHECK(lv_obj_is_visible(default_house.root()));
        lv_obj_send_event(open_list, LV_EVENT_CLICKED, nullptr);
        CHECK(!lv_obj_is_visible(default_house.root()) && calls == 0);
        page.setStatus("Устройства готовы к управлению", false);
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

        auto *plan_button = textButton(page.root(), "План"); CHECK(plan_button);
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
            reopened.setAuthenticated(true); reopened.setDevices(devices); reopened.show(page.root());
            CHECK(reopened.plan().serialize() == saved && !reopened.plan().dirty());
            reopened.hide();
        }
        CHECK(calls == calls_before_editor); // Editing must never control real devices.
        click("Выбор"); tap(17, 6); click("Весь план");
        snapshot(display, "floor-plan-editor.ppm");
        page.navigate(HomePage::View::Devices);
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
        CHECK(lv_obj_is_visible(house.root())); // Closing device controls returns to the house.
        const auto floor_point = house.devicePoint("yandex:vacuum");
        house.pointerDown(int(floor_point.x), int(floor_point.y)); house.pointerUp(int(floor_point.x), int(floor_point.y));
        CHECK(!textButton(page.root(), "Начать уборку")); // Filtered floor objects are not clickable.
        houseClick("Все уровни"); CHECK(house.levelFilter() == -1);
        const auto original_point = house.devicePoint("yandex:lamp");
        house.pointerDown(20, 20); house.pointerMove(55, 40); house.pointerUp(55, 40);
        CHECK(std::abs(house.devicePoint("yandex:lamp").x - original_point.x) < 0.01);
        CHECK(std::abs(house.devicePoint("yandex:lamp").y - original_point.y) < 0.01);
        CHECK(!textButton(house.root(), "+") && !textButton(house.root(), "-") && !textButton(house.root(), "Вписать"));
        houseClick("Поворот"); CHECK(house.projection().rotation == 1);
        snapshot(display, "house-rotated.ppm");
        for (int i = 0; i < 3; ++i) houseClick("Поворот");
        CHECK(house.projection().rotation == 0);
        houseClick("Стены: 27%"); CHECK(house.wallOpacity() == 110);
        houseClick("Стены: 43%"); CHECK(house.wallOpacity() == 0);
        houseClick("Стены: 0%"); CHECK(house.wallOpacity() == 70);
        CHECK(editor.plan().serialize() == before_house); // Camera/filter controls never edit the plan.
        snapshot(display, "house-perspective.ppm");
        page.navigate(HomePage::View::Plan); CHECK(!lv_obj_has_flag(editor.root(), LV_OBJ_FLAG_HIDDEN));
        page.navigate(HomePage::View::House); CHECK(!lv_obj_has_flag(house.root(), LV_OBJ_FLAG_HIDDEN));
        page.navigate(HomePage::View::Devices); CHECK(lv_obj_has_flag(house.root(), LV_OBJ_FLAG_HIDDEN));

        // Logout must invalidate an open device picker, pressed cards and saved
        // house markers immediately, without deleting the local floor plan.
        const auto preserved_plan = editor.plan().serialize();
        auto *pressed_card = textButton(page.root(), lamp.name.c_str()); CHECK(pressed_card);
        lv_obj_add_state(pressed_card, LV_STATE_PRESSED);
        page.navigate(HomePage::View::Plan); editor.setTool(FloorPlanEditor::Tool::Device);
        auto *old_picker = find(editor.root(), &lv_dropdown_class); CHECK(old_picker);
        lv_dropdown_open(old_picker);
        page.updateAuth(AuthSnapshot{});
        CHECK(textButton(page.root(), "Устройств: 0"));
        auto *new_picker = find(editor.root(), &lv_dropdown_class); CHECK(new_picker);
        CHECK(lv_obj_has_state(new_picker, LV_STATE_DISABLED) && !lv_dropdown_is_open(new_picker));
        CHECK(lv_dropdown_get_option_count(new_picker) == 1);
        page.update(devices); // A stale cloud snapshot after logout must be ignored.
        CHECK(textButton(page.root(), "Устройств: 0") && house.devicePoint("yandex:lamp").x < 0);
        const auto offline_point = editor.gridPoint(17, 6);
        editor.pointerDown(offline_point.x, offline_point.y); editor.pointerUp(offline_point.x, offline_point.y);
        CHECK(editor.plan().serialize() == preserved_plan);
        page.navigate(HomePage::View::House);
        CHECK(lv_obj_is_visible(textButton(page.root(), "Нет подключений")));
        snapshot(display, "house-signed-out.ppm");
        page.navigate(HomePage::View::Plan); editor.setTool(FloorPlanEditor::Tool::Select);
        const auto &saved_pin = editor.plan().document().devices.front();
        const auto *saved_room = editor.plan().room(saved_pin.room_id); CHECK(saved_room);
        const auto ghost = editor.gridPoint(saved_room->rect.x + saved_room->rect.w * saved_pin.u / 1000.0,
                                           saved_room->rect.y + saved_room->rect.h * saved_pin.v / 1000.0);
        editor.pointerDown(ghost.x, ghost.y); editor.pointerUp(ghost.x, ghost.y);
        CHECK(textButton(editor.root(), "Проверьте подключения"));
        snapshot(display, "floor-plan-signed-out.ppm");
        CHECK(editor.plan().serialize() == preserved_plan);
        page.updateAuth(connected); page.update({speaker});
        CHECK(house.devicePoint("yandex:lamp").x < 0 && house.devicePoint("yandex:speaker").x > 0);
        page.update(devices);
        CHECK(house.devicePoint("yandex:lamp").x > 0);
        page.navigate(HomePage::View::Devices);

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
        // Swipes leave the camera and image fixed and must not activate pins.
        const auto before_pan_pixels = pixels;
        const auto swipe_pin = isolated.devicePoint("yandex:lamp3"); picked.clear();
        isolated.pointerDown(int(swipe_pin.x) - 40, int(swipe_pin.y));
        isolated.pointerMove(int(swipe_pin.x), int(swipe_pin.y));
        isolated.pointerUp(int(swipe_pin.x), int(swipe_pin.y));
        CHECK(picked.empty());
        lv_refr_now(display);
        const auto cached_pan_pixels = pixels;
        CHECK(cached_pan_pixels == before_pan_pixels);
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
    {
        HomePage page;
        page.setActionHandler(send); page.setConnectionHandler(connectionCommand);
        page.update({}); lv_screen_load(page.root());
        Device ha; ha.source = DeviceSource::HomeAssistant; ha.id = "ha:light.test"; ha.native_id = "light.test";
        ha.name = "Свет Home Assistant"; ha.type = "devices.types.light";
        DeviceControl power; power.type = "ha.on"; power.instance = "on"; power.known = true; ha.controls = {power};
        Device ya = ha; ya.source = DeviceSource::Yandex; ya.id = "yandex-test"; ya.name = "Свет Яндекса";
        ConnectionSnapshot connection; connection.configured = true; connection.connected = true; connection.devices = 1;
        std::snprintf(connection.address, sizeof(connection.address), "http://192.168.1.20:8123");
        page.updateConnection(connection); page.update({ya, ha}); page.navigate(HomePage::View::Devices);
        CHECK(textButton(page.root(), ha.name.c_str()) && !textButton(page.root(), ya.name.c_str()));
        auto *card = textButton(page.root(), ha.name.c_str()); CHECK(card);
        lv_obj_send_event(card, LV_EVENT_CLICKED, nullptr);
        const auto previous_calls = calls;
        auto *on = textButton(page.root(), "Включить"); CHECK(on);
        lv_obj_send_event(on, LV_EVENT_CLICKED, nullptr);
        CHECK(calls == previous_calls + 1 && !std::strcmp(last.device_id, "ha:light.test") && !std::strcmp(last.type, "ha.on"));
        page.setStatus("Команда принята", false); page.update({ha});
        auto *done = textButton(page.root(), "Готово"); CHECK(done); lv_obj_send_event(done, LV_EVENT_CLICKED, nullptr);
        AuthSnapshot yandex; yandex.authorized = true; yandex.state = AuthState::Authorized;
        page.updateAuth(yandex); page.update({ya, ha}); CHECK(textButton(page.root(), "Устройств: 2"));
        page.navigate(HomePage::View::Connections);
        CHECK(textButton(page.root(), "Подключено: 2 из 2"));
        snapshot(display, "connections-both.ppm");
        page.navigate(HomePage::View::Devices);
        card = textButton(page.root(), ha.name.c_str()); CHECK(card);
        lv_obj_send_event(card, LV_EVENT_CLICKED, nullptr);
        on = textButton(page.root(), "Включить"); CHECK(on); lv_obj_send_event(on, LV_EVENT_CLICKED, nullptr);
        CHECK(page.busy());
        page.updateAuth(AuthSnapshot{});
        CHECK(page.busy()); // Disconnecting Yandex must not release an outstanding HA command.
        page.setStatus("Команда принята", false); page.update({ya, ha});
        done = textButton(page.root(), "Готово"); CHECK(done); lv_obj_send_event(done, LV_EVENT_CLICKED, nullptr);
        page.updateAuth(yandex); page.update({ya, ha});
        auto *pressed = textButton(page.root(), ha.name.c_str()); CHECK(pressed); lv_obj_add_state(pressed, LV_STATE_PRESSED);
        page.updateAuth(AuthSnapshot{}); // A Yandex logout must keep HA, including while a card is pressed.
        CHECK(textButton(page.root(), ha.name.c_str()) && !textButton(page.root(), ya.name.c_str()));
        page.update({ya, ha}); CHECK(textButton(page.root(), "Устройств: 1"));
        page.updateAuth(yandex); page.update({ya, ha});
        page.updateConnection(ConnectionSnapshot{}); // HA disconnect must keep Yandex.
        CHECK(textButton(page.root(), ya.name.c_str()) && !textButton(page.root(), ha.name.c_str()));
        page.update({ya, ha}); CHECK(textButton(page.root(), "Устройств: 1"));
        page.navigate(HomePage::View::Connections);
        auto *settings = textButton(page.root(), "Настроить"); CHECK(settings); lv_obj_send_event(settings, LV_EVENT_CLICKED, nullptr);
        auto *form = textButton(page.root(), "Настройка Home Assistant"); CHECK(form);
        auto *address = find(form, &lv_textarea_class); CHECK(address);
        lv_obj_t *token = nullptr;
        for (uint32_t i = 0; i < lv_obj_get_child_count(form); ++i) {
            auto *child = lv_obj_get_child(form, i);
            if (child != address && lv_obj_check_type(child, &lv_textarea_class)) token = child;
        }
        CHECK(token && lv_textarea_get_password_mode(token));
        auto *keyboard = find(form, &lv_keyboard_class); CHECK(keyboard);
        lv_obj_update_layout(form);
        lv_area_t keyboard_area; lv_obj_get_coords(keyboard, &keyboard_area);
        CHECK(lv_obj_is_visible(keyboard) && keyboard_area.y1 >= 64 && keyboard_area.y2 < 600);
        lv_obj_send_event(token, LV_EVENT_FOCUSED, nullptr);
        CHECK(lv_keyboard_get_textarea(keyboard) == token);
        lv_textarea_set_text(address, "http://192.168.1.20:8123"); lv_textarea_set_text(token, "test-only-token");
        snapshot(display, "home-assistant-settings.ppm");
        auto *connect = textButton(form, "Подключить"); CHECK(connect); lv_obj_send_event(connect, LV_EVENT_CLICKED, nullptr);
        CHECK(connection_calls == 1 && last_connection.action == ConnectionAction::Save);
        CHECK(!std::strcmp(last_connection.address, "http://192.168.1.20:8123") && !std::strcmp(last_connection.token, "test-only-token"));
        CHECK(!lv_textarea_get_text(token)[0]);
        page.updateConnection(connection);
        auto *disconnect = textButton(form, "Отключить"); CHECK(disconnect); lv_obj_send_event(disconnect, LV_EVENT_CLICKED, nullptr);
        CHECK(connection_calls == 2 && last_connection.action == ConnectionAction::Disconnect);
        page.updateConnection(ConnectionSnapshot{});
        // Both source IDs survive a perspective render and resolve to their own commands.
        FloorPlan plan; const auto room = plan.addRoom({2, 2, 12, 9});
        PlanPlacement pin; pin.device_id = devicePlanKey(ha); pin.name = ha.name; pin.type = ha.type; pin.room_id = room; pin.u = 250;
        CHECK(plan.placeDevice(pin)); pin.device_id = devicePlanKey(ya); pin.name = ya.name; pin.u = 750; CHECK(plan.placeDevice(pin));
        HouseView house; std::string picked;
        house.setHandlers(&picked, [](void *ctx, const std::string &id) { *static_cast<std::string *>(ctx) = id; }, nullptr);
        house.show(page.root(), plan.document(), {ha, ya});
        const auto point = house.devicePoint(ha.id); CHECK(point.x > 0);
        house.pointerDown(point.x, point.y); house.pointerUp(point.x, point.y); CHECK(picked == ha.id);
        house.update(plan.document(), {ha});
        CHECK(house.devicePoint(ha.id).x > 0 && house.devicePoint("yandex:" + ya.id).x < 0);
        house.hide();
    }
    {
        HomePage page; page.setWifiHandler(wifiCommand); page.update({}); lv_screen_load(page.root());
        NetworkSnapshot net; net.active = NetworkPath::Ethernet; net.ethernet_ready = net.wifi_ready = net.wifi_configured = true;
        std::snprintf(net.ethernet_ip, sizeof(net.ethernet_ip), "192.168.31.148");
        std::snprintf(net.wifi_ip, sizeof(net.wifi_ip), "192.168.31.149");
        std::snprintf(net.ssid, sizeof(net.ssid), "Home_24");
        std::snprintf(net.saved_password, sizeof(net.saved_password), "demo-password");
        std::snprintf(net.message, sizeof(net.message), "Wi-Fi подключён. Провод имеет приоритет.");
        page.updateNetwork(net);
        auto *badge = textButton(page.root(), "Провод"); CHECK(badge);
        lv_obj_send_event(badge, LV_EVENT_CLICKED, nullptr);
        auto *network_root = textButton(page.root(), "Подключение к сети"); CHECK(network_root && page.view() == HomePage::View::Network);
        auto *wired = textButton(network_root, "Провод\n192.168.31.148"); CHECK(wired);
        auto *wireless = textButton(network_root, "Wi-Fi\n192.168.31.149"); CHECK(wireless);
        CHECK((lv_color_to_u32(lv_obj_get_style_bg_color(wired, LV_PART_MAIN)) & 0xFFFFFF) == 0xE5F4EC);
        CHECK((lv_color_to_u32(lv_obj_get_style_bg_color(wireless, LV_PART_MAIN)) & 0xFFFFFF) == 0xFFFFFF);
        checkGlyphs(network_root); snapshot(display, "network-ethernet.ppm");
        net.active = NetworkPath::Wifi; net.ethernet_ready = false; net.ethernet_ip[0] = 0;
        page.updateNetwork(net); CHECK(page.view() == HomePage::View::Network);
        CHECK((lv_color_to_u32(lv_obj_get_style_bg_color(wireless, LV_PART_MAIN)) & 0xFFFFFF) == 0xE5F4EC);
        snapshot(display, "network-wifi.ppm");
        auto *preview_password = find(network_root, &lv_textarea_class);
        for (uint32_t i = 0; i < lv_obj_get_child_count(network_root); ++i) {
            auto *o = lv_obj_get_child(network_root, i);
            if (lv_obj_check_type(o, &lv_textarea_class) && lv_textarea_get_password_mode(o)) preview_password = o;
        }
        CHECK(preview_password && lv_textarea_get_password_mode(preview_password));
        CHECK(textButton(network_root, "Пароль сохранён"));
        auto *reveal = textButton(network_root, "Показать"); CHECK(reveal);
        lv_obj_send_event(reveal, LV_EVENT_CLICKED, nullptr);
        CHECK(!lv_textarea_get_password_mode(preview_password));
        CHECK(!std::strcmp(lv_textarea_get_text(preview_password), "demo-password"));
        snapshot(display, "network-password-preview.ppm");
        lv_obj_send_event(reveal, LV_EVENT_CLICKED, nullptr); CHECK(lv_textarea_get_password_mode(preview_password));
        auto *edit = textButton(network_root, "Изменить"); CHECK(edit); lv_obj_send_event(edit, LV_EVENT_CLICKED, nullptr);
        auto *keyboard = find(network_root, &lv_keyboard_class); CHECK(keyboard && lv_obj_is_visible(keyboard));
        lv_obj_update_layout(network_root); lv_area_t area; lv_obj_get_coords(keyboard, &area);
        CHECK(area.y1 >= 64 && area.y2 < 600);
        auto *ssid = find(network_root, &lv_textarea_class); CHECK(ssid);
        lv_obj_t *password = nullptr;
        for (uint32_t i = 0; i < lv_obj_get_child_count(network_root); ++i) {
            auto *obj = lv_obj_get_child(network_root, i);
            if (obj != ssid && lv_obj_check_type(obj, &lv_textarea_class)) password = obj;
        }
        CHECK(password && lv_textarea_get_password_mode(password));
        lv_textarea_set_text(ssid, "New_24"); CHECK(!lv_textarea_get_text(password)[0]);
        CHECK(textButton(network_root, "Пароль для этой сети не сохранён"));
        lv_textarea_set_text(password, "test-password");
        lv_obj_send_event(reveal, LV_EVENT_CLICKED, nullptr); CHECK(!lv_textarea_get_password_mode(password));
        lv_obj_send_event(reveal, LV_EVENT_CLICKED, nullptr); CHECK(lv_textarea_get_password_mode(password));
        lv_obj_send_event(password, LV_EVENT_FOCUSED, nullptr); CHECK(lv_keyboard_get_textarea(keyboard) == password);
        snapshot(display, "network-settings.ppm");
        auto *save = textButton(network_root, "Подключить Wi-Fi"); CHECK(save);
        lv_obj_send_event(save, LV_EVENT_CLICKED, nullptr);
        CHECK(wifi_calls == 1 && last_wifi.action == WifiAction::Save && !std::strcmp(last_wifi.ssid, "New_24"));
        CHECK(!std::strcmp(last_wifi.password, "test-password") && !lv_textarea_get_text(password)[0]);
        page.updateNetwork(net);
        auto *forget = textButton(network_root, "Забыть Wi-Fi"); CHECK(forget); lv_obj_send_event(forget, LV_EVENT_CLICKED, nullptr);
        CHECK(wifi_calls == 2 && last_wifi.action == WifiAction::Disconnect);
        net = {}; page.updateNetwork(net);
        auto *done = textButton(network_root, "Готово"); CHECK(done); lv_obj_send_event(done, LV_EVENT_CLICKED, nullptr);
        CHECK(!lv_obj_is_visible(keyboard));
        CHECK(textButton(network_root, "Нет подключения\nОжидание сети"));
        snapshot(display, "network-offline.ppm");
        auto *search = textButton(network_root, "Найти сети"); CHECK(search);
        lv_obj_send_event(search, LV_EVENT_CLICKED, nullptr);
        CHECK(wifi_calls == 3 && last_wifi.action == WifiAction::Scan && !lv_obj_is_visible(keyboard));
        auto *popup = textButton(network_root, "Доступные сети Wi-Fi"); CHECK(popup && lv_obj_is_visible(popup));
        auto *refresh = textButton(popup, "Обновить"); CHECK(lv_obj_has_state(refresh, LV_STATE_DISABLED));
        page.updateNetwork(net); CHECK(lv_obj_has_state(refresh, LV_STATE_DISABLED));
        net.scan.state = WifiScanState::Scanning; net.scan.generation = 1; net.busy = true; page.updateNetwork(net);
        net.scan.state = WifiScanState::Ready; net.scan.generation = 2; net.busy = false; net.scan.count = 3;
        std::snprintf(net.scan.message, sizeof(net.scan.message), "Выберите сеть");
        const char *names[] = {"Home_24", "Guest", "Enterprise"};
        for (int i = 0; i < 3; ++i) { auto &n = net.scan.networks[i]; std::snprintf(n.ssid, sizeof(n.ssid), "%s", names[i]); n.rssi = -35 - i * 15; n.channel = 6; }
        net.scan.networks[0].security = WifiSecurity::Password; net.scan.networks[1].security = WifiSecurity::Open;
        page.updateNetwork(net); CHECK(!lv_obj_has_state(refresh, LV_STATE_DISABLED));
        auto *secured = textButton(popup, "Home_24"); auto *enterprise = textButton(popup, "Enterprise"); CHECK(secured && enterprise);
        CHECK(lv_obj_has_state(enterprise, LV_STATE_DISABLED));
        checkGlyphs(popup); snapshot(display, "wifi-scan.ppm");
        lv_obj_send_event(secured, LV_EVENT_CLICKED, nullptr);
        CHECK(!lv_obj_is_visible(popup) && lv_obj_is_visible(keyboard));
        CHECK(!std::strcmp(lv_textarea_get_text(ssid), "Home_24") && !lv_textarea_get_text(password)[0]);
        CHECK(lv_keyboard_get_textarea(keyboard) == password);
        lv_obj_send_event(search, LV_EVENT_CLICKED, nullptr); CHECK(wifi_calls == 4);
        net.scan.generation = 3; page.updateNetwork(net);
        auto *guest = textButton(popup, "Guest"); CHECK(guest); lv_obj_send_event(guest, LV_EVENT_CLICKED, nullptr);
        CHECK(!std::strcmp(lv_textarea_get_text(ssid), "Guest"));
        auto *open = find(network_root, &lv_checkbox_class); CHECK(open);
        CHECK(lv_obj_has_state(open, LV_STATE_CHECKED));
        lv_obj_send_event(save, LV_EVENT_CLICKED, nullptr); CHECK(last_wifi.open_network && !std::strcmp(last_wifi.ssid, "Guest"));
        lv_obj_send_event(search, LV_EVENT_CLICKED, nullptr);
        net.scan.state = WifiScanState::Error; net.scan.count = 0; ++net.scan.generation;
        std::snprintf(net.scan.message, sizeof(net.scan.message), "Нет ответа Wi-Fi модуля"); page.updateNetwork(net);
        CHECK(textButton(popup, "Нет ответа Wi-Fi модуля") && !textButton(popup, "Home_24"));
        page.navigate(HomePage::View::House); CHECK(!lv_obj_is_visible(network_root));
        CHECK(lv_textarea_get_password_mode(password));
        lv_obj_update_layout(page.root()); lv_obj_get_coords(badge, &area);
        CHECK(area.x1 >= 0 && area.x2 < 1024 && area.y2 < 64);
    }
    lv_display_delete(display);
    lv_deinit();
    puts("HomePage interaction tests passed");
}
