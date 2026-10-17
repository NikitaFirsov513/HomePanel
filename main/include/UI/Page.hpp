#pragma once

#include "lvgl.h"

class Page
{
public:
    Page() = default;
    Page(const Page &) = delete;
    Page &operator=(const Page &) = delete;
    virtual ~Page()
    {
        if (root_ != nullptr)
        {
            lv_obj_delete(root_);
            root_ = nullptr;
        }
    }

    virtual void create() = 0;

    virtual void onShow()
    {
    }

    virtual void onHide()
    {
    }

    lv_obj_t *root() const
    {
        return root_;
    }

protected:
    lv_obj_t *root_ = nullptr;
};
