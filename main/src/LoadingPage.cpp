#include "LoadingPage.hpp"
#include "lvgl.h"


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
    const char *text)
{
    if (label_ == nullptr)
        return;

    lv_label_set_text(
        label_,
        text);
}
