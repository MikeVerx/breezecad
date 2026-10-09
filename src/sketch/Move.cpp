#include "sketch/Move.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
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
        else if (const Spline* s = sk.FindSpline(id)) for (const ObjectId p : s->points) add(p);
        else if (const Text* t = sk.FindText(id)) for (const ObjectId p : t->corners) add(p);
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

std::vector<ObjectId> CopyEntities(Sketch& sk, IdGenerator& ids, std::span<const ObjectId> entities, const Motion& motion) {
    std::unordered_map<ObjectId, ObjectId> pointCopy, curveCopy;
    const auto copyPoint = [&](ObjectId p) {
        if (const auto it = pointCopy.find(p); it != pointCopy.end()) return it->second;
        const ObjectId c = sk.AddPoint(ids, motion.Apply(sk.PositionOf(p)));
        pointCopy[p] = c;
        return c;
    };
    std::vector<Line> lines;
    std::vector<Circle> circles;
    std::vector<Arc> arcs;
    std::vector<Spline> splines;
    std::vector<Text> texts;
    for (ObjectId id : entities) {
        if (const Spline* s = sk.FindSpline(id)) splines.push_back(*s);
        if (sk.FindText(id)) texts.push_back(*sk.FindText(id));
        if (const Line* l = sk.FindLine(id)) lines.push_back(*l);
        else if (const Circle* c = sk.FindCircle(id)) circles.push_back(*c);
        else if (const Arc* a = sk.FindArc(id)) arcs.push_back(*a);
    }
    std::vector<ObjectId> created;
    for (const Line& l : lines) {
        const ObjectId c = sk.AddLine(ids, copyPoint(l.start), copyPoint(l.end), l.construction);
        sk.FindLine(c)->centerline = l.centerline;
        curveCopy[l.id] = c;
        created.push_back(c);
    }
    for (const Circle& ci : circles) {
        const ObjectId c = sk.AddCircle(ids, copyPoint(ci.center), ci.radius, ci.construction);
        curveCopy[ci.id] = c;
        created.push_back(c);
    }
    for (const Arc& a : arcs) {
        const ObjectId c = sk.AddArc(ids, copyPoint(a.center), copyPoint(a.start), copyPoint(a.end), a.construction);
        curveCopy[a.id] = c;
        created.push_back(c);
    }
    for (const Text& t : texts) {
        // A copy of the box (corners moved), the same words and format.
        const Vec2 bl = motion.Apply(sk.PositionOf(t.corners[0])), tr = motion.Apply(sk.PositionOf(t.corners[2]));
        const ObjectId c = sk.AddText(ids, bl, tr, t.text, t.height);
        Text* copy = sk.FindText(c);
        for (std::size_t i = 0; i < 4; ++i)
            if (Point* p = sk.FindPoint(copy->corners[i])) p->pos = motion.Apply(sk.PositionOf(t.corners[i]));
        copy->font = t.font;
        copy->bold = t.bold;
        copy->italic = t.italic;
        copy->hAlign = t.hAlign;
        copy->vAlign = t.vAlign;
        copy->construction = t.construction;
        curveCopy[t.id] = c;
        created.push_back(c);
    }
    for (const Spline& s : splines) {
        std::vector<ObjectId> pts;
        for (const ObjectId p : s.points) pts.push_back(copyPoint(p));   // a closed one repeats its first point: one copy
        const ObjectId c = sk.AddSpline(ids, std::move(pts), s.interpolate, s.construction);
        curveCopy[s.id] = c;
        created.push_back(c);
    }
    // Constraints wholly inside the copied set come along; a size becomes
    // an equality with the original, so one dimension drives both.
    const auto mapped = [&](ObjectId id) -> ObjectId {
        if (const auto it = curveCopy.find(id); it != curveCopy.end()) return it->second;
        if (const auto it = pointCopy.find(id); it != pointCopy.end()) return it->second;
        return {};
    };
    const std::vector<Constraint> existing = sk.constraints;
    for (const Constraint& c : existing) {
        if (c.type == ConstraintType::Fix || c.type == ConstraintType::Symmetric) continue;
        if (IsDimensional(c.type)) {
            if ((c.type == ConstraintType::Length || c.type == ConstraintType::Radius || c.type == ConstraintType::Diameter) &&
                curveCopy.contains(c.refs[0])) {
                Constraint eq;
                eq.type = ConstraintType::Equal;
                eq.refs = {c.refs[0], curveCopy[c.refs[0]], {}};
                sk.AddConstraint(ids, eq);
            }
            continue;
        }
        Constraint copy = c;
        bool inside = true;
        for (ObjectId& r : copy.refs)
            if (r) {
                const ObjectId m = mapped(r);
                if (!m) inside = false;
                r = m;
            }
        if (inside) sk.AddConstraint(ids, copy);
    }
    // A rotated copy cannot keep horizontal / vertical lines.
    const double quarter = motion.angle / (kPi / 2);
    if (std::abs(quarter - std::round(quarter)) > 1e-9) {
        std::erase_if(sk.constraints, [&](const Constraint& c) {
            const bool hv = c.type == ConstraintType::Horizontal || c.type == ConstraintType::Vertical;
            if (!hv) return false;
            for (ObjectId id : created)
                if (c.refs[0] == id) return true;
            return false;
        });
    }
    return created;
}

bool ScaleEntities(Sketch& sk, std::span<const ObjectId> entities, Vec2 centre, double factor) {
    if (factor <= 0) return false;
    const std::vector<ObjectId> moved = MovedPoints(sk, entities);
    if (moved.empty()) return false;
    const std::unordered_set<ObjectId> set(moved.begin(), moved.end());
    for (Point& p : sk.points)
        if (set.contains(p.id)) p.pos = centre + (p.pos - centre) * factor;
    for (Constraint& c : sk.constraints)
        if (c.type == ConstraintType::Fix && set.contains(c.refs[0])) c.fixedAt = centre + (c.fixedAt - centre) * factor;
    for (ObjectId id : entities)
        if (Circle* c = sk.FindCircle(id)) c->radius *= factor;
    return true;
}

} // namespace bz::sketch
