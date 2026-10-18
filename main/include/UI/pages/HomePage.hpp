#pragma once
#include "Page.hpp"
#include "lvgl.h"
#include "DeviceManager.hpp"
#include "DeviceAction.hpp"
#include "FloorPlanEditor.hpp"
#include "AccountView.hpp"
#include "ConnectionsView.hpp"
#include <array>
class HomePage : public Page
{
public:
    enum class View { House, Devices, Plan, Connections, Account };
    void navigate(View view);
    View view() const { return view_; }
    void create() override;
    void create(DeviceManager *device_manager);
    void update(const std::vector<Device> &devices);
    using ActionHandler = bool (*)(const DeviceAction &);
    void setActionHandler(ActionHandler handler) { handler_ = handler; }
    void setStatus(const char *text, bool error, bool finished = true);
    bool busy() const { return busy_; }
    FloorPlanEditor &editor() { return editor_; }
    void setAuthHandler(AccountView::Handler handler) { account_.setHandler(handler); }
    void updateAuth(const AuthSnapshot &snapshot);
    void setConnectionHandler(ConnectionsView::Handler handler) { connections_.setHandler(handler); }
    void updateConnection(const ConnectionSnapshot &snapshot);

private:
    View view_ = View::House;
    bool authorized_ = false;
    bool yandex_authorized_ = false, ha_configured_ = false, ha_connected_ = false;
    void refreshConnectionsBadge();
    bool acceptsSource(const Device &device) const;
    void pruneSources();
    std::string pending_device_id_;
    bool auth_seen_ = false;
    AuthState auth_state_ = AuthState::SignedOut;
    lv_obj_t *navigation_ = nullptr, *auth_badge_ = nullptr, *auth_badge_text_ = nullptr;
    std::array<lv_obj_t *, 4> nav_buttons_{};
    lv_obj_t *list_ = nullptr;
    lv_obj_t *status_ = nullptr;
    lv_obj_t *count_ = nullptr;
    lv_obj_t *overlay_ = nullptr;
    lv_obj_t *details_ = nullptr;
    lv_obj_t *detail_title_ = nullptr;
    lv_obj_t *detail_status_ = nullptr;
    lv_obj_t *close_ = nullptr;
    std::vector<lv_obj_t *> widgets_;
    ActionHandler handler_ = nullptr;
    bool busy_ = false;
    std::string expanded_id_;
    std::vector<Device> devices_;
    size_t card_widget_count_ = 1;
    void toggleDevice(const std::string &id);
    void rebuildDevices(const std::vector<Device> &devices, bool force = false);
    void closeDetails();
    void showDetails(const Device &device);
    void createControls(const Device &device, lv_obj_t *body);
    struct Binding;
    static void controlEvent(lv_event_t *event);
    void submit(const DeviceAction &action);
    void bind(lv_obj_t *widget, Binding *binding, lv_event_code_t code);
    FloorPlanEditor editor_;
    AccountView account_;
    ConnectionsView connections_;
};
