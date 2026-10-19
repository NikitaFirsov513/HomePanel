#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct PlanRect {
    int x = 0, y = 0, w = 8, h = 6;
    bool operator==(const PlanRect &b) const { return x == b.x && y == b.y && w == b.w && h == b.h; }
};
struct PlanRoom { uint32_t id = 0; std::string name; PlanRect rect; std::vector<PlanRect> parts; };
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
    int inset = 0; // Distance from the bounding-box edge to an inner contour wall.
    int span = 0; // Width in thousandths of a grid cell; zero uses the standard width.
};
struct PlanPoint { double x = 0, y = 0; };
struct PlanWall { int x1, y1, x2, y2; PlanEdge edge; };
struct PlanSegment { PlanPoint from, to; };
PlanSegment openingSegment(const PlanRoom &room, const PlanOpening &opening);
std::vector<PlanRect> roomParts(const PlanRoom &room);
bool roomContains(const PlanRoom &room, double x, double y);
bool roomCell(const PlanRoom &room, int x, int y);
std::vector<PlanWall> roomWalls(const PlanRoom &room);
PlanPoint roomInterior(const PlanRoom &room, PlanPoint point);
PlanPoint openingPosition(const PlanRoom &room, const PlanOpening &opening);
bool openingFits(const PlanRoom &room, const PlanOpening &opening);
bool normalizeRoom(PlanRoom &room);
PlanRoom transformRoom(const PlanRoom &room, const PlanRect &rect);
bool editedRoom(const PlanRoom &room, const PlanRect &area, bool remove, PlanRoom &result);

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
    bool canPlaceRoom(const PlanRoom &room, uint32_t ignore_room = 0) const;
    bool canEditRoom(uint32_t id, const PlanRect &area, bool remove) const;
    bool editRoom(uint32_t id, const PlanRect &area, bool remove);
    uint32_t addRoom(const PlanRect &rect);
    bool changeRoom(uint32_t id, const PlanRect &rect);
    bool renameRoom(uint32_t id, const std::string &name);
    bool removeRoom(uint32_t id);
    bool placeDevice(const PlanPlacement &placement);
    bool setLevel(const std::string &device_id, PlanLevel level);
    bool removeDevice(const std::string &device_id);
    uint32_t addOpening(uint32_t room_id, PlanEdge edge, int offset, bool window, int inset = 0, int span = 0);
    uint32_t moveOpening(uint32_t id, int cells);
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
