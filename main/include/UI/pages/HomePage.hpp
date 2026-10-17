#pragma once
#include "Page.hpp"
#include "lvgl.h"
#include "DeviceManager.hpp"
#include "DeviceAction.hpp"
class HomePage : public Page
{
public:
    void create() override;
    void create(DeviceManager *device_manager);
    void update(const std::vector<Device> &devices);
    using ActionHandler = bool (*)(const DeviceAction &);
    void setActionHandler(ActionHandler handler) { handler_ = handler; }
    void setStatus(const char *text, bool error, bool finished = true);
    bool busy() const { return busy_; }

private:
    lv_obj_t *list_ = nullptr;
    lv_obj_t *status_ = nullptr;
    std::vector<lv_obj_t *> widgets_;
    ActionHandler handler_ = nullptr;
    bool busy_ = false;
    std::string expanded_id_;
    std::vector<std::pair<std::string, lv_obj_t *>> panels_;
    void toggleDevice(const std::string &id);
    struct Binding;
    static void controlEvent(lv_event_t *event);
    void submit(const DeviceAction &action);
    void bind(lv_obj_t *widget, Binding *binding, lv_event_code_t code);
};
