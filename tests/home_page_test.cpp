#include "HomePage.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

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
    }
    lv_display_delete(display);
    lv_deinit();
    puts("HomePage interaction tests passed");
}
