#include "sketch/Fillet.h"

#include <algorithm>
#include <cmath>

namespace bz::sketch {

std::optional<std::pair<ObjectId, ObjectId>> CornerLines(const Sketch& sk, ObjectId corner) {
    std::optional<ObjectId> first, second;
    for (const Line& l : sk.lines) {
        if (l.start != corner && l.end != corner) continue;
        if (l.projected) return std::nullopt;
        if (!first) first = l.id;
        else if (!second) second = l.id;
        else return std::nullopt;   // three lines: which two is ambiguous
    }
    // Arcs ending there make it a tangent joint, not a corner.
    for (const Arc& a : sk.arcs)
        if (a.start == corner || a.end == corner) return std::nullopt;
    if (!second) return std::nullopt;
    return std::make_pair(*first, *second);
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

std::optional<ObjectId> FilletCorner(Sketch& sk, IdGenerator& ids, ObjectId corner, double radius) {
    const std::optional<FilletPlan> plan = PlanFillet(sk, corner, radius);
    if (!plan) return std::nullopt;
    const ObjectId onA = sk.AddPoint(ids, plan->onA);
    const ObjectId onB = sk.AddPoint(ids, plan->onB);
    const ObjectId centre = sk.AddPoint(ids, plan->centre);
    bool construction = false;
    for (Line& l : sk.lines) {
        const ObjectId to = l.id == plan->a ? onA : l.id == plan->b ? onB : ObjectId{};
        if (!to) continue;
        (l.start == corner ? l.start : l.end) = to;
        construction = construction || l.construction;
    }
    // The corner has no curves left: it goes, with what was tied to it.
    sk.Remove(corner);
    // Arcs run counter-clockwise from start to end.
    const bool ccw = (plan->onA - plan->centre).Cross(plan->onB - plan->centre) > 0;
    const ObjectId arc = sk.AddArc(ids, centre, ccw ? onA : onB, ccw ? onB : onA, construction);
    for (const ObjectId line : {plan->a, plan->b}) {
        Constraint t;
        t.type = ConstraintType::Tangent;
        t.refs = {line, arc};
        sk.AddConstraint(ids, t);
    }
    return arc;
}

} // namespace bz::sketch
