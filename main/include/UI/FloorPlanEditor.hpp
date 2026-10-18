#pragma once
#include "FloorPlan.hpp"
#include "Device.hpp"
#include "lvgl.h"
#include "HouseView.hpp"

class FloorPlanEditor {
public:
    enum class Tool { Select, Room, Device, Door, Window, Pan };
    FloorPlanEditor() = default;
    ~FloorPlanEditor();
    FloorPlanEditor(const FloorPlanEditor &) = delete;
    FloorPlanEditor &operator=(const FloorPlanEditor &) = delete;
    void show(lv_obj_t *parent);
    void hide();
    void showHouse(lv_obj_t *parent);
    void setDeviceHandler(HouseView::DeviceHandler handler, void *context) { device_handler_ = handler; device_context_ = context; }
    HouseView &house() { return house_; }
    void setDevices(const std::vector<Device> &devices);
    lv_obj_t *root() const { return root_; }
    lv_obj_t *canvas() const { return canvas_; }
    const FloorPlan &plan() const { return plan_; }
    void setTool(Tool tool);
    // Canvas-local input: used by the touch handler and headless interaction tests.
    void pointerDown(int x, int y);
    void pointerMove(int x, int y);
    void pointerUp(int x, int y);
    lv_point_t gridPoint(double x, double y) const;
private:
    FloorPlan plan_;
    std::vector<Device> devices_;
    std::vector<std::string> option_ids_;
    lv_obj_t *root_ = nullptr, *canvas_ = nullptr, *sidebar_ = nullptr, *status_ = nullptr;
    lv_obj_t *saved_label_ = nullptr, *zoom_label_ = nullptr, *undo_ = nullptr, *device_dropdown_ = nullptr;
    lv_obj_t *name_popup_ = nullptr, *name_input_ = nullptr;
    std::vector<lv_obj_t *> tool_buttons_;
    Tool tool_ = Tool::Select;
    enum class Gesture { None, NewRoom, MoveRoom, ResizeRoom, MoveDevice, Pan };
    Gesture gesture_ = Gesture::None;
    uint32_t selected_room_ = 0, selected_opening_ = 0;
    std::string selected_device_, pending_device_;
    PlanLevel pending_level_ = PlanLevel::Middle;
    PlanRect original_rect_, draft_rect_;
    PlanPlacement draft_device_;
    int scale_ = 16, pan_x_ = 24, pan_y_ = 24;
    int down_x_ = 0, down_y_ = 0, original_pan_x_ = 0, original_pan_y_ = 0;
    double start_gx_ = 0, start_gy_ = 0;
    bool draft_valid_ = true;
    HouseView::DeviceHandler device_handler_ = nullptr;
    void *device_context_ = nullptr;
    HouseView house_;
    struct Action;
    lv_obj_t *actionButton(lv_obj_t *parent, const char *text, int action, const std::string &arg = "");
    void create(lv_obj_t *parent);
    void refreshSidebar();
    void changed(const char *message);
    void message(const char *text, bool error = false);
    void perform(int action, const std::string &arg);
    void openNameEditor();
    void zoom(int delta);
    void fit();
    void clearSelection();
    const PlanRoom *roomAt(double x, double y) const;
    PlanRect displayRect(const PlanRoom &room) const;
    lv_point_t devicePoint(const PlanPlacement &device) const;
    PlanPlacement placementAt(const Device &device, const PlanRoom &room, double x, double y) const;
    void draw(lv_layer_t *layer);
    static void canvasEvent(lv_event_t *event);
};
