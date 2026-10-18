#include "FloorPlan.hpp"
#include "cJSON.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <set>

namespace {
bool overlaps(const PlanRect &a, const PlanRect &b)
{
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}
bool validRect(const PlanRect &r)
{
    return r.x >= 0 && r.y >= 0 && r.w >= 3 && r.h >= 3 &&
           r.w <= FloorPlan::WIDTH && r.h <= FloorPlan::HEIGHT &&
           r.x <= FloorPlan::WIDTH - r.w && r.y <= FloorPlan::HEIGHT - r.h;
}
bool integer(const cJSON *obj, const char *key, int &out, int min, int max)
{
    const auto *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble) ||
        v->valuedouble < min || v->valuedouble > max || std::floor(v->valuedouble) != v->valuedouble) return false;
    out = static_cast<int>(v->valuedouble);
    return true;
}
bool text(const cJSON *obj, const char *key, std::string &out, size_t limit)
{
    const auto *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(v) || !v->valuestring || !v->valuestring[0]) return false;
    out = v->valuestring;
    return out.size() <= limit;
}
}

FloorPlan::FloorPlan() { markSaved(); }
const PlanRoom *FloorPlan::room(uint32_t id) const
{
    for (const auto &r : doc_.rooms) if (r.id == id) return &r;
    return nullptr;
}
const PlanPlacement *FloorPlan::placement(const std::string &id) const
{
    for (const auto &p : doc_.devices) if (p.device_id == id) return &p;
    return nullptr;
}
bool FloorPlan::canPlace(const PlanRect &rect, uint32_t ignore_room) const
{
    if (!validRect(rect)) return false;
    for (const auto &r : doc_.rooms) if (r.id != ignore_room && overlaps(rect, r.rect)) return false;
    return true;
}
void FloorPlan::checkpoint()
{
    if (history_.size() == 20) history_.erase(history_.begin());
    history_.push_back(doc_);
}
uint32_t FloorPlan::addRoom(const PlanRect &rect)
{
    if (doc_.rooms.size() >= MAX_ROOMS || !canPlace(rect)) return 0;
    checkpoint();
    const auto id = doc_.next_id++;
    doc_.rooms.push_back({id, "Комната " + std::to_string(id), rect});
    return id;
}
bool FloorPlan::changeRoom(uint32_t id, const PlanRect &rect)
{
    const auto *r = room(id);
    if (!r || !canPlace(rect, id)) return false;
    if (r->rect.x == rect.x && r->rect.y == rect.y && r->rect.w == rect.w && r->rect.h == rect.h) return true;
    checkpoint();
    for (auto &entry : doc_.rooms) if (entry.id == id) entry.rect = rect;
    return true;
}
bool FloorPlan::renameRoom(uint32_t id, const std::string &name)
{
    if (!room(id) || name.empty() || name.size() > 96 || name.find_first_not_of(" \t\r\n") == std::string::npos) return false;
    if (room(id)->name == name) return true;
    checkpoint();
    for (auto &r : doc_.rooms) if (r.id == id) r.name = name;
    return true;
}
bool FloorPlan::removeRoom(uint32_t id)
{
    if (!room(id)) return false;
    checkpoint();
    auto &rooms = doc_.rooms; auto &devices = doc_.devices; auto &openings = doc_.openings;
    rooms.erase(std::remove_if(rooms.begin(), rooms.end(), [&](const PlanRoom &r) { return r.id == id; }), rooms.end());
    devices.erase(std::remove_if(devices.begin(), devices.end(), [&](const PlanPlacement &p) { return p.room_id == id; }), devices.end());
    openings.erase(std::remove_if(openings.begin(), openings.end(), [&](const PlanOpening &o) { return o.room_id == id; }), openings.end());
    return true;
}
bool FloorPlan::placeDevice(const PlanPlacement &p)
{
    if (!room(p.room_id) || p.device_id.empty() || p.device_id.size() > 180 || p.name.empty() || p.name.size() > 192 ||
        p.type.empty() || p.type.size() > 128 || p.u < 0 || p.u > 1000 || p.v < 0 || p.v > 1000 ||
        static_cast<int>(p.level) < 0 || static_cast<int>(p.level) > 2) return false;
    if (!placement(p.device_id) && doc_.devices.size() >= MAX_DEVICES) return false;
    checkpoint();
    for (auto &entry : doc_.devices) if (entry.device_id == p.device_id) { entry = p; return true; }
    doc_.devices.push_back(p);
    return true;
}
bool FloorPlan::setLevel(const std::string &id, PlanLevel level)
{
    const auto *p = placement(id);
    if (!p || static_cast<int>(level) < 0 || static_cast<int>(level) > 2) return false;
    if (p->level == level) return true;
    checkpoint();
    for (auto &entry : doc_.devices) if (entry.device_id == id) entry.level = level;
    return true;
}
bool FloorPlan::removeDevice(const std::string &id)
{
    if (!placement(id)) return false;
    checkpoint();
    auto &devices = doc_.devices;
    devices.erase(std::remove_if(devices.begin(), devices.end(), [&](const PlanPlacement &p) { return p.device_id == id; }), devices.end());
    return true;
}
uint32_t FloorPlan::addOpening(uint32_t room_id, PlanEdge edge, int offset, bool window)
{
    if (!room(room_id) || static_cast<int>(edge) < 0 || static_cast<int>(edge) > 3 || offset < 50 || offset > 950 ||
        doc_.openings.size() >= MAX_OPENINGS) return 0;
    for (const auto &o : doc_.openings)
        if (o.room_id == room_id && o.edge == edge && std::abs(o.offset - offset) < 100) return 0;
    checkpoint();
    const auto id = doc_.next_id++;
    doc_.openings.push_back({id, room_id, edge, offset, window});
    return id;
}
bool FloorPlan::removeOpening(uint32_t id)
{
    const auto it = std::find_if(doc_.openings.begin(), doc_.openings.end(), [&](const PlanOpening &o) { return o.id == id; });
    if (it == doc_.openings.end()) return false;
    checkpoint();
    doc_.openings.erase(std::remove_if(doc_.openings.begin(), doc_.openings.end(), [&](const PlanOpening &o) { return o.id == id; }), doc_.openings.end());
    return true;
}
bool FloorPlan::undo()
{
    if (history_.empty()) return false;
    doc_ = std::move(history_.back()); history_.pop_back(); return true;
}
bool FloorPlan::dirty() const { return serialize() != saved_; }
void FloorPlan::markSaved() { saved_ = serialize(); }

std::string FloorPlan::serialize() const
{
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_CreateObject(), &cJSON_Delete);
    if (!root || !cJSON_AddNumberToObject(root.get(), "version", 1)) return {};
    auto *rooms = cJSON_AddArrayToObject(root.get(), "rooms");
    auto *devices = cJSON_AddArrayToObject(root.get(), "devices");
    auto *openings = cJSON_AddArrayToObject(root.get(), "openings");
    if (!rooms || !devices || !openings) return {};
    for (const auto &r : doc_.rooms) {
        auto *o = cJSON_CreateObject(); if (!o) return {};
        cJSON_AddItemToArray(rooms, o);
        if (!cJSON_AddNumberToObject(o, "id", r.id) || !cJSON_AddStringToObject(o, "name", r.name.c_str()) ||
            !cJSON_AddNumberToObject(o, "x", r.rect.x) || !cJSON_AddNumberToObject(o, "y", r.rect.y) ||
            !cJSON_AddNumberToObject(o, "w", r.rect.w) || !cJSON_AddNumberToObject(o, "h", r.rect.h)) return {};
    }
    for (const auto &p : doc_.devices) {
        auto *o = cJSON_CreateObject(); if (!o) return {};
        cJSON_AddItemToArray(devices, o);
        if (!cJSON_AddStringToObject(o, "device_id", p.device_id.c_str()) || !cJSON_AddStringToObject(o, "name", p.name.c_str()) ||
            !cJSON_AddStringToObject(o, "type", p.type.c_str()) || !cJSON_AddNumberToObject(o, "room_id", p.room_id) ||
            !cJSON_AddNumberToObject(o, "u", p.u) || !cJSON_AddNumberToObject(o, "v", p.v) ||
            !cJSON_AddNumberToObject(o, "level", static_cast<int>(p.level))) return {};
    }
    for (const auto &entry : doc_.openings) {
        auto *o = cJSON_CreateObject(); if (!o) return {};
        cJSON_AddItemToArray(openings, o);
        if (!cJSON_AddNumberToObject(o, "id", entry.id) || !cJSON_AddNumberToObject(o, "room_id", entry.room_id) ||
            !cJSON_AddNumberToObject(o, "edge", static_cast<int>(entry.edge)) || !cJSON_AddNumberToObject(o, "offset", entry.offset) ||
            !cJSON_AddBoolToObject(o, "window", entry.window)) return {};
    }
    char *json = cJSON_PrintUnformatted(root.get()); if (!json) return {};
    std::string result(json); cJSON_free(json); return result;
}

bool FloorPlan::load(const std::string &json)
{
    if (json.empty() || json.size() > 32768 || json.find('\0') != std::string::npos) return false;
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_ParseWithOpts(json.c_str(), nullptr, true), &cJSON_Delete);
    int version;
    if (!integer(root.get(), "version", version, 1, 1)) return false;
    const auto *rooms = cJSON_GetObjectItemCaseSensitive(root.get(), "rooms");
    const auto *devices = cJSON_GetObjectItemCaseSensitive(root.get(), "devices");
    const auto *openings = cJSON_GetObjectItemCaseSensitive(root.get(), "openings");
    if (!cJSON_IsArray(rooms) || !cJSON_IsArray(devices) || !cJSON_IsArray(openings) ||
        cJSON_GetArraySize(rooms) > MAX_ROOMS || cJSON_GetArraySize(devices) > MAX_DEVICES ||
        cJSON_GetArraySize(openings) > MAX_OPENINGS) return false;
    FloorPlan parsed;
    std::set<int> ids;
    const cJSON *entry = nullptr;
    cJSON_ArrayForEach(entry, rooms) {
        PlanRoom r; int id;
        if (!integer(entry, "id", id, 1, 1000000000) || !ids.insert(id).second || !text(entry, "name", r.name, 96) ||
            !integer(entry, "x", r.rect.x, 0, WIDTH) || !integer(entry, "y", r.rect.y, 0, HEIGHT) ||
            !integer(entry, "w", r.rect.w, 3, WIDTH) || !integer(entry, "h", r.rect.h, 3, HEIGHT) ||
            !parsed.canPlace(r.rect)) return false;
        r.id = id; parsed.doc_.rooms.push_back(r); parsed.doc_.next_id = std::max(parsed.doc_.next_id, r.id + 1);
    }
    cJSON_ArrayForEach(entry, devices) {
        PlanPlacement p; int id, level;
        if (!text(entry, "device_id", p.device_id, 180) || !text(entry, "name", p.name, 192) ||
            !text(entry, "type", p.type, 128) || !integer(entry, "room_id", id, 1, 1000000000) ||
            !integer(entry, "u", p.u, 0, 1000) || !integer(entry, "v", p.v, 0, 1000) ||
            !integer(entry, "level", level, 0, 2) || parsed.placement(p.device_id)) return false;
        p.room_id = id; p.level = static_cast<PlanLevel>(level);
        if (!parsed.room(p.room_id)) return false;
        parsed.doc_.devices.push_back(p);
    }
    cJSON_ArrayForEach(entry, openings) {
        PlanOpening o; int id, room_id, edge;
        const auto *window = cJSON_GetObjectItemCaseSensitive(entry, "window");
        if (!integer(entry, "id", id, 1, 1000000000) || !ids.insert(id).second ||
            !integer(entry, "room_id", room_id, 1, 1000000000) || !parsed.room(room_id) ||
            !integer(entry, "edge", edge, 0, 3) || !integer(entry, "offset", o.offset, 50, 950) || !cJSON_IsBool(window)) return false;
        o.id = id; o.room_id = room_id; o.edge = static_cast<PlanEdge>(edge); o.window = cJSON_IsTrue(window);
        for (const auto &other : parsed.doc_.openings)
            if (other.room_id == o.room_id && other.edge == o.edge && std::abs(other.offset - o.offset) < 100) return false;
        parsed.doc_.openings.push_back(o); parsed.doc_.next_id = std::max(parsed.doc_.next_id, o.id + 1);
    }
    doc_ = std::move(parsed.doc_); history_.clear(); markSaved(); return true;
}
