#include "sketch/Shapes.h"

#include <cmath>

namespace bz::sketch {
namespace {

Constraint Make(ConstraintType type, ObjectId a, ObjectId b = {}) {
    Constraint c;
    c.type = type;
    c.refs = {a, b};
    return c;
}

/// Closes four corners into sides.
RectangleIds Close(Sketch& sk, IdGenerator& ids, const std::array<ObjectId, 4>& corners) {
    RectangleIds r{corners, {}};
    for (std::size_t i = 0; i < 4; ++i) r.sides[i] = sk.AddLine(ids, corners[i], corners[(i + 1) % 4]);
    return r;
}

void MakeAxisAligned(Sketch& sk, IdGenerator& ids, const RectangleIds& r) {
    sk.AddConstraint(ids, Make(ConstraintType::Horizontal, r.sides[0]));
    sk.AddConstraint(ids, Make(ConstraintType::Vertical, r.sides[1]));
    sk.AddConstraint(ids, Make(ConstraintType::Horizontal, r.sides[2]));
    sk.AddConstraint(ids, Make(ConstraintType::Vertical, r.sides[3]));
}

} // namespace

RectangleIds AddCornerRectangle(Sketch& sk, IdGenerator& ids, ObjectId corner, ObjectId opposite) {
    const Vec2 p = sk.PositionOf(corner), q = sk.PositionOf(opposite);
    const ObjectId c1 = sk.AddPoint(ids, {q.x, p.y});
    const ObjectId c3 = sk.AddPoint(ids, {p.x, q.y});
    const RectangleIds r = Close(sk, ids, {corner, c1, opposite, c3});
    MakeAxisAligned(sk, ids, r);
    return r;
}

RectangleIds AddCentredRectangle(Sketch& sk, IdGenerator& ids, ObjectId centre, Vec2 corner) {
    const Vec2 c = sk.PositionOf(centre);
    const Vec2 opposite = c * 2.0 - corner;
    const ObjectId c0 = sk.AddPoint(ids, corner);
    const ObjectId c2 = sk.AddPoint(ids, opposite);
    const ObjectId c1 = sk.AddPoint(ids, {opposite.x, corner.y});
    const ObjectId c3 = sk.AddPoint(ids, {corner.x, opposite.y});
    const RectangleIds r = Close(sk, ids, {c0, c1, c2, c3});
    MakeAxisAligned(sk, ids, r);
    const ObjectId diagonal = sk.AddLine(ids, c0, c2, true);
    sk.AddConstraint(ids, Make(ConstraintType::Midpoint, centre, diagonal));
    return r;
}

std::optional<RectangleIds> AddThreePointRectangle(Sketch& sk, IdGenerator& ids, ObjectId a, ObjectId b, Vec2 third) {
    const Vec2 pa = sk.PositionOf(a), pb = sk.PositionOf(b);
    const Vec2 side = pb - pa;
    if (side.Length() < 1e-9) return std::nullopt;
    const Vec2 normal = Vec2{-side.y, side.x}.Normalized();
    const double width = (third - pb).Dot(normal);
    if (std::abs(width) < 1e-9) return std::nullopt;
    const ObjectId c2 = sk.AddPoint(ids, pb + normal * width);
    const ObjectId c3 = sk.AddPoint(ids, pa + normal * width);
    const RectangleIds r = Close(sk, ids, {a, b, c2, c3});
    sk.AddConstraint(ids, Make(ConstraintType::Perpendicular, r.sides[0], r.sides[1]));
    sk.AddConstraint(ids, Make(ConstraintType::Parallel, r.sides[0], r.sides[2]));
    sk.AddConstraint(ids, Make(ConstraintType::Parallel, r.sides[1], r.sides[3]));
    return r;
}

std::optional<CircleThrough> CircleThroughPoints(Vec2 a, Vec2 b, Vec2 c) {
    // Perpendicular bisectors meet in the centre.
    const double d = 2 * ((b - a).Cross(c - a));
    const double scale = std::max({(b - a).Length(), (c - a).Length(), (c - b).Length()});
    if (scale < 1e-12 || std::abs(d) < 1e-9 * scale * scale) return std::nullopt;
    const Vec2 ab = b - a, ac = c - a;
    const double ab2 = ab.Dot(ab), ac2 = ac.Dot(ac);
    const Vec2 offset{(ac.y * ab2 - ab.y * ac2) / d, (ab.x * ac2 - ac.x * ab2) / d};
    return CircleThrough{a + offset, offset.Length()};
}

PolygonIds AddPolygon(Sketch& sk, IdGenerator& ids, ObjectId centre, Vec2 towards, int sideCount, bool inscribed) {
    PolygonIds out;
    const std::size_t n = static_cast<std::size_t>(std::max(sideCount, 3));
    const Vec2 c = sk.PositionOf(centre);
    const Vec2 to = towards - c;
    const double distance = std::max(to.Length(), 1e-6);
    const double step = 2 * kPi / static_cast<double>(n);
    // Inscribed: `towards` is a vertex. Circumscribed: it is a side's middle,
    // so the vertices sit half a step off and farther out.
    const double radius = inscribed ? distance : distance / std::cos(step / 2);
    const double start = std::atan2(to.y, to.x) + (inscribed ? 0.0 : step / 2);

    out.circle = sk.AddCircle(ids, centre, distance, true);
    for (std::size_t i = 0; i < n; ++i) {
        const double angle = start + static_cast<double>(i) * step;
        out.corners.push_back(sk.AddPoint(ids, c + Vec2{std::cos(angle), std::sin(angle)} * radius));
    }
    for (std::size_t i = 0; i < n; ++i) out.sides.push_back(sk.AddLine(ids, out.corners[i], out.corners[(i + 1) % n]));
    for (std::size_t i = 0; i < n; ++i) {
        if (inscribed) sk.AddConstraint(ids, Make(ConstraintType::PointOnCurve, out.corners[i], out.circle));
        else sk.AddConstraint(ids, Make(ConstraintType::Tangent, out.sides[i], out.circle));
        if (i > 0) sk.AddConstraint(ids, Make(ConstraintType::Equal, out.sides[0], out.sides[i]));
    }
    return out;
}

} // namespace bz::sketch
