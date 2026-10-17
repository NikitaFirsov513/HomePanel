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
        Device lamp; lamp.id = "lamp"; lamp.name = "Лампочка в гостиной"; lamp.room = "Гостиная";
        DeviceControl power; power.type = "devices.capabilities.on_off"; power.instance = "on"; power.known = true;
        DeviceControl brightness; brightness.type = "devices.capabilities.range"; brightness.instance = "brightness";
        brightness.kind = ControlKind::Range; brightness.minimum = 1; brightness.maximum = 100; brightness.number = 40; brightness.known = true;
        DeviceControl color; color.type = "devices.capabilities.color_setting"; color.instance = "rgb"; color.kind = ControlKind::Rgb;
        lamp.controls = {power, brightness, color};
        Device vacuum; vacuum.id = "vacuum"; vacuum.name = "Пылесос"; vacuum.type = "devices.types.vacuum_cleaner";
        vacuum.controls = {power};
        Device speaker; speaker.id = "speaker"; speaker.name = "Алиса"; speaker.type = "devices.types.smart_speaker.yandex.station";
        const std::vector<Device> devices = {lamp, vacuum, speaker};
        page.update(devices);
        lv_screen_load(page.root());
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
        auto *dropdown = find(page.root(), &lv_dropdown_class); CHECK(dropdown);
        lv_dropdown_set_selected(dropdown, 1);
        lv_obj_send_event(dropdown, LV_EVENT_VALUE_CHANGED, nullptr);
        CHECK(calls == 3 && last.number == 0xFF0000);
        page.setStatus("Команда выполнена", false);
        page.update(devices);
        auto *refresh = textButton(page.root(), "Обновить"); CHECK(refresh);
        accept = false;
        lv_obj_send_event(refresh, LV_EVENT_CLICKED, nullptr);
        CHECK(!page.busy() && calls == 3);
        accept = true;
        lv_obj_send_event(refresh, LV_EVENT_CLICKED, nullptr);
        CHECK(page.busy() && calls == 4 && last.kind == ActionValue::Refresh);
        page.setStatus("Состояния обновлены", false);
        page.update(devices);
        for (int i = 0; i < 30; ++i) page.update(devices); // Exercise binding deletion and recreation.
        lv_obj_update_layout(page.root());
        lv_refr_now(display);
        FILE *image = fopen("home-page.ppm", "wb"); CHECK(image);
        fprintf(image, "P6\n1024 600\n255\n");
        for (size_t i = 0; i < pixels.size(); i += 3) {
            const uint8_t rgb[] = {pixels[i + 2], pixels[i + 1], pixels[i]};
            fwrite(rgb, 1, 3, image);
        }
        fclose(image);
    }
    lv_display_delete(display);
    lv_deinit();
    puts("HomePage interaction tests passed");
}
