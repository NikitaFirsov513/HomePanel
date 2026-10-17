#pragma once
#include "Page.hpp"
#include "lvgl.h"

class LoadingPage : public Page
{
public:
    void create() override;
    void setText(
        const char *text, bool loading = true);

private:
    lv_obj_t *spinner_ = nullptr;
    lv_obj_t *label_ = nullptr;
};
