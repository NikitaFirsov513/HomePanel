#include "LoadingPage.hpp"
extern "C" { LV_FONT_DECLARE(font_roboto_20); }
namespace {
lv_obj_t *caption(lv_obj_t *parent, const char *value, int y, uint32_t color) {
    auto *label = lv_label_create(parent); lv_label_set_text(label, value);
    lv_obj_set_width(label, 800); lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, y); return label;
}
}
void LoadingPage::create() {
    if (root_) return;
    root_ = lv_obj_create(nullptr); lv_obj_set_scrollable(root_, false);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x171D30), 0);
    lv_obj_set_style_text_font(root_, &font_roboto_20, 0);
    auto *tile = lv_obj_create(root_); lv_obj_remove_style_all(tile);
    lv_obj_set_size(tile, 112, 112); lv_obj_align(tile, LV_ALIGN_CENTER, 0, -82);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x2D294B), 0); lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tile, 30, 0);
    static const lv_point_precise_t roof[] = {{24, 54}, {56, 26}, {88, 54}};
    auto *line = lv_line_create(tile); lv_line_set_points(line, roof, 3);
    lv_obj_set_style_line_color(line, lv_color_hex(0xC1B6FF), 0); lv_obj_set_style_line_width(line, 4, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    auto *walls = lv_obj_create(tile); lv_obj_remove_style_all(walls);
    lv_obj_set_pos(walls, 34, 53); lv_obj_set_size(walls, 44, 34);
    lv_obj_set_style_border_width(walls, 3, 0); lv_obj_set_style_border_color(walls, lv_color_hex(0xC1B6FF), 0);
    lv_obj_set_style_radius(walls, 6, 0);
    auto *door = lv_obj_create(tile); lv_obj_remove_style_all(door); lv_obj_set_size(door, 11, 18); lv_obj_set_pos(door, 51, 70);
    lv_obj_set_style_bg_color(door, lv_color_hex(0xC1B6FF), 0); lv_obj_set_style_bg_opa(door, LV_OPA_COVER, 0); lv_obj_set_style_radius(door, 3, 0);
    auto *brand = caption(root_, "МОЙ ДОМ", 12, 0xF5F3FF); lv_obj_set_style_text_letter_space(brand, 5, 0);
    caption(root_, "Всё, что делает дом вашим", 52, 0xA2ADC5);
    spinner_ = lv_spinner_create(root_); lv_obj_set_size(spinner_, 30, 30); lv_obj_align(spinner_, LV_ALIGN_CENTER, 0, 123);
    lv_spinner_set_anim_params(spinner_, 1100, 190);
    lv_obj_set_style_arc_width(spinner_, 3, LV_PART_MAIN); lv_obj_set_style_arc_width(spinner_, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner_, lv_color_hex(0x30354F), LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner_, lv_color_hex(0xB2A1FF), LV_PART_INDICATOR);
    label_ = caption(root_, "Подготавливаем панель", 172, 0xBDC5D7);
    caption(root_, "Свет   /   Устройства   /   Пространство", 246, 0x737E99);
}
void LoadingPage::setText(const char *text, bool loading) {
    create(); lv_label_set_text(label_, text ? text : "Подготавливаем панель");
    if (spinner_) lv_obj_set_hidden(spinner_, !loading);
}
void LoadingPage::stop() { if (spinner_) { lv_obj_delete(spinner_); spinner_ = nullptr; } }
