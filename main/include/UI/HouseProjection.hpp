#pragma once
#include "FloorPlan.hpp"
#include <vector>

struct HousePoint { double x = 0, y = 0; };
struct HousePosition { double x = 0, y = 0, z = 0; };
struct HouseWall { double x1 = 0, y1 = 0, x2 = 0, y2 = 0; };
struct HouseAperture { double from = 0, to = 0; bool window = false; };

class HouseProjection {
public:
    static constexpr double WALL_HEIGHT = 5.6; // Grid units (0.5 m).
    int rotation = 0;
    double scale = 20, offset_x = 0, offset_y = 0;
    static double levelHeight(PlanLevel level);
    static HousePosition devicePosition(const PlanRoom &room, const PlanPlacement &device);
    HousePoint raw(HousePosition point) const;
    HousePoint project(HousePosition point) const;
    double depth(double x, double y) const;
    void fit(const FloorPlan::Document &plan, int width, int height);
};

// Shared room boundaries are rendered once, including partially shared walls.
std::vector<HouseWall> houseWalls(const FloorPlan::Document &plan);
std::vector<HouseAperture> wallApertures(const HouseWall &wall, const FloorPlan::Document &plan);
