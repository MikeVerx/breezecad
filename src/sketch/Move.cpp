#include "sketch/Move.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace bz::sketch {

Vec2 Motion::Apply(Vec2 p) const {
    const Vec2 r = p - centre;
    const double c = std::cos(angle), s = std::sin(angle);
    return centre + Vec2{r.x * c - r.y * s, r.x * s + r.y * c} + offset;
}

std::vector<ObjectId> MovedPoints(const Sketch& sk, std::span<const ObjectId> entities) {
    std::vector<ObjectId> out;
    const auto add = [&](ObjectId id) {
        const Point* p = sk.FindPoint(id);
        if (!p || p->isOrigin || p->projected) return;
        if (std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
    };
    for (ObjectId id : entities) {
        if (sk.FindPoint(id)) add(id);
        else if (const Line* l = sk.FindLine(id)) { add(l->start); add(l->end); }
        else if (const Circle* c = sk.FindCircle(id)) add(c->center);
        else if (const Arc* a = sk.FindArc(id)) { add(a->center); add(a->start); add(a->end); }
    }
    return out;
}

bool MoveEntities(Sketch& sk, IdGenerator& ids, std::span<const ObjectId> entities, const Motion& motion) {
    const std::vector<ObjectId> moved = MovedPoints(sk, entities);
    if (moved.empty()) return false;
    const std::unordered_set<ObjectId> set(moved.begin(), moved.end());
    for (Point& p : sk.points)
        if (set.contains(p.id)) p.pos = motion.Apply(p.pos);
    for (Constraint& c : sk.constraints)
        if (c.type == ConstraintType::Fix && set.contains(c.refs[0])) c.fixedAt = motion.Apply(c.fixedAt);

    // Rotation: horizontal / vertical lines that turned with the rest keep
    // their angles to each other instead.
    const double quarter = motion.angle / (kPi / 2);
    if (std::abs(quarter - std::round(quarter)) > 1e-9) {
        const auto movedLine = [&](ObjectId id) {
            const Line* l = sk.FindLine(id);
            return l && set.contains(l->start) && set.contains(l->end);
        };
        ObjectId reference;
        bool referenceHorizontal = true;
        std::vector<Constraint> replacements;
        std::vector<ObjectId> dropped;
        for (const Constraint& c : sk.constraints) {
            const bool hv = c.type == ConstraintType::Horizontal || c.type == ConstraintType::Vertical;
            if (!hv || c.refs[1] || !movedLine(c.refs[0])) continue;
            dropped.push_back(c.id);
            const bool horizontal = c.type == ConstraintType::Horizontal;
            if (!reference) {
                reference = c.refs[0];
                referenceHorizontal = horizontal;
                continue;
            }
            Constraint r;
            r.type = horizontal == referenceHorizontal ? ConstraintType::Parallel : ConstraintType::Perpendicular;
            r.refs = {reference, c.refs[0]};
            replacements.push_back(r);
        }
        std::erase_if(sk.constraints, [&](const Constraint& c) {
            return std::find(dropped.begin(), dropped.end(), c.id) != dropped.end();
        });
        for (Constraint& r : replacements) sk.AddConstraint(ids, std::move(r));
    }
    return true;
}

} // namespace bz::sketch
