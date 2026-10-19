#include "FloorPlan.hpp"
#include "HouseProjection.hpp"
#include "cJSON.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #c); exit(1); } } while(false)
int main()
{
    FloorPlan plan;
    CHECK(!plan.dirty() && !plan.canUndo());
    CHECK(!plan.addRoom({-1, 0, 8, 6}));
    CHECK(!plan.addRoom({0, 0, 2, 6}));
    const auto a = plan.addRoom({2, 2, 8, 6}); CHECK(a);
    const auto b = plan.addRoom({10, 2, 8, 6}); CHECK(b); // Shared walls are allowed.
    CHECK(!plan.addRoom({4, 4, 8, 6}));
    CHECK(!plan.changeRoom(a, {3, 2, 8, 6})); // No overlapping rooms.
    CHECK(plan.renameRoom(a, "Гостиная"));
    CHECK(!plan.renameRoom(a, " "));
    PlanPlacement p; p.device_id = "yandex:lamp"; p.name = "Лампа"; p.type = "devices.types.light";
    p.room_id = a; p.u = 500; p.v = 750; p.level = PlanLevel::Ceiling;
    CHECK(plan.placeDevice(p));
    p.u = 1001; CHECK(!plan.placeDevice(p)); p.u = 500;
    p.room_id = 999; CHECK(!plan.placeDevice(p)); p.room_id = b;
    CHECK(plan.placeDevice(p) && plan.document().devices.size() == 1);
    CHECK(plan.undo() && plan.placement(p.device_id)->room_id == a);
    CHECK(plan.setLevel(p.device_id, PlanLevel::Floor));
    const auto door = plan.addOpening(a, PlanEdge::Right, 500, false); CHECK(door);
    CHECK(!plan.addOpening(a, PlanEdge::Right, 520, true));
    CHECK(plan.addOpening(b, PlanEdge::Top, 500, true));
    const auto json = plan.serialize(); CHECK(!json.empty() && plan.dirty());
    plan.markSaved(); CHECK(!plan.dirty());
    FloorPlan restored; CHECK(restored.load(json));
    CHECK(restored.serialize() == json && !restored.dirty() && !restored.canUndo());
    CHECK(restored.document().rooms[0].name == "Гостиная");
    CHECK(restored.placement(p.device_id)->level == PlanLevel::Floor);
    CHECK(restored.changeRoom(a, {2, 2, 7, 5}));
    CHECK(restored.placement(p.device_id)->u == 500 && restored.placement(p.device_id)->v == 750);
    CHECK(restored.removeRoom(a));
    CHECK(!restored.placement(p.device_id) && restored.document().openings.size() == 1);
    CHECK(restored.undo() && restored.placement(p.device_id));
    CHECK(restored.removeDevice(p.device_id) && restored.undo());
    CHECK(restored.removeOpening(door) && restored.undo());
    const auto before = restored.serialize();
    const char *bad[] = {"null", "{}", "{", "[]", R"({"version":99,"rooms":[],"devices":[],"openings":[]})",
        R"({"version":1,"rooms":[{"id":1,"name":"A","x":0,"y":0,"w":8,"h":6},{"id":2,"name":"B","x":1,"y":1,"w":8,"h":6}],"devices":[],"openings":[]})",
        R"({"version":1,"rooms":[{"id":1,"name":"A","x":1.5,"y":0,"w":8,"h":6}],"devices":[],"openings":[]})",
        R"({"version":1,"rooms":[],"devices":[{"device_id":"a","name":"L","type":"light","room_id":1,"u":500,"v":500,"level":1}],"openings":[]})"};
    for (const auto *value : bad) { CHECK(!restored.load(value)); CHECK(restored.serialize() == before); }
    CHECK(!restored.load(json + "garbage") && restored.serialize() == before);
    FloorPlan limited;
    for (int i = 0; i < 25; ++i) { const auto id = limited.addRoom({i % 8 * 5, i / 8 * 5, 3, 3}); CHECK(bool(id) == (i < 24)); }
    for (int i = 0; i < 30; ++i) limited.renameRoom(1, "Name " + std::to_string(i));
    int undos = 0; while (limited.undo()) ++undos;
    CHECK(undos == 20);
    FloorPlan house;
    const auto left = house.addRoom({2, 2, 8, 6});
    house.addRoom({10, 2, 8, 6});
    const auto walls = houseWalls(house.document());
    double wall_length = 0;
    for (const auto &wall : walls) wall_length += std::abs(wall.x2 - wall.x1) + std::abs(wall.y2 - wall.y1);
    CHECK(walls.size() == 5 && wall_length == 50); // Shared six-cell wall appears once.
    house.addOpening(left, PlanEdge::Right, 500, false);
    house.addOpening(left, PlanEdge::Top, 500, true);
    int doors = 0, windows = 0;
    for (const auto &wall : walls) for (const auto &opening : wallApertures(wall, house.document())) {
        CHECK(opening.to > opening.from);
        if (opening.window) { ++windows; CHECK(std::abs(opening.to - opening.from - 2.4) < 0.001); }
        else { ++doors; CHECK(std::abs(opening.to - opening.from - 1.6) < 0.001); }
    }
    CHECK(doors == 1 && windows == 1);
    HouseProjection projection;
    for (int rotation = 0; rotation < 4; ++rotation) {
        projection.rotation = rotation;
        projection.fit(house.document(), 960, 420);
        for (const auto &room : house.document().rooms) for (int corner = 0; corner < 4; ++corner) {
            for (double z : {0.0, HouseProjection::WALL_HEIGHT}) {
                const auto point = projection.project({double(room.rect.x + (corner & 1 ? room.rect.w : 0)),
                    double(room.rect.y + (corner & 2 ? room.rect.h : 0)), z});
                CHECK(point.x >= 20 && point.x <= 940 && point.y >= 20 && point.y <= 400);
            }
        }
        PlanPlacement pin; pin.u = 500; pin.v = 500;
        pin.level = PlanLevel::Floor; const auto floor = projection.project(HouseProjection::devicePosition(*house.room(left), pin));
        pin.level = PlanLevel::Middle; const auto middle = projection.project(HouseProjection::devicePosition(*house.room(left), pin));
        pin.level = PlanLevel::Ceiling; const auto ceiling = projection.project(HouseProjection::devicePosition(*house.room(left), pin));
        CHECK(std::abs(floor.x - middle.x) < 0.001 && std::abs(floor.x - ceiling.x) < 0.001);
        CHECK(floor.y > middle.y && middle.y > ceiling.y);
        CHECK(std::abs(floor.y - ceiling.y - projection.scale * 5.0) < 0.001);
    }
    FloorPlan shaped;
    const auto shape=shaped.addRoom({2,2,10,10}); CHECK(shape);
    PlanPlacement pin; pin.room_id=shape;pin.device_id="shape-lamp";pin.name="Lamp";pin.type="light";pin.u=800;pin.v=200;pin.level=PlanLevel::Ceiling;
    CHECK(shaped.placeDevice(pin));
    const auto removed_door=shaped.addOpening(shape,PlanEdge::Top,800,false); CHECK(removed_door);
    CHECK(shaped.editRoom(shape,{7,2,5,5},true));
    CHECK(!shaped.room(shape)->parts.empty() && !roomContains(*shaped.room(shape),9,4));
    CHECK(roomContains(*shaped.room(shape),4,4) && roomContains(*shaped.room(shape),9,9));
    CHECK(shaped.document().openings.empty());
    const auto relocated=HouseProjection::devicePosition(*shaped.room(shape),*shaped.placement(pin.device_id));
    CHECK(roomContains(*shaped.room(shape),relocated.x,relocated.y) && relocated.z==HouseProjection::levelHeight(PlanLevel::Ceiling));
    pin.u=800;pin.v=200; CHECK(!shaped.placeDevice(pin));
    CHECK(shaped.canPlace({7,2,5,5}));
    const auto neighbour=shaped.addRoom({7,2,5,5});CHECK(neighbour);
    CHECK(!shaped.editRoom(shape,{7,2,5,5},false)); CHECK(shaped.removeRoom(neighbour));
    const auto inner_door=shaped.addOpening(shape,PlanEdge::Top,700,false,5);CHECK(inner_door);
    CHECK(shaped.addOpening(shape,PlanEdge::Right,200,true,5));
    CHECK(!shaped.editRoom(shape,{8,3,2,2},true)); // No-op cuts do not create history entries.
    CHECK(!shaped.addOpening(shape,PlanEdge::Top,800,false)); // Missing outer wall.
    int inner_apertures=0;
    for(const auto &wall:houseWalls(shaped.document())) for(const auto &ap:wallApertures(wall,shaped.document())) {++inner_apertures;CHECK(ap.to>ap.from);}
    CHECK(inner_apertures==2);
    const auto shaped_json=shaped.serialize(); FloorPlan loaded_shape;CHECK(loaded_shape.load(shaped_json));
    CHECK(loaded_shape.serialize()==shaped_json && !loaded_shape.room(shape)->parts.empty());
    const auto before_extend=HouseProjection::devicePosition(*shaped.room(shape),*shaped.placement("shape-lamp"));
    CHECK(shaped.editRoom(shape,{12,7,4,5},false));
    const auto after_extend=HouseProjection::devicePosition(*shaped.room(shape),*shaped.placement("shape-lamp"));
    CHECK(std::hypot(before_extend.x-after_extend.x,before_extend.y-after_extend.y)<0.02);
    CHECK(shaped.undo() && shaped.serialize()==shaped_json);
    CHECK(!shaped.editRoom(shape,{25,2,3,3},false)); // Detached islands are not rooms.
    CHECK(!shaped.editRoom(shape,{4,2,1,10},true)); // Cannot split the room.
    CHECK(!shaped.editRoom(shape,{2,2,10,10},true));
    CHECK(shaped.editRoom(shape,{7,2,5,5},false) && shaped.room(shape)->parts.empty());
    CHECK(shaped.document().openings.empty() && shaped.undo() && shaped.serialize()==shaped_json);
    CHECK(shaped.changeRoom(shape,{3,3,12,12}));
    CHECK(!roomContains(*shaped.room(shape),13,5));
    FloorPlan ring; const auto ring_id=ring.addRoom({2,2,12,12}); CHECK(ring.editRoom(ring_id,{6,6,4,4},true));
    CHECK(!roomContains(*ring.room(ring_id),8,8) && roomWalls(*ring.room(ring_id)).size()==8);
    FloorPlan ring_loaded;CHECK(ring_loaded.load(ring.serialize()));
    CHECK(!ring_loaded.load(R"({"version":2,"rooms":[{"id":1,"name":"bad","x":2,"y":2,"w":10,"h":10,"parts":[[2,2,3,3],[9,9,3,3]]}],"devices":[],"openings":[]})"));
    { FloorPlan windows; const auto wr=windows.addRoom({2,2,10,10});CHECK(wr);
    const auto first=windows.addOpening(wr,PlanEdge::Top,250,true);CHECK(first);
    CHECK(windows.addOpening(wr,PlanEdge::Top,450,true)==first);
    CHECK(windows.addOpening(wr,PlanEdge::Top,650,true)==first);
    CHECK(windows.document().openings.size()==1 && windows.document().openings[0].span==6400);
    const auto wide=openingSegment(*windows.room(wr),windows.document().openings[0]);
    CHECK(std::abs(wide.to.x-wide.from.x-6.4)<0.01);
    const auto windows_json=windows.serialize();FloorPlan reloaded;CHECK(reloaded.load(windows_json));CHECK(reloaded.serialize()==windows_json);
    CHECK(!windows.addOpening(wr,PlanEdge::Top,500,false));
    CHECK(windows.moveOpening(first,1)==first);CHECK(windows.document().openings[0].offset==550);
    CHECK(windows.undo() && windows.serialize()==windows_json);
    CHECK(windows.undo() && windows.document().openings[0].span==4400);
    CHECK(windows.addOpening(wr,PlanEdge::Bottom,500,true));CHECK(windows.document().openings.size()==2);
    FloorPlan separated;const auto sr=separated.addRoom({2,2,16,10});CHECK(sr);
    const auto sw=separated.addOpening(sr,PlanEdge::Top,200,true);CHECK(sw);
    CHECK(separated.addOpening(sr,PlanEdge::Top,700,true));CHECK(separated.document().openings.size()==2);
    CHECK(separated.moveOpening(sw,1)==sw);CHECK(separated.document().openings.size()==2);
    CHECK(!separated.moveOpening(sw,-20));
    }
    puts("Floor plan persistence, geometry, projection and undo tests passed");
}
