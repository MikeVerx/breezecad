#include "app/sketch/DimensionArrange.h"

#include "app/sketch/DimensionLayout.h"
#include "sketch/Trim.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace bz::app {
namespace {

using sketch::ConstraintType;

struct Box {
    Vec2 min, max;
};
struct Segment {
    Vec2 a, b;
};

/// What a label must keep clear of.
struct Obstacles {
    std::vector<Box>     labels;     ///< other dimensions' texts
    std::vector<Segment> dimLines;   ///< other dimensions' lines
    std::vector<Segment> geometry;   ///< the sketch's curves, as short segments
};

std::size_t Glyphs(const std::string& s) {
    return static_cast<std::size_t>(std::count_if(s.begin(), s.end(), [](char ch) { return (static_cast<unsigned char>(ch) & 0xC0) != 0x80; }));
}

/// The label's box as SketchOverlay draws it (mono 12 px, the name 9.5 px under
/// it, 5 px padding), plus a few pixels of air; in sketch units.
Box LabelBox(const DimensionLayout& l, const ArrangeScale& s) {
    const double px = s.mmPerPixel * s.uiScale;
    const double w = (std::max(Glyphs(l.text) * 7.3, Glyphs(l.name) * 5.8) + 10 + 8) * px;
    const double h = (18.5 + (l.name.empty() ? 0.0 : 11.5) + 8) * px;
    return {{l.label.x - w / 2, l.label.y - h / 2}, {l.label.x + w / 2, l.label.y + h / 2}};
}

bool Overlap(const Box& a, const Box& b) {
    return a.min.x < b.max.x && b.min.x < a.max.x && a.min.y < b.max.y && b.min.y < a.max.y;
}

/// Liang–Barsky: does the segment cross the box (or lie in it)?
bool Crosses(const Segment& s, const Box& b) {
    double t0 = 0, t1 = 1;
    const Vec2 d = s.b - s.a;
    const double p[4] = {-d.x, d.x, -d.y, d.y};
    const double q[4] = {s.a.x - b.min.x, b.max.x - s.a.x, s.a.y - b.min.y, b.max.y - s.a.y};
    for (int i = 0; i < 4; ++i) {
        if (std::abs(p[i]) < 1e-15) {
            if (q[i] < 0) return false;
            continue;
        }
        const double t = q[i] / p[i];
        if (p[i] < 0) t0 = std::max(t0, t);
        else t1 = std::min(t1, t);
        if (t0 > t1) return false;
    }
    return true;
}

std::vector<Segment> GeometryOf(const sketch::Sketch& sk) {
    std::vector<Segment> out;
    for (const auto& l : sk.lines) out.push_back({sk.PositionOf(l.start), sk.PositionOf(l.end)});
    std::vector<ObjectId> curves;
    for (const auto& c : sk.circles) curves.push_back(c.id);
    for (const auto& a : sk.arcs) curves.push_back(a.id);
    for (const auto& s : sk.splines) curves.push_back(s.id);
    for (const auto& e : sk.ellipses) curves.push_back(e.id);
    for (const ObjectId id : curves) {
        const std::vector<Vec2> pts = sketch::SpanPolyline(sk, sketch::TrimSpan{id, std::nullopt, std::nullopt}, 32);
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) out.push_back({pts[i], pts[i + 1]});
    }
    return out;
}

void AddObstacle(Obstacles& o, const DimensionLayout& l, const ArrangeScale& s) {
    o.labels.push_back(LabelBox(l, s));
    for (const auto& seg : l.lines) o.dimLines.push_back({seg.a, seg.b});
    for (std::size_t i = 0; i + 1 < l.arc.size(); ++i) o.dimLines.push_back({l.arc[i], l.arc[i + 1]});
}

Vec2 Centre(const sketch::Sketch& sk) {
    Vec2 sum{0, 0};
    for (const auto& p : sk.points) sum = sum + p.pos;
    return sk.points.empty() ? sum : sum / static_cast<double>(sk.points.size());
}

Vec2 Rotated(Vec2 v, double degrees) {
    const double a = degrees * 3.14159265358979323846 / 180.0, c = std::cos(a), s = std::sin(a);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

/// Tries the label out along its wished direction, a little turned, then on the
/// other side; keeps the least crowded spot (the nearest of equally clear ones).
void Place(const sketch::Sketch& sk, sketch::Constraint& c, const UnitSystem& units, const ArrangeScale& s, const Obstacles& o) {
    const std::optional<Vec2> anchor = DimensionAnchor(sk, c);
    if (!anchor) return;
    Vec2 wish = c.labelOffset;
    if (wish.Length() < 1e-9) wish = *anchor - Centre(sk);
    if (wish.Length() < 1e-9) wish = {0, 1};
    const Vec2 dir0 = wish.Normalized();

    const bool radial = c.type == ConstraintType::Diameter || c.type == ConstraintType::Radius;
    const bool angle = sketch::IsAngular(c.type);
    const double step = 24.0 * s.mmPerPixel * s.uiScale;
    const double base = radial ? sk.RadiusOf(c.refs[0]) : 0.0;
    // An angle's label also picks which of the angles is meant: never flipped, barely turned.
    static constexpr double kTurns[] = {0, 25, -25, 50, -50, 180, 155, 205};
    static constexpr double kAngleTurns[] = {0, 10, -10};
    const double* turns = angle ? kAngleTurns : kTurns;
    const int turnCount = angle ? 3 : 8;

    double bestCost = std::numeric_limits<double>::max();
    Vec2 best = c.labelOffset;
    sketch::Constraint trial = c;
    for (int k = 1; k <= 8; ++k) {
        for (int t = 0; t < turnCount; ++t) {
            const double turnCost = t == 0 ? 0.0 : std::abs(turns[t]) >= 150 ? 30.0 : std::abs(turns[t]) / 5.0;
            const double reach = 4.0 * k + turnCost;
            if (reach >= bestCost) continue;   // cannot win even with no clash
            trial.labelOffset = Rotated(dir0, turns[t]) * (base + step * k);
            const std::optional<DimensionLayout> l = LayoutDimension(sk, trial, units);
            if (!l) continue;
            const Box box = LabelBox(*l, s);
            double cost = reach;
            for (const Box& other : o.labels)
                if (Overlap(box, other)) cost += 1000;
            for (const Segment& g : o.geometry)
                if (Crosses(g, box)) cost += 300;
            for (const Segment& d : o.dimLines)
                if (Crosses(d, box)) cost += 120;
            for (const auto& seg : l->lines)
                for (const Box& other : o.labels)
                    if (Crosses({seg.a, seg.b}, other)) cost += 120;
            if (cost < bestCost) {
                bestCost = cost;
                best = trial.labelOffset;
            }
        }
    }
    c.labelOffset = best;
}

} // namespace

void ArrangeDimension(const sketch::Sketch& sk, sketch::Constraint& c, const UnitSystem& units, const ArrangeScale& scale) {
    Obstacles o;
    o.geometry = GeometryOf(sk);
    for (const auto& other : sk.constraints) {
        if (other.id == c.id || !sketch::IsDimensional(other.type)) continue;
        if (const auto l = LayoutDimension(sk, other, units)) AddObstacle(o, *l, scale);
    }
    Place(sk, c, units, scale, o);
}

int ArrangeDimensions(sketch::Sketch& sk, const UnitSystem& units, const ArrangeScale& scale) {
    const Vec2 centre = Centre(sk);
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < sk.constraints.size(); ++i)
        if (sketch::IsDimensional(sk.constraints[i].type)) order.push_back(i);
    const auto distance = [&](std::size_t i) {
        const auto a = DimensionAnchor(sk, sk.constraints[i]);
        return a ? (*a - centre).Length() : 0.0;
    };
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return distance(a) < distance(b); });

    Obstacles o;
    o.geometry = GeometryOf(sk);
    int moved = 0;
    for (const std::size_t i : order) {
        sketch::Constraint& c = sk.constraints[i];
        const Vec2 before = c.labelOffset;
        Place(sk, c, units, scale, o);
        if ((c.labelOffset - before).Length() > 1e-9) ++moved;
        if (const auto l = LayoutDimension(sk, c, units)) AddObstacle(o, *l, scale);
    }
    return moved;
}

} // namespace bz::app
