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
bool FloorPlan::canPlace(const PlanRect &rect, uint32_t ignore_room) const {
    return canPlaceRoom(PlanRoom{0, "", rect}, ignore_room);
}
bool FloorPlan::canPlaceRoom(const PlanRoom &candidate, uint32_t ignore_room) const {
    auto checked = candidate;
    if (!validRect(candidate.rect) || !normalizeRoom(checked) || !(checked.rect == candidate.rect)) return false;
    for (const auto &r : doc_.rooms) if (r.id != ignore_room && overlaps(candidate.rect, r.rect)) {
        const int x0=std::max(r.rect.x,candidate.rect.x), y0=std::max(r.rect.y,candidate.rect.y);
        const int x1=std::min(r.rect.x+r.rect.w,candidate.rect.x+candidate.rect.w), y1=std::min(r.rect.y+r.rect.h,candidate.rect.y+candidate.rect.h);
        for(int y=y0;y<y1;++y) for(int x=x0;x<x1;++x) if(roomCell(r,x,y) && roomCell(candidate,x,y)) return false;
    }
    return true;
}
namespace {
void rebase(FloorPlan::Document &doc, const PlanRoom &old, const PlanRoom &next, bool world) {
    for(auto &p:doc.devices) if(p.room_id==old.id) {
        const auto &base=world?old.rect:next.rect;
        const auto point=roomInterior(next,{base.x+base.w*p.u/1000.0,base.y+base.h*p.v/1000.0});
        p.u=std::clamp(int(std::lround((point.x-next.rect.x)*1000/next.rect.w)),0,1000);
        p.v=std::clamp(int(std::lround((point.y-next.rect.y)*1000/next.rect.h)),0,1000);
    }
    for(auto &o:doc.openings) if(o.room_id==old.id) {
        const bool horizontal=o.edge==PlanEdge::Top || o.edge==PlanEdge::Bottom;
        if(world) {
            const auto point=openingPosition(old,o);
            o.offset=int(std::lround((horizontal?(point.x-next.rect.x)/next.rect.w:(point.y-next.rect.y)/next.rect.h)*1000));
            o.inset=int(std::lround(o.edge==PlanEdge::Top?point.y-next.rect.y:o.edge==PlanEdge::Bottom?next.rect.y+next.rect.h-point.y:
                                   o.edge==PlanEdge::Left?point.x-next.rect.x:next.rect.x+next.rect.w-point.x));
        } else o.inset=int(std::lround(double(o.inset)*(horizontal?next.rect.h:next.rect.w)/(horizontal?old.rect.h:old.rect.w)));
    }
    doc.openings.erase(std::remove_if(doc.openings.begin(),doc.openings.end(),[&](const PlanOpening &o) {
        return o.room_id==old.id && (o.offset<50 || o.offset>950 || !openingFits(next,o));
    }),doc.openings.end());
}
}
bool FloorPlan::canEditRoom(uint32_t id,const PlanRect &area,bool remove) const {
    const auto *r=room(id); PlanRoom candidate;
    return r && editedRoom(*r,area,remove,candidate) && canPlaceRoom(candidate,id);
}
bool FloorPlan::editRoom(uint32_t id,const PlanRect &area,bool remove) {
    const auto *r=room(id); PlanRoom candidate;
    if(!r || !editedRoom(*r,area,remove,candidate) || !canPlaceRoom(candidate,id)) return false;
    if(candidate.rect==r->rect && candidate.parts==r->parts) return true;
    const auto old=*r; checkpoint(); rebase(doc_,old,candidate,true);
    for(auto &entry:doc_.rooms) if(entry.id==id) entry=std::move(candidate);
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
bool FloorPlan::changeRoom(uint32_t id, const PlanRect &rect) {
    const auto *r=room(id); if(!r || !validRect(rect)) return false;
    auto next=transformRoom(*r,rect);
    if(!canPlaceRoom(next,id) || !normalizeRoom(next)) return false;
    if(r->rect==next.rect && r->parts==next.parts) return true;
    const auto old=*r; checkpoint(); rebase(doc_,old,next,false);
    for(auto &entry:doc_.rooms) if(entry.id==id) entry=std::move(next);
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
    if(!roomContains(*room(p.room_id),room(p.room_id)->rect.x+room(p.room_id)->rect.w*p.u/1000.0,room(p.room_id)->rect.y+room(p.room_id)->rect.h*p.v/1000.0)) return false;
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
uint32_t FloorPlan::addOpening(uint32_t room_id, PlanEdge edge, int offset, bool window, int inset, int span) {
    const auto *r=room(room_id);
    if(!r || int(edge)<0 || int(edge)>3 || offset<50 || offset>950 || span<0 || span>40000 || (!window && span)) return 0;
    PlanOpening candidate{0,room_id,edge,offset,window,inset,span};
    if(!openingFits(*r,candidate)) return 0;
    const bool horizontal=edge==PlanEdge::Top || edge==PlanEdge::Bottom;
    const auto segment=openingSegment(*r,candidate);
    double from=horizontal?segment.from.x:segment.from.y, to=horizontal?segment.to.x:segment.to.y;
    std::vector<uint32_t> merged;
    if(window) {
        bool expanded=true;
        while(expanded) {
            expanded=false;
            for(const auto &o:doc_.openings) if(o.room_id==room_id && o.edge==edge && o.inset==inset && o.window &&
                std::find(merged.begin(),merged.end(),o.id)==merged.end()) {
                const auto range=openingSegment(*r,o);const double a=horizontal?range.from.x:range.from.y,b=horizontal?range.to.x:range.to.y;
                if(a<=to+0.02 && b>=from-0.02) {from=std::min(from,a);to=std::max(to,b);merged.push_back(o.id);expanded=true;}
            }
        }
    }
    for(const auto &o:doc_.openings) if(o.room_id==room_id && o.edge==edge && o.inset==inset && !(window && o.window)) {
        const auto range=openingSegment(*r,o);const double a=horizontal?range.from.x:range.from.y,b=horizontal?range.to.x:range.to.y;
        if(a<to-0.001 && b>from+0.001) return 0;
    }
    if(merged.empty() && doc_.openings.size()>=MAX_OPENINGS) return 0;
    if(!merged.empty()) {
        candidate.id=merged.front(); candidate.span=int(std::lround((to-from)*1000));
        candidate.offset=int(std::lround(((from+to)*0.5-(horizontal?r->rect.x:r->rect.y))*1000/(horizontal?r->rect.w:r->rect.h)));
        if(!openingFits(*r,candidate)) return 0;
        if(merged.size()==1) {
            const auto existing=std::find_if(doc_.openings.begin(),doc_.openings.end(),[&](const PlanOpening &o){return o.id==candidate.id;});
            const auto range=openingSegment(*r,*existing);
            if(std::abs((horizontal?range.from.x:range.from.y)-from)<0.001 && std::abs((horizontal?range.to.x:range.to.y)-to)<0.001) return candidate.id;
        }
    }
    checkpoint();
    if(merged.empty()) candidate.id=doc_.next_id++;
    else doc_.openings.erase(std::remove_if(doc_.openings.begin(),doc_.openings.end(),[&](const PlanOpening &o){return std::find(merged.begin(),merged.end(),o.id)!=merged.end();}),doc_.openings.end());
    doc_.openings.push_back(candidate);return candidate.id;
}
uint32_t FloorPlan::moveOpening(uint32_t id,int cells) {
    const auto found=std::find_if(doc_.openings.begin(),doc_.openings.end(),[&](const PlanOpening &o){return o.id==id;});
    if(found==doc_.openings.end() || !cells || cells < -40 || cells > 40) return 0;
    auto moved=*found; const auto *r=room(moved.room_id); const bool horizontal=moved.edge==PlanEdge::Top || moved.edge==PlanEdge::Bottom;
    moved.offset+=int(std::lround(double(cells)*1000/(horizontal?r->rect.w:r->rect.h)));
    FloorPlan next;next.doc_=doc_;
    next.doc_.openings.erase(std::remove_if(next.doc_.openings.begin(),next.doc_.openings.end(),[&](const PlanOpening &o){return o.id==id;}),next.doc_.openings.end());
    auto result=next.addOpening(moved.room_id,moved.edge,moved.offset,moved.window,moved.inset,moved.span);
    if(!result) return 0;
    if(result>=doc_.next_id) {for(auto &o:next.doc_.openings) if(o.id==result) o.id=id;result=id;}
    next.doc_.next_id=doc_.next_id;
    checkpoint();doc_=std::move(next.doc_);return result;
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
    const bool complex = std::any_of(doc_.rooms.begin(),doc_.rooms.end(),[](const PlanRoom &r){return !r.parts.empty();}) || std::any_of(doc_.openings.begin(),doc_.openings.end(),[](const PlanOpening &o){return o.span>0;});
    if (!root || !cJSON_AddNumberToObject(root.get(), "version", complex ? 2 : 1)) return {};
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
        if(!r.parts.empty()) {
            auto *parts=cJSON_AddArrayToObject(o,"parts"); if(!parts) return {};
            for(const auto &p:r.parts) { const int values[]={p.x,p.y,p.w,p.h}; auto *part=cJSON_CreateIntArray(values,4); if(!part) return {}; cJSON_AddItemToArray(parts,part); }
        }

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
        if(entry.inset && !cJSON_AddNumberToObject(o,"inset",entry.inset)) return {};
        if(entry.span && !cJSON_AddNumberToObject(o,"span",entry.span)) return {};
    }
    char *json = cJSON_PrintUnformatted(root.get()); if (!json) return {};
    std::string result(json); cJSON_free(json); return result;
}

bool FloorPlan::load(const std::string &json)
{
    if (json.empty() || json.size() > 32768 || json.find('\0') != std::string::npos) return false;
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_ParseWithOpts(json.c_str(), nullptr, true), &cJSON_Delete);
    int version;
    if (!integer(root.get(), "version", version, 1, 2)) return false;
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
            !integer(entry, "w", r.rect.w, 3, WIDTH) || !integer(entry, "h", r.rect.h, 3, HEIGHT)) return false;
        const auto *parts=cJSON_GetObjectItemCaseSensitive(entry,"parts");
        if(parts) {
            if(version<2 || !cJSON_IsArray(parts) || cJSON_GetArraySize(parts)<1 || cJSON_GetArraySize(parts)>64) return false;
            const cJSON *part=nullptr;
            cJSON_ArrayForEach(part,parts) {
                if(!cJSON_IsArray(part) || cJSON_GetArraySize(part)!=4) return false;
                int value[4];
                for(int i=0;i<4;++i) { const auto *v=cJSON_GetArrayItem(part,i); if(!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble) || v->valuedouble<0 || v->valuedouble>40 || std::floor(v->valuedouble)!=v->valuedouble) return false; value[i]=int(v->valuedouble); }
                r.parts.push_back({value[0],value[1],value[2],value[3]});
            }
        }
        if(!parsed.canPlaceRoom(r) || !normalizeRoom(r)) return false;
        r.id = id; parsed.doc_.rooms.push_back(r); parsed.doc_.next_id = std::max(parsed.doc_.next_id, r.id + 1);
    }
    cJSON_ArrayForEach(entry, devices) {
        PlanPlacement p; int id, level;
        if (!text(entry, "device_id", p.device_id, 180) || !text(entry, "name", p.name, 192) ||
            !text(entry, "type", p.type, 128) || !integer(entry, "room_id", id, 1, 1000000000) ||
            !integer(entry, "u", p.u, 0, 1000) || !integer(entry, "v", p.v, 0, 1000) ||
            !integer(entry, "level", level, 0, 2) || parsed.placement(p.device_id)) return false;
        p.room_id = id; p.level = static_cast<PlanLevel>(level);
        const auto *room=parsed.room(p.room_id);
        if (!room || !roomContains(*room,room->rect.x+room->rect.w*p.u/1000.0,room->rect.y+room->rect.h*p.v/1000.0)) return false;
        parsed.doc_.devices.push_back(p);
    }
    cJSON_ArrayForEach(entry, openings) {
        PlanOpening o; int id, room_id, edge;
        const auto *window = cJSON_GetObjectItemCaseSensitive(entry, "window");
        if (!integer(entry, "id", id, 1, 1000000000) || !ids.insert(id).second ||
            !integer(entry, "room_id", room_id, 1, 1000000000) || !parsed.room(room_id) ||
            !integer(entry, "edge", edge, 0, 3) || !integer(entry, "offset", o.offset, 50, 950) || !cJSON_IsBool(window)) return false;
        o.id = id; o.room_id = room_id; o.edge = static_cast<PlanEdge>(edge); o.window = cJSON_IsTrue(window);
        if(cJSON_GetObjectItemCaseSensitive(entry,"inset") && (version<2 || !integer(entry,"inset",o.inset,0,40))) return false;
        if(cJSON_GetObjectItemCaseSensitive(entry,"span") && (version<2 || !o.window || !integer(entry,"span",o.span,1,40000))) return false;
        if(!openingFits(*parsed.room(room_id),o)) return false;

        for (const auto &other : parsed.doc_.openings)
            if (other.room_id == o.room_id && other.edge == o.edge && other.inset == o.inset && std::abs(other.offset - o.offset) < 100) return false;
        parsed.doc_.openings.push_back(o); parsed.doc_.next_id = std::max(parsed.doc_.next_id, o.id + 1);
    }
    doc_ = std::move(parsed.doc_); history_.clear(); markSaved(); return true;
}
