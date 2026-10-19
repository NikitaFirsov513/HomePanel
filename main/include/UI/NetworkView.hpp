#pragma once
#include "Network.hpp"
#include "lvgl.h"

class NetworkView {
public:
    using Handler = bool (*)(const WifiCommand &);
    ~NetworkView();
    void show(lv_obj_t *parent);
    void hide();
    void update(const NetworkSnapshot &snapshot);
    void setHandler(Handler handler) { handler_ = handler; }
    lv_obj_t *root() const { return root_; }
private:
    Handler handler_ = nullptr;
    NetworkSnapshot snapshot_;
    bool editing_ = false, pending_ = false;
    lv_obj_t *root_ = nullptr, *ssid_ = nullptr, *password_ = nullptr, *open_ = nullptr, *keyboard_ = nullptr;
    lv_obj_t *message_ = nullptr, *save_ = nullptr, *disconnect_ = nullptr, *edit_ = nullptr;
    lv_obj_t *ssid_label_ = nullptr, *password_label_ = nullptr, *hint_ = nullptr;
    lv_obj_t *reveal_ = nullptr, *password_status_ = nullptr;
    void maskPassword();
    void updatePasswordStatus();
    lv_obj_t *cards_[3]{}, *labels_[3]{};
    lv_obj_t *scan_button_ = nullptr, *scan_panel_ = nullptr, *scan_list_ = nullptr, *scan_message_ = nullptr, *scan_refresh_ = nullptr;
    bool scan_waiting_ = false;
    uint32_t scan_requested_generation_ = 0, scan_rendered_generation_ = UINT32_MAX;
    void openScan();
    void closeScan();
    void scan();
    void renderScan();
    void selectNetwork(const WifiNetwork &network);
    void create(lv_obj_t *parent);
    void edit(bool enabled);
    void send(WifiAction action);
};
