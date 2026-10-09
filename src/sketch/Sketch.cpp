#include "sketch/Sketch.h"

#include <algorithm>

namespace bz::sketch {
namespace {

template <class T>
T* FindIn(std::vector<T>& items, ObjectId id) {
    for (auto& item : items)
        if (item.id == id) return &item;
    return nullptr;
}

template <class T>
const T* FindIn(const std::vector<T>& items, ObjectId id) {
    for (const auto& item : items)
        if (item.id == id) return &item;
    return nullptr;
}

template <class T, class Pred>
void EraseIf(std::vector<T>& items, Pred pred) {
    items.erase(std::remove_if(items.begin(), items.end(), pred), items.end());
}

} // namespace

bool IsDimensional(ConstraintType type) {
    switch (type) {
        case ConstraintType::Distance:
        case ConstraintType::DistanceX:
        case ConstraintType::DistanceY:
        case ConstraintType::Length:
        case ConstraintType::PointLineDistance:
        case ConstraintType::Radius:
        case ConstraintType::Diameter:
        case ConstraintType::Angle:
            return true;
        default:
            return false;
    }
}

ObjectId Sketch::AddPoint(IdGenerator& ids, Vec2 pos) {
    const ObjectId id = ids.Next();
    points.push_back({id, pos, false});
    return id;
}

ObjectId Sketch::AddLine(IdGenerator& ids, ObjectId start, ObjectId end, bool construction) {
    const ObjectId id = ids.Next();
    lines.push_back({id, start, end, construction});
    return id;
}

ObjectId Sketch::AddText(IdGenerator& ids, Vec2 bottomLeft, Vec2 topRight, std::string text, double height) {
    Text t;
    t.corners = {AddPoint(ids, bottomLeft), AddPoint(ids, {topRight.x, bottomLeft.y}), AddPoint(ids, topRight),
                 AddPoint(ids, {bottomLeft.x, topRight.y})};
    for (std::size_t i = 0; i < 4; ++i) t.sides[i] = AddLine(ids, t.corners[i], t.corners[(i + 1) % 4], true);
    const auto add = [&](ConstraintType type, ObjectId a, ObjectId b) {
        Constraint c;
        c.type = type;
        c.refs = {a, b};
        AddConstraint(ids, c);
    };
    // A rectangle that may turn: delete the horizontal to rotate the text.
    add(ConstraintType::Perpendicular, t.sides[0], t.sides[3]);
    add(ConstraintType::Parallel, t.sides[0], t.sides[2]);
    add(ConstraintType::Parallel, t.sides[1], t.sides[3]);
    add(ConstraintType::Horizontal, t.sides[0], {});
    t.id = ids.Next();
    t.text = std::move(text);
    t.height = height;
    texts.push_back(std::move(t));
    return texts.back().id;
}

ObjectId Sketch::AddSpline(IdGenerator& ids, std::vector<ObjectId> pts, bool interpolate, bool construction) {
    const ObjectId id = ids.Next();
    splines.push_back({id, std::move(pts), interpolate, construction});
    return id;
}

ObjectId Sketch::AddCircle(IdGenerator& ids, ObjectId center, double radius, bool construction) {
    const ObjectId id = ids.Next();
    circles.push_back({id, center, radius, construction});
    return id;
}

ObjectId Sketch::AddArc(IdGenerator& ids, ObjectId center, ObjectId start, ObjectId end, bool construction) {
    const ObjectId id = ids.Next();
    arcs.push_back({id, center, start, end, construction});
    return id;
}

ObjectId Sketch::AddConstraint(IdGenerator& ids, Constraint constraint) {
    // The same geometric relation twice (in either order of its first two
    // entities) adds nothing but an over-constrained sketch.
    if (!IsDimensional(constraint.type))
        for (const Constraint& c : constraints) {
            if (c.type != constraint.type || c.refs[2] != constraint.refs[2]) continue;
            const bool same = (c.refs[0] == constraint.refs[0] && c.refs[1] == constraint.refs[1]) ||
                              (c.refs[0] == constraint.refs[1] && c.refs[1] == constraint.refs[0]);
            if (same) return c.id;
        }
    constraint.id = ids.Next();
    constraints.push_back(std::move(constraint));
    return constraints.back().id;
}

ObjectId Sketch::EnsureOrigin(IdGenerator& ids) {
    if (const Point* origin = Origin()) return origin->id;
    const ObjectId id = ids.Next();
    points.push_back({id, {0.0, 0.0}, true});
    return id;
}

EntityKind Sketch::KindOf(ObjectId id) const {
    if (FindIn(points, id))      return EntityKind::Point;
    if (FindIn(lines, id))       return EntityKind::Line;
    if (FindIn(circles, id))     return EntityKind::Circle;
    if (FindIn(arcs, id))        return EntityKind::Arc;
    if (FindIn(splines, id))     return EntityKind::Spline;
    if (FindIn(texts, id))       return EntityKind::Text;
    if (FindIn(constraints, id)) return EntityKind::Constraint;
    return EntityKind::None;
}

Point*            Sketch::FindPoint(ObjectId id)            { return FindIn(points, id); }
const Point*      Sketch::FindPoint(ObjectId id) const      { return FindIn(points, id); }
Line*             Sketch::FindLine(ObjectId id)             { return FindIn(lines, id); }
const Line*       Sketch::FindLine(ObjectId id) const       { return FindIn(lines, id); }
Circle*           Sketch::FindCircle(ObjectId id)           { return FindIn(circles, id); }
const Circle*     Sketch::FindCircle(ObjectId id) const     { return FindIn(circles, id); }
const Arc*        Sketch::FindArc(ObjectId id) const        { return FindIn(arcs, id); }
const Spline*     Sketch::FindSpline(ObjectId id) const     { return FindIn(splines, id); }
Text*             Sketch::FindText(ObjectId id)             { return FindIn(texts, id); }
const Text*       Sketch::FindText(ObjectId id) const       { return FindIn(texts, id); }
Constraint*       Sketch::FindConstraint(ObjectId id)       { return FindIn(constraints, id); }
const Constraint* Sketch::FindConstraint(ObjectId id) const { return FindIn(constraints, id); }

const Point* Sketch::Origin() const {
    for (const auto& p : points)
        if (p.isOrigin) return &p;
    return nullptr;
}

Vec2 Sketch::PositionOf(ObjectId pointId) const {
    const Point* p = FindPoint(pointId);
    return p ? p->pos : Vec2{};
}

double Sketch::RadiusOf(ObjectId curveId) const {
    if (const Circle* c = FindCircle(curveId)) return c->radius;
    if (const Arc* a = FindArc(curveId)) return (PositionOf(a->start) - PositionOf(a->center)).Length();
    return 0.0;
}

bool Sketch::IsPointUsed(ObjectId pointId) const {
    for (const auto& l : lines)
        if (l.start == pointId || l.end == pointId) return true;
    for (const auto& c : circles)
        if (c.center == pointId) return true;
    for (const auto& a : arcs)
        if (a.center == pointId || a.start == pointId || a.end == pointId) return true;
    for (const auto& s : splines)
        if (std::find(s.points.begin(), s.points.end(), pointId) != s.points.end()) return true;
    for (const auto& t : texts)
        if (std::find(t.corners.begin(), t.corners.end(), pointId) != t.corners.end()) return true;
    return false;
}

void Sketch::RemoveConstraintsReferencing(ObjectId id) {
    EraseIf(constraints, [id](const Constraint& c) { return c.References(id); });
}

void Sketch::Remove(ObjectId id) {
    const EntityKind kind = KindOf(id);
    switch (kind) {
        case EntityKind::None:
            return;
        case EntityKind::Constraint:
            EraseIf(constraints, [id](const Constraint& c) { return c.id == id; });
            return;
        case EntityKind::Point: {
            const Point* p = FindPoint(id);
            if (p->isOrigin) return;
            // Curves that use the point go first, they take their constraints.
            std::vector<ObjectId> curves;
            for (const auto& l : lines)
                if (l.start == id || l.end == id) curves.push_back(l.id);
            for (const auto& c : circles)
                if (c.center == id) curves.push_back(c.id);
            for (const auto& a : arcs)
                if (a.center == id || a.start == id || a.end == id) curves.push_back(a.id);
            for (const auto& s : splines)
                if (std::find(s.points.begin(), s.points.end(), id) != s.points.end()) curves.push_back(s.id);
            for (const auto& t : texts)
                if (std::find(t.corners.begin(), t.corners.end(), id) != t.corners.end()) curves.push_back(t.id);
            for (ObjectId curve : curves) Remove(curve);
            RemoveConstraintsReferencing(id);
            EraseIf(points, [id](const Point& pt) { return pt.id == id; });
            return;
        }
        case EntityKind::Line:
        case EntityKind::Circle:
        case EntityKind::Arc:
        case EntityKind::Spline:
        case EntityKind::Text: {
            std::vector<ObjectId> ownPoints;
            if (const Line* l = FindLine(id)) ownPoints = {l->start, l->end};
            if (const Circle* c = FindCircle(id)) ownPoints = {c->center};
            if (const Arc* a = FindArc(id)) ownPoints = {a->center, a->start, a->end};
            if (const Spline* s = FindSpline(id)) ownPoints = s->points;
            std::vector<ObjectId> ownLines;
            if (const Text* t = FindText(id)) {
                ownPoints.assign(t->corners.begin(), t->corners.end());
                ownLines.assign(t->sides.begin(), t->sides.end());
            }

            RemoveConstraintsReferencing(id);
            EraseIf(lines, [id](const Line& l) { return l.id == id; });
            EraseIf(circles, [id](const Circle& c) { return c.id == id; });
            EraseIf(arcs, [id](const Arc& a) { return a.id == id; });
            EraseIf(splines, [id](const Spline& s) { return s.id == id; });
            EraseIf(texts, [id](const Text& t) { return t.id == id; });
            // A text takes its box along; a side of a box takes its text.
            for (ObjectId line : ownLines) Remove(line);
            if (kind == EntityKind::Line)
                for (const Text& t : std::vector<Text>(texts))
                    if (std::find(t.sides.begin(), t.sides.end(), id) != t.sides.end()) Remove(t.id);

            for (ObjectId pid : ownPoints) {
                const Point* p = FindPoint(pid);
                if (p && !p->isOrigin && !IsPointUsed(pid)) {
                    RemoveConstraintsReferencing(pid);
                    EraseIf(points, [pid](const Point& pt) { return pt.id == pid; });
                }
            }
            return;
        }
    }
}

void Sketch::MergePoints(ObjectId from, ObjectId into) {
    if (from == into) return;
    const auto remap = [&](ObjectId& ref) {
        if (ref == from) ref = into;
    };
    for (auto& l : lines) {
        remap(l.start);
        remap(l.end);
    }
    for (auto& c : circles) remap(c.center);
    for (auto& a : arcs) {
        remap(a.center);
        remap(a.start);
        remap(a.end);
    }
    for (auto& s : splines)
        for (ObjectId& p : s.points) remap(p);
    for (auto& t : texts)
        for (ObjectId& p : t.corners) remap(p);
    for (auto& c : constraints) {
        remap(c.refs[0]);
        remap(c.refs[1]);
        remap(c.refs[2]);
    }
    // A constraint between a point and itself is now meaningless.
    EraseIf(constraints, [](const Constraint& c) {
        return c.type == ConstraintType::Coincident && c.refs[0] == c.refs[1];
    });
    if (const Point* p = FindPoint(from); p && !p->isOrigin)
        EraseIf(points, [from](const Point& pt) { return pt.id == from; });
}

Box2 Sketch::Extents() const {
    Box2 box;
    for (const Point& p : points)
        if (!p.isOrigin) box.Include(p.pos);
    for (const Circle& c : circles) box.Include(PositionOf(c.center), c.radius);
    for (const Arc& a : arcs) box.Include(PositionOf(a.center), RadiusOf(a.id));
    return box;
}

void Sketch::ScaleAbout(Vec2 center, double factor) {
    for (Point& p : points)
        if (!p.isOrigin) p.pos = center + (p.pos - center) * factor;
    for (Circle& c : circles) c.radius *= factor;
    for (Constraint& c : constraints) {
        if (c.type == ConstraintType::Fix) c.fixedAt = center + (c.fixedAt - center) * factor;
        c.labelOffset = c.labelOffset * factor;
    }
}

bool IsDiametric(const Sketch& sk, const Constraint& c) {
    if (c.type != ConstraintType::PointLineDistance) return false;
    const Line* axis = sk.FindLine(c.refs[1]);
    return axis && axis->centerline;
}

} // namespace bz::sketch
