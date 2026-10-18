#include "HouseProjection.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>

double HouseProjection::levelHeight(PlanLevel level)
{
    switch (level) {
    case PlanLevel::Floor: return 0.3;
    case PlanLevel::Ceiling: return 5.3;
    default: return 2.8;
    }
}
HousePosition HouseProjection::devicePosition(const PlanRoom &room, const PlanPlacement &device)
{
    return {room.rect.x + room.rect.w * device.u / 1000.0,
            room.rect.y + room.rect.h * device.v / 1000.0, levelHeight(device.level)};
}
namespace {
HousePoint rotate(double x, double y, int rotation)
{
    switch ((rotation % 4 + 4) % 4) {
    case 1: return {-y, x};
    case 2: return {-x, -y};
    case 3: return {y, -x};
    default: return {x, y};
    }
}
}
HousePoint HouseProjection::raw(HousePosition point) const
{
    const auto p = rotate(point.x, point.y, rotation);
    return {(p.x - p.y) * 0.866025403784, (p.x + p.y) * 0.5 - point.z};
}
HousePoint HouseProjection::project(HousePosition point) const
{
    const auto p = raw(point);
    return {offset_x + p.x * scale, offset_y + p.y * scale};
}
double HouseProjection::depth(double x, double y) const
{
    const auto p = rotate(x, y, rotation);
    return p.x + p.y;
}
void HouseProjection::fit(const FloorPlan::Document &plan, int width, int height)
{
    double min_x = std::numeric_limits<double>::max(), min_y = min_x;
    double max_x = -min_x, max_y = -min_x;
    for (const auto &room : plan.rooms) {
        for (int corner = 0; corner < 4; ++corner) {
            for (double z : {0.0, WALL_HEIGHT}) {
                const auto p = raw({double(room.rect.x + (corner & 1 ? room.rect.w : 0)),
                                    double(room.rect.y + (corner & 2 ? room.rect.h : 0)), z});
                min_x = std::min(min_x, p.x); max_x = std::max(max_x, p.x);
                min_y = std::min(min_y, p.y); max_y = std::max(max_y, p.y);
            }
        }
    }
    if (plan.rooms.empty()) { min_x = min_y = -5; max_x = max_y = 5; }
    scale = std::clamp(std::min(std::max(20, width - 100) / std::max(1.0, max_x - min_x),
                                std::max(20, height - 90) / std::max(1.0, max_y - min_y)), 2.0, 48.0);
    offset_x = width * 0.5 - (min_x + max_x) * scale * 0.5;
    offset_y = height * 0.5 - (min_y + max_y) * scale * 0.5;
}
std::vector<HouseWall> houseWalls(const FloorPlan::Document &plan)
{
    // (vertical, fixed coordinate, interval start), at half-metre resolution.
    std::set<std::tuple<bool, int, int>> edges;
    for (const auto &room : plan.rooms) {
        const auto &r = room.rect;
        for (int x = r.x; x < r.x + r.w; ++x) { edges.emplace(false, r.y, x); edges.emplace(false, r.y + r.h, x); }
        for (int y = r.y; y < r.y + r.h; ++y) { edges.emplace(true, r.x, y); edges.emplace(true, r.x + r.w, y); }
    }
    std::vector<HouseWall> result;
    for (auto it = edges.begin(); it != edges.end();) {
        const bool vertical = std::get<0>(*it); const int fixed = std::get<1>(*it);
        const int start = std::get<2>(*it); int end = start + 1;
        ++it;
        while (it != edges.end() && std::get<0>(*it) == vertical && std::get<1>(*it) == fixed && std::get<2>(*it) == end) { ++end; ++it; }
        result.push_back(vertical ? HouseWall{double(fixed), double(start), double(fixed), double(end)} :
                                    HouseWall{double(start), double(fixed), double(end), double(fixed)});
    }
    return result;
}
std::vector<HouseAperture> wallApertures(const HouseWall &wall, const FloorPlan::Document &plan)
{
    const bool vertical = wall.x1 == wall.x2;
    const double start = vertical ? wall.y1 : wall.x1, end = vertical ? wall.y2 : wall.x2;
    std::vector<HouseAperture> result;
    for (const auto &o : plan.openings) {
        const auto room = std::find_if(plan.rooms.begin(), plan.rooms.end(), [&](const PlanRoom &r) { return r.id == o.room_id; });
        if (room == plan.rooms.end()) continue;
        const bool ov = o.edge == PlanEdge::Left || o.edge == PlanEdge::Right;
        if (ov != vertical) continue;
        const auto &r = room->rect;
        const double fixed = ov ? r.x + (o.edge == PlanEdge::Right ? r.w : 0) : r.y + (o.edge == PlanEdge::Bottom ? r.h : 0);
        if (std::abs(fixed - (vertical ? wall.x1 : wall.y1)) > 0.001) continue;
        const double center = ov ? r.y + r.h * o.offset / 1000.0 : r.x + r.w * o.offset / 1000.0;
        const double half = o.window ? 1.2 : 0.8;
        const double from = std::max(start, center - half), to = std::min(end, center + half);
        if (to > from) result.push_back({from - start, to - start, o.window});
    }
    return result;
}
