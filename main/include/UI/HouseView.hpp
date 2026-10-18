#pragma once
#include "FloorPlan.hpp"
#include "HouseProjection.hpp"
#include "Device.hpp"
#include "lvgl.h"
#include <memory>

struct HouseScene;

class HouseView {
public:
    HouseView();
    HouseView(const HouseView &) = delete;
    HouseView &operator=(const HouseView &) = delete;
    using DeviceHandler = void (*)(void *, const std::string &);
    using EditHandler = void (*)(void *);
    ~HouseView();
    void setHandlers(void *context, DeviceHandler device, EditHandler edit);
    void show(lv_obj_t *parent, const FloorPlan::Document &plan, const std::vector<Device> &devices);
    void update(const FloorPlan::Document &plan, const std::vector<Device> &devices);
    void hide();
    lv_obj_t *root() const { return root_; }
    lv_obj_t *canvas() const { return canvas_; }
    const HouseProjection &projection() const { return projection_; }
    HousePoint devicePoint(const std::string &id) const;
    void pointerDown(int x, int y);
    void pointerMove(int x, int y);
    void pointerUp(int x, int y);
    int levelFilter() const { return level_filter_; }
    int wallOpacity() const { return wall_opacity_; }
private:
    FloorPlan::Document plan_;
    std::vector<Device> devices_;
    std::vector<HouseWall> walls_;
    std::vector<std::vector<HouseAperture>> apertures_;
    HouseProjection projection_;
    lv_obj_t *root_ = nullptr, *canvas_ = nullptr, *status_ = nullptr, *walls_button_ = nullptr;
    lv_obj_t *chooser_ = nullptr;
    std::vector<lv_obj_t *> filters_;
    void *context_ = nullptr;
    DeviceHandler device_handler_ = nullptr;
    EditHandler edit_handler_ = nullptr;
    int level_filter_ = -1, wall_opacity_ = 70;
    int down_x_ = 0, down_y_ = 0;
    bool pressed_ = false, dragged_ = false;
    std::string selected_;
    std::unique_ptr<HouseScene> scene_;
    bool scene_dirty_ = true;
    struct Action;
    lv_obj_t *button(lv_obj_t *parent, const char *text, int action);
    void create(lv_obj_t *parent);
    void perform(int action);
    void fit();
    void chooseDevice(size_t index);
    void showChooser(const std::vector<size_t> &indices);
    const Device *device(const std::string &id) const;
    const PlanRoom *room(uint32_t id) const;
    bool visible(const PlanPlacement &placement) const;
    void draw(lv_layer_t *layer);
    void buildScene();
    static void event(lv_event_t *event);
};
