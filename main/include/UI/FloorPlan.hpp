#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct PlanRect { int x = 0, y = 0, w = 8, h = 6; };
struct PlanRoom { uint32_t id = 0; std::string name; PlanRect rect; };
enum class PlanLevel { Floor, Middle, Ceiling };
enum class PlanEdge { Top, Right, Bottom, Left };
struct PlanPlacement {
    std::string device_id, name, type;
    uint32_t room_id = 0;
    int u = 500, v = 500; // Room-relative coordinates, 0..1000.
    PlanLevel level = PlanLevel::Middle;
};
struct PlanOpening {
    uint32_t id = 0, room_id = 0;
    PlanEdge edge = PlanEdge::Top;
    int offset = 500;
    bool window = false;
};

class FloorPlan {
public:
    static constexpr int WIDTH = 40, HEIGHT = 30; // One grid cell is 0.5 m.
    static constexpr size_t MAX_ROOMS = 24, MAX_DEVICES = 64, MAX_OPENINGS = 64;
    struct Document {
        std::vector<PlanRoom> rooms;
        std::vector<PlanPlacement> devices;
        std::vector<PlanOpening> openings;
        uint32_t next_id = 1;
    };
    FloorPlan();
    const Document &document() const { return doc_; }
    const PlanRoom *room(uint32_t id) const;
    const PlanPlacement *placement(const std::string &id) const;
    bool canPlace(const PlanRect &rect, uint32_t ignore_room = 0) const;
    uint32_t addRoom(const PlanRect &rect);
    bool changeRoom(uint32_t id, const PlanRect &rect);
    bool renameRoom(uint32_t id, const std::string &name);
    bool removeRoom(uint32_t id);
    bool placeDevice(const PlanPlacement &placement);
    bool setLevel(const std::string &device_id, PlanLevel level);
    bool removeDevice(const std::string &device_id);
    uint32_t addOpening(uint32_t room_id, PlanEdge edge, int offset, bool window);
    bool removeOpening(uint32_t id);
    bool undo();
    bool canUndo() const { return !history_.empty(); }
    bool dirty() const;
    void markSaved();
    std::string serialize() const;
    bool load(const std::string &json);
private:
    Document doc_;
    std::vector<Document> history_;
    std::string saved_;
    void checkpoint();
};
