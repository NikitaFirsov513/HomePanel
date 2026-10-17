#include "LoadingPage.hpp"
#include "lvgl.h"
extern "C" { LV_FONT_DECLARE(font_roboto_20); }


void LoadingPage::create()
{
    if (root_ != nullptr)
    {
        return;
    }

    /*
     * Отдельный screen.
     */
    root_ =
        lv_obj_create(nullptr);
    lv_obj_set_style_text_font(root_, &font_roboto_20, 0);


    /*
     * Spinner.
     */
    spinner_ =
        lv_spinner_create(root_);

    lv_obj_set_size(
        spinner_,
        70,
        70);

    lv_obj_align(
        spinner_,
        LV_ALIGN_CENTER,
        0,
        -30);


    /*
     * Label.
     */
    label_ =
        lv_label_create(root_);

    lv_obj_set_width(label_, LV_PCT(90));
    lv_obj_set_style_text_align(label_, LV_TEXT_ALIGN_CENTER, 0);

    lv_label_set_text(
        label_,
        "Loading...");

    lv_obj_align(
        label_,
        LV_ALIGN_CENTER,
        0,
        50);
}


void LoadingPage::setText(
    const char *text, bool loading)
{
    if (label_ == nullptr)
        return;

    lv_label_set_text(
        label_,
        text);
    if (loading) {
        lv_obj_remove_flag(spinner_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(spinner_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_text_color(label_,
        loading ? lv_color_hex(0x333333) : lv_color_hex(0xC62828), 0);
    lv_obj_align(label_, LV_ALIGN_CENTER, 0, loading ? 50 : 0);
}
