#include "sketch/Mirror.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace bz::sketch {

Vec2 Reflect(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 d = (b - a).Normalized();
    const Vec2 foot = a + d * (p - a).Dot(d);
    return foot * 2.0 - p;
}

std::vector<ObjectId> MirrorEntities(Sketch& sk, IdGenerator& ids, std::span<const ObjectId> entities, ObjectId axis) {
    const Line* axisLine = sk.FindLine(axis);
    if (!axisLine || std::find(entities.begin(), entities.end(), axis) != entities.end()) return {};
    const Vec2 a = sk.PositionOf(axisLine->start), b = sk.PositionOf(axisLine->end);
    if ((b - a).Length() < 1e-9) return {};
    const double tolerance = std::max((b - a).Length(), 1.0) * 1e-9;

    std::unordered_map<ObjectId, ObjectId> image;
    const auto mirrored = [&](ObjectId point) {
        if (const auto it = image.find(point); it != image.end()) return it->second;
        const Vec2 p = sk.PositionOf(point);
        const Vec2 d = (b - a).Normalized();
        ObjectId out = point;   // on the axis: its own image
        if (std::abs(d.Cross(p - a)) > tolerance) {
            out = sk.AddPoint(ids, Reflect(p, a, b));
            Constraint c;
            c.type = ConstraintType::Symmetric;
            c.refs = {point, out, axis};
            sk.AddConstraint(ids, c);
        }
        image[point] = out;
        return out;
    };

    // Copies first (adding curves would move the vectors under us).
    std::vector<Line> lines;
    std::vector<Circle> circles;
    std::vector<Arc> arcs;
    for (ObjectId id : entities) {
        if (const Line* l = sk.FindLine(id)) lines.push_back(*l);
        else if (const Circle* c = sk.FindCircle(id)) circles.push_back(*c);
        else if (const Arc* ar = sk.FindArc(id)) arcs.push_back(*ar);
    }
    std::vector<ObjectId> created;
    for (const Line& l : lines) {
        const ObjectId copy = sk.AddLine(ids, mirrored(l.start), mirrored(l.end), l.construction);
        sk.FindLine(copy)->centerline = l.centerline;
        created.push_back(copy);
    }
    for (const Circle& c : circles) {
        const ObjectId copy = sk.AddCircle(ids, mirrored(c.center), c.radius, c.construction);
        Constraint eq;
        eq.type = ConstraintType::Equal;
        eq.refs = {c.id, copy, {}};
        sk.AddConstraint(ids, eq);
        created.push_back(copy);
    }
    for (const Arc& ar : arcs)   // a reflection turns the arc's direction round
        created.push_back(sk.AddArc(ids, mirrored(ar.center), mirrored(ar.end), mirrored(ar.start), ar.construction));
    return created;
}

} // namespace bz::sketch
