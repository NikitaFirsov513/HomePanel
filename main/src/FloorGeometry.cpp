#include "FloorPlan.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>

namespace {
constexpr int W = FloorPlan::WIDTH, H = FloorPlan::HEIGHT;
bool validPart(const PlanRect &r) {
    return r.x >= 0 && r.y >= 0 && r.w >= 1 && r.h >= 1 && r.w <= W && r.h <= H && r.x <= W-r.w && r.y <= H-r.h;
}
bool fromCells(PlanRoom &room, const std::vector<uint8_t> &cells) {
    int x0=W, y0=H, x1=0, y1=0, count=0, first=-1;
    for (int y=0; y<H; ++y) for (int x=0; x<W; ++x) if (cells[y*W+x]) {
        x0=std::min(x0,x); y0=std::min(y0,y); x1=std::max(x1,x+1); y1=std::max(y1,y+1); ++count; first=y*W+x;
    }
    if (!count || x1-x0<3 || y1-y0<3) return false;
    auto remaining=cells; std::vector<int> queue{first}; remaining[first]=0;
    for (size_t i=0; i<queue.size(); ++i) {
        const int p=queue[i], x=p%W, y=p/W;
        for (const auto n : {PlanPoint{double(x-1),double(y)}, {double(x+1),double(y)}, {double(x),double(y-1)}, {double(x),double(y+1)}}) {
            const int nx=int(n.x), ny=int(n.y);
            if (nx>=0 && nx<W && ny>=0 && ny<H && remaining[ny*W+nx]) { remaining[ny*W+nx]=0; queue.push_back(ny*W+nx); }
        }
    }
    if (queue.size()!=size_t(count)) return false;
    std::vector<PlanRect> parts;
    for (int y=y0; y<y1; ++y) for (int x=x0; x<x1;) {
        if (!cells[y*W+x]) { ++x; continue; }
        const int start=x; while(x<x1 && cells[y*W+x]) ++x;
        auto it=std::find_if(parts.begin(),parts.end(),[&](const PlanRect &p){return p.x==start && p.w==x-start && p.y+p.h==y;});
        if(it!=parts.end()) ++it->h; else parts.push_back({start,y,x-start,1});
    }
    if(parts.size()>64) return false;
    room.rect={x0,y0,x1-x0,y1-y0}; room.parts=parts.size()==1 ? std::vector<PlanRect>{} : std::move(parts); return true;
}
}
std::vector<PlanRect> roomParts(const PlanRoom &r) { return r.parts.empty() ? std::vector<PlanRect>{r.rect} : r.parts; }
bool roomCell(const PlanRoom &r,int x,int y) {
    if(r.parts.empty()) return x>=r.rect.x && y>=r.rect.y && x<r.rect.x+r.rect.w && y<r.rect.y+r.rect.h;
    for(const auto &p:r.parts) if(x>=p.x && y>=p.y && x<p.x+p.w && y<p.y+p.h) return true;
    return false;
}
bool roomContains(const PlanRoom &r,double x,double y) {
    for(const auto &p:roomParts(r)) if(x>=p.x-1e-7 && y>=p.y-1e-7 && x<=p.x+p.w+1e-7 && y<=p.y+p.h+1e-7) return true;
    return false;
}
bool normalizeRoom(PlanRoom &r) {
    if(r.parts.size()>64) return false;
    std::vector<uint8_t> cells(W*H,0);
    for(const auto &p:roomParts(r)) {
        if(!validPart(p)) return false;
        for(int y=p.y;y<p.y+p.h;++y) for(int x=p.x;x<p.x+p.w;++x) cells[y*W+x]=1;
    }
    return fromCells(r,cells);
}
bool editedRoom(const PlanRoom &r,const PlanRect &a,bool remove,PlanRoom &out) {
    if(!validPart(a)) return false;
    std::vector<uint8_t> cells(W*H,0);
    for(int y=0;y<H;++y) for(int x=0;x<W;++x) cells[y*W+x]=roomCell(r,x,y);
    bool changed=false;
    for(int y=a.y;y<a.y+a.h;++y) for(int x=a.x;x<a.x+a.w;++x) { if(cells[y*W+x]!=!remove) changed=true; cells[y*W+x]=!remove; }
    if(!changed) return false;
    out=r; return fromCells(out,cells);
}
PlanRoom transformRoom(const PlanRoom &r,const PlanRect &rect) {
    PlanRoom out=r; out.rect=rect;
    for(auto &p:out.parts) {
        const int x=rect.x+int(std::lround(double(p.x-r.rect.x)*rect.w/r.rect.w));
        const int y=rect.y+int(std::lround(double(p.y-r.rect.y)*rect.h/r.rect.h));
        const int endx=rect.x+int(std::lround(double(p.x+p.w-r.rect.x)*rect.w/r.rect.w));
        const int endy=rect.y+int(std::lround(double(p.y+p.h-r.rect.y)*rect.h/r.rect.h));
        p={x,y,endx-x,endy-y};
    }
    return out;
}
PlanPoint roomInterior(const PlanRoom &r,PlanPoint point) {
    if(roomContains(r,point.x,point.y)) return point;
    PlanPoint best; double distance=std::numeric_limits<double>::max();
    for(const auto &p:roomParts(r)) {
        PlanPoint candidate{std::clamp(point.x,p.x+0.15,p.x+p.w-0.15),std::clamp(point.y,p.y+0.15,p.y+p.h-0.15)};
        const double d=std::hypot(candidate.x-point.x,candidate.y-point.y);
        if(d<distance) {distance=d;best=candidate;}
    }
    return best;
}
std::vector<PlanWall> roomWalls(const PlanRoom &r) {
    std::vector<PlanWall> walls;
    // Scan contiguous boundary runs directly; avoid a map allocation per wall cell.
    for(int edge=0;edge<4;++edge) {
        const bool horizontal=edge%2==0;
        const int fixed0=horizontal?r.rect.y:r.rect.x, fixed1=fixed0+(horizontal?r.rect.h:r.rect.w);
        const int along0=horizontal?r.rect.x:r.rect.y, along1=along0+(horizontal?r.rect.w:r.rect.h);
        for(int fixed=fixed0;fixed<=fixed1;++fixed) {
            int start=-1;
            for(int along=along0;along<=along1;++along) {
                bool boundary=false;
                if(along<along1) {
                    if(edge==0) boundary=roomCell(r,along,fixed)&&!roomCell(r,along,fixed-1);
                    if(edge==1) boundary=roomCell(r,fixed-1,along)&&!roomCell(r,fixed,along);
                    if(edge==2) boundary=roomCell(r,along,fixed-1)&&!roomCell(r,along,fixed);
                    if(edge==3) boundary=roomCell(r,fixed,along)&&!roomCell(r,fixed-1,along);
                }
                if(boundary && start<0) start=along;
                if(!boundary && start>=0) {
                    walls.push_back(horizontal?PlanWall{start,fixed,along,fixed,PlanEdge(edge)}:PlanWall{fixed,start,fixed,along,PlanEdge(edge)});
                    start=-1;
                }
            }
        }
    }
    return walls;
}
PlanPoint openingPosition(const PlanRoom &r,const PlanOpening &o) {
    const auto &b=r.rect;
    if(o.edge==PlanEdge::Top || o.edge==PlanEdge::Bottom) return {b.x+b.w*o.offset/1000.0,double(o.edge==PlanEdge::Top?b.y+o.inset:b.y+b.h-o.inset)};
    return {double(o.edge==PlanEdge::Left?b.x+o.inset:b.x+b.w-o.inset),b.y+b.h*o.offset/1000.0};
}
PlanSegment openingSegment(const PlanRoom &r,const PlanOpening &o) {
    const auto center=openingPosition(r,o); const bool horizontal=o.edge==PlanEdge::Top || o.edge==PlanEdge::Bottom;
    const double half=o.span>0?o.span/2000.0:o.window?1.2:0.8;
    for(const auto &w:roomWalls(r)) if(w.edge==o.edge &&
        std::abs((horizontal?w.y1:w.x1)-(horizontal?center.y:center.x))<0.001 &&
        (horizontal?center.x:center.y)>=(horizontal?w.x1:w.y1) && (horizontal?center.x:center.y)<=(horizontal?w.x2:w.y2)) {
        const double from=std::max(double(horizontal?w.x1:w.y1),(horizontal?center.x:center.y)-half);
        const double to=std::min(double(horizontal?w.x2:w.y2),(horizontal?center.x:center.y)+half);
        return horizontal?PlanSegment{{from,center.y},{to,center.y}}:PlanSegment{{center.x,from},{center.x,to}};
    }
    return {center,center};
}
bool openingFits(const PlanRoom &r,const PlanOpening &o) {
    if(o.inset<0 || o.inset>std::max(FloorPlan::WIDTH,FloorPlan::HEIGHT)) return false;
    const auto p=openingPosition(r,o);
    for(const auto &w:roomWalls(r)) if(w.edge==o.edge) {
        if(w.x1==w.x2 && std::abs(w.x1-p.x)<0.001 && p.y>w.y1 && p.y<w.y2) return true;
        if(w.y1==w.y2 && std::abs(w.y1-p.y)<0.001 && p.x>w.x1 && p.x<w.x2) return true;
    }
    return false;
}
