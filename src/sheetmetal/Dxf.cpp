#include "sheetmetal/Dxf.h"

#include <cmath>

namespace bz::sheet {
namespace {

void Add(dxf::Drawing& d, const Segment& s, const char* layer) {
    if (!s.arc) {
        d.entities.push_back(dxf::Entity::Line(s.a, s.b, layer));
        return;
    }
    if (s.full) {
        d.entities.push_back(dxf::Entity::Circle(s.center, s.Radius(), layer));
        return;
    }
    // DXF arcs run counter-clockwise from the start angle to the end angle.
    const Segment ccw = s.ccw ? s : s.Reversed();
    const double a0 = std::atan2(ccw.a.y - ccw.center.y, ccw.a.x - ccw.center.x) * 180.0 / kPi;
    const double a1 = std::atan2(ccw.b.y - ccw.center.y, ccw.b.x - ccw.center.x) * 180.0 / kPi;
    d.entities.push_back(dxf::Entity::Arc(ccw.center, ccw.Radius(), a0, a1, layer));
}

} // namespace

dxf::Drawing FlatPatternDrawing(const FlatPattern& flat) {
    dxf::Drawing d;
    d.EnsureLayer(dxf::kCut, 7);
    d.EnsureLayer(dxf::kBend, 1);
    d.EnsureLayer(dxf::kMark, 3);
    for (const Segment& s : flat.outline.outer) Add(d, s, dxf::kCut);
    for (const Loop& hole : flat.outline.holes)
        for (const Segment& s : hole) Add(d, s, dxf::kCut);
    const double textHeight = 3.5;   // a standard lettering size (ГОСТ 2.304)
    for (const FlatPattern::Bend& b : flat.bends) {
        d.entities.push_back(dxf::Entity::Line(b.a, b.b, dxf::kBend));
        const Vec2 dir = (b.b - b.a).Normalized();
        double angle = std::atan2(dir.y, dir.x) * 180.0 / kPi;
        Vec2 from = b.a, along = dir;
        if (angle > 90.0 || angle <= -90.0) {   // keep the note readable
            angle += angle > 0 ? -180.0 : 180.0;
            from = b.b;
            along = dir * -1.0;
        }
        const Vec2 at = from + along * std::min(5.0, (b.b - b.a).Length() * 0.1) + along.Perp() * (textHeight * 0.4);
        d.entities.push_back(dxf::Entity::Text(at, textHeight, angle, BendNote(b), dxf::kBend));
    }
    return d;
}

std::string FlatPatternDxf(const FlatPattern& flat) { return dxf::WriteR12(FlatPatternDrawing(flat)); }

} // namespace bz::sheet
