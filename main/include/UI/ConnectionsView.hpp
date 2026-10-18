#pragma once
#include "Connections.hpp"
#include "YandexAuth.hpp"
#include "lvgl.h"

class ConnectionsView {
public:
    using Handler = bool (*)(const ConnectionCommand &);
    using OpenYandex = void (*)(void *);
    ~ConnectionsView();
    void show(lv_obj_t *parent);
    void hide();
    void setHandler(Handler handler) { handler_ = handler; }
    void setYandexHandler(OpenYandex handler, void *context) { yandex_handler_ = handler; context_ = context; }
    void updateYandex(const AuthSnapshot &snapshot);
    void update(const ConnectionSnapshot &snapshot);
    lv_obj_t *root() const { return root_; }
private:
    Handler handler_ = nullptr;
    OpenYandex yandex_handler_ = nullptr;
    void *context_ = nullptr;
    AuthSnapshot yandex_;
    ConnectionSnapshot snapshot_;
    bool editing_ = false;
    lv_obj_t *root_ = nullptr, *yandex_status_ = nullptr, *ha_status_ = nullptr, *address_label_ = nullptr;
    lv_obj_t *form_ = nullptr, *address_ = nullptr, *token_ = nullptr, *keyboard_ = nullptr;
    lv_obj_t *form_message_ = nullptr, *save_ = nullptr, *disconnect_ = nullptr;
    void create(lv_obj_t *parent);
    void openSettings();
    void closeSettings();
    void send(ConnectionAction action);
};
