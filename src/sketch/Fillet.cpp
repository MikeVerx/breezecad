#include "sketch/Fillet.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace bz::sketch {

std::optional<std::pair<ObjectId, ObjectId>> CornerLines(const Sketch& sk, ObjectId corner) {
    // Lines of the outline meet at the corner; helper lines ending there (a
    // centred rectangle's diagonals, a fillet's phantom sides) do not count
    // unless the corner is made of helpers only.
    std::vector<const Line*> normal, helpers;
    for (const Line& l : sk.lines) {
        if (l.start != corner && l.end != corner) continue;
        (l.construction ? helpers : normal).push_back(&l);
    }
    if (normal.empty()) {
        // A fillet's virtual sharp is not a corner to round again: its
        // phantom sides lead to the ends of an arc.
        for (const Line* h : helpers) {
            const ObjectId other = h->start == corner ? h->end : h->start;
            for (const Arc& a : sk.arcs)
                if (a.start == other || a.end == other) return std::nullopt;
        }
    }
    const std::vector<const Line*>& pair = normal.empty() ? helpers : normal;
    if (pair.size() != 2) return std::nullopt;   // one line, or three: which two is ambiguous
    if (pair[0]->projected || pair[1]->projected) return std::nullopt;
    // Arcs of the same kind ending there make it a tangent joint, not a corner.
    const bool construction = pair[0]->construction;
    for (const Arc& a : sk.arcs)
        if ((a.start == corner || a.end == corner) && a.construction == construction) return std::nullopt;
    return std::make_pair(pair[0]->id, pair[1]->id);
}

std::optional<FilletPlan> PlanFillet(const Sketch& sk, ObjectId corner, double radius) {
    if (!(radius > 0)) return std::nullopt;
    const auto lines = CornerLines(sk, corner);
    if (!lines) return std::nullopt;
    const Vec2 c = sk.PositionOf(corner);
    // Unit direction from the corner along a line, and the line's length.
    const auto away = [&](ObjectId id, double& length) {
        const Line* l = sk.FindLine(id);
        const Vec2 other = sk.PositionOf(l->start == corner ? l->end : l->start);
        length = (other - c).Length();
        return length > 1e-12 ? (other - c) * (1.0 / length) : Vec2{};
    };
    double lengthA = 0, lengthB = 0;
    const Vec2 u = away(lines->first, lengthA), v = away(lines->second, lengthB);
    const double cosine = std::clamp(u.Dot(v), -1.0, 1.0);
    const double half = std::acos(cosine) / 2;   // half the corner angle
    if (half < 1e-6 || half > kPi / 2 - 1e-6) return std::nullopt;   // the lines are in one line
    const double along = radius / std::tan(half);
    if (along >= lengthA - 1e-9 || along >= lengthB - 1e-9) return std::nullopt;   // the arc would not fit
    const Vec2 bisector = (u + v).Normalized();
    return FilletPlan{lines->first, lines->second, c + bisector * (radius / std::sin(half)), c + u * along, c + v * along};
}

namespace {

Constraint Make(ConstraintType type, ObjectId a, ObjectId b = {}) {
    Constraint c;
    c.type = type;
    c.refs = {a, b, {}};
    return c;
}

} // namespace

std::optional<ObjectId> FilletCorner(Sketch& sk, IdGenerator& ids, ObjectId corner, double radius) {
    const std::optional<FilletPlan> plan = PlanFillet(sk, corner, radius);
    if (!plan) return std::nullopt;
    const ObjectId onA = sk.AddPoint(ids, plan->onA);
    const ObjectId onB = sk.AddPoint(ids, plan->onB);
    const ObjectId centre = sk.AddPoint(ids, plan->centre);

    // The corner stays as a "virtual sharp" (as KOMPAS and drawings show it):
    // a point to dimension and snap to, reached by two dashed phantom sides
    // from the tangent points. Whatever was tied to the corner keeps holding
    // it, so nothing else in the sketch moves.
    struct Side {
        ObjectId line, tangentPoint, farEnd, phantom, full;
    };
    std::array<Side, 2> sides{Side{plan->a, onA, {}, {}, {}}, Side{plan->b, onB, {}, {}, {}}};
    bool construction = false;
    for (Side& s : sides) {
        Line* l = sk.FindLine(s.line);
        s.farEnd = l->start == corner ? l->end : l->start;
        construction = construction || l->construction;
    }
    for (Side& s : sides) {
        // A constraint that measured the whole side (its length, its middle,
        // equal to another) now needs the side as it was: a helper line from
        // the far end to the corner carries it. Direction-only ones (parallel,
        // horizontal, angle, point on the line…) stay on the shortened side.
        bool needsFull = false;
        for (const Constraint& c : sk.constraints)
            if (c.References(s.line) &&
                (c.type == ConstraintType::Equal || c.type == ConstraintType::Midpoint))
                needsFull = true;
        for (Constraint& c : sk.constraints) {
            if (!c.References(s.line)) continue;
            if (c.type == ConstraintType::Length) {
                // The length of the side up to the corner, as before.
                c.type = ConstraintType::Distance;
                c.refs = {s.farEnd, corner, {}};
            }
        }
        if (needsFull) {
            s.full = sk.AddLine(ids, s.farEnd, corner, true);
            for (Constraint& c : sk.constraints)
                if (c.type == ConstraintType::Equal || c.type == ConstraintType::Midpoint)
                    for (ObjectId& r : c.refs)
                        if (r == s.line) r = s.full;
        }
        // Shorten the side to the tangent point.
        Line* l = sk.FindLine(s.line);
        (l->start == corner ? l->start : l->end) = s.tangentPoint;
        s.phantom = sk.AddLine(ids, s.tangentPoint, corner, true);
        // The phantom continues the side: they share the tangent point and run parallel.
        sk.AddConstraint(ids, Make(ConstraintType::Parallel, s.line, s.phantom));
    }

    // Arcs run counter-clockwise from start to end.
    const bool ccw = (plan->onA - plan->centre).Cross(plan->onB - plan->centre) > 0;
    const ObjectId arc = sk.AddArc(ids, centre, ccw ? onA : onB, ccw ? onB : onA, construction);
    for (const Side& s : sides) sk.AddConstraint(ids, Make(ConstraintType::Tangent, s.line, arc));
    return arc;
}

} // namespace bz::sketch
