#pragma once
#include "YandexAuth.hpp"
#include "lvgl.h"
#include <string>

class AccountView {
public:
    using Handler = bool (*)(AuthAction);
    ~AccountView();
    void setHandler(Handler handler) { handler_ = handler; }
    void show(lv_obj_t *parent);
    void hide() { if (root_) lv_obj_set_hidden(root_, true); }
    void update(const AuthSnapshot &snapshot);
    lv_obj_t *root() const { return root_; }
private:
    Handler handler_ = nullptr;
    AuthSnapshot snapshot_;
    lv_obj_t *root_ = nullptr, *qr_ = nullptr, *placeholder_ = nullptr, *message_ = nullptr;
    lv_obj_t *code_ = nullptr, *expiry_ = nullptr, *login_ = nullptr, *cancel_ = nullptr, *logout_ = nullptr;
    lv_obj_t *steps_ = nullptr;
    lv_obj_t *restart_ = nullptr;
    lv_obj_t *url_label_ = nullptr;
    std::string qr_url_;
    void create(lv_obj_t *parent);
    lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int action);
};
