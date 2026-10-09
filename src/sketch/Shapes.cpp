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

std::optional<RectangleIds> AddThreePointCentredRectangle(Sketch& sk, IdGenerator& ids, ObjectId centre, Vec2 side, Vec2 third) {
    const Vec2 c = sk.PositionOf(centre);
    const Vec2 u = side - c;
    if (u.Length() < 1e-9) return std::nullopt;
    const Vec2 n = Vec2{-u.y, u.x}.Normalized();
    const double half = (third - c).Dot(n);
    if (std::abs(half) < 1e-9) return std::nullopt;
    const Vec2 v = n * half;
    const std::array<ObjectId, 4> corners{sk.AddPoint(ids, c - u - v), sk.AddPoint(ids, c + u - v), sk.AddPoint(ids, c + u + v),
                                          sk.AddPoint(ids, c - u + v)};
    const RectangleIds r = Close(sk, ids, corners);
    sk.AddConstraint(ids, Make(ConstraintType::Perpendicular, r.sides[0], r.sides[1]));
    sk.AddConstraint(ids, Make(ConstraintType::Parallel, r.sides[0], r.sides[2]));
    sk.AddConstraint(ids, Make(ConstraintType::Parallel, r.sides[1], r.sides[3]));
    const ObjectId diagonal = sk.AddLine(ids, corners[0], corners[2], true);
    sk.AddConstraint(ids, Make(ConstraintType::Midpoint, centre, diagonal));
    return r;
}

SlotIds AddStraightSlot(Sketch& sk, IdGenerator& ids, ObjectId centre1, ObjectId centre2, double width) {
    const Vec2 c1 = sk.PositionOf(centre1), c2 = sk.PositionOf(centre2);
    const Vec2 d = (c2 - c1).Normalized(), n{-d.y, d.x};
    const double r = width / 2;
    const ObjectId a1 = sk.AddPoint(ids, c1 + n * r), b1 = sk.AddPoint(ids, c2 + n * r);
    const ObjectId a2 = sk.AddPoint(ids, c2 - n * r), b2 = sk.AddPoint(ids, c1 - n * r);
    SlotIds out;
    const ObjectId side1 = sk.AddLine(ids, a1, b1), side2 = sk.AddLine(ids, a2, b2);
    const ObjectId cap2 = sk.AddArc(ids, centre2, a2, b1);   // round the far end of centre 2
    const ObjectId cap1 = sk.AddArc(ids, centre1, a1, b2);   // round the far end of centre 1
    out.curves = {side1, cap2, side2, cap1};
    out.axis = sk.AddLine(ids, centre1, centre2, true);
    out.cap = cap1;
    for (ObjectId side : {side1, side2})
        for (ObjectId cap : {cap1, cap2}) sk.AddConstraint(ids, Make(ConstraintType::Tangent, side, cap));
    sk.AddConstraint(ids, Make(ConstraintType::Equal, cap1, cap2));
    return out;
}

SlotIds AddArcSlot(Sketch& sk, IdGenerator& ids, ObjectId centre, ObjectId start, ObjectId end, double width) {
    const Vec2 o = sk.PositionOf(centre), s = sk.PositionOf(start);
    const double radius = (s - o).Length();
    const Vec2 us = (s - o).Normalized(), ue = (sk.PositionOf(end) - o).Normalized();
    if (Point* p = sk.FindPoint(end)) p->pos = o + ue * radius;
    const Vec2 e = o + ue * radius;
    const double r = width / 2;
    const ObjectId sOut = sk.AddPoint(ids, s + us * r), sIn = sk.AddPoint(ids, s - us * r);
    const ObjectId eOut = sk.AddPoint(ids, e + ue * r), eIn = sk.AddPoint(ids, e - ue * r);
    SlotIds out;
    const ObjectId outer = sk.AddArc(ids, centre, sOut, eOut);
    const ObjectId inner = sk.AddArc(ids, centre, sIn, eIn);
    const ObjectId capEnd = sk.AddArc(ids, end, eOut, eIn);      // bulges past the end
    const ObjectId capStart = sk.AddArc(ids, start, sIn, sOut);  // bulges before the start
    out.curves = {outer, capEnd, inner, capStart};
    out.axis = sk.AddArc(ids, centre, start, end, true);
    out.cap = capStart;
    for (ObjectId cap : {capStart, capEnd})
        for (ObjectId side : {outer, inner}) sk.AddConstraint(ids, Make(ConstraintType::Tangent, cap, side));
    sk.AddConstraint(ids, Make(ConstraintType::Equal, capStart, capEnd));
    return out;
}

namespace {

void AppendArc(std::vector<Vec2>& pts, Vec2 c, double r, double a0, double a1, int n = 24) {
    for (int i = 0; i <= n; ++i) {
        const double a = a0 + (a1 - a0) * i / n;
        pts.push_back(c + Vec2{std::cos(a), std::sin(a)} * r);
    }
}

} // namespace

std::vector<Vec2> StraightSlotOutline(Vec2 c1, Vec2 c2, double width) {
    std::vector<Vec2> pts;
    if ((c2 - c1).Length() < 1e-12) return pts;
    const Vec2 d = (c2 - c1).Normalized();
    const double base = std::atan2(d.y, d.x), r = width / 2;
    AppendArc(pts, c2, r, base - kPi / 2, base + kPi / 2);
    AppendArc(pts, c1, r, base + kPi / 2, base + 3 * kPi / 2);
    return pts;
}

std::vector<Vec2> ArcSlotOutline(Vec2 o, Vec2 s, Vec2 e, double width) {
    std::vector<Vec2> pts;
    const double radius = (s - o).Length(), r = width / 2;
    if (radius < 1e-12) return pts;
    const double a0 = std::atan2(s.y - o.y, s.x - o.x);
    double a1 = std::atan2(e.y - o.y, e.x - o.x);
    while (a1 <= a0) a1 += 2 * kPi;
    const Vec2 se = o + Vec2{std::cos(a1), std::sin(a1)} * radius;
    AppendArc(pts, o, radius + r, a0, a1);
    AppendArc(pts, se, r, a1, a1 + kPi);
    AppendArc(pts, o, std::max(radius - r, 0.0), a1, a0);
    AppendArc(pts, s, r, a0 + kPi, a0 + 2 * kPi);
    return pts;
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

std::optional<TangentCircle> CircleTangentToLines(const std::array<std::pair<Vec2, Vec2>, 3>& lines,
                                                  const std::array<Vec2, 3>& picks) {
    // Each line as n.p = d with a unit normal; a circle touching it from side
    // s has n.c - d = s r. Try every side combination, keep the best fit.
    std::array<Vec2, 3> n;
    std::array<double, 3> d{};
    for (std::size_t i = 0; i < 3; ++i) {
        const Vec2 dir = lines[i].second - lines[i].first;
        if (dir.Length() < 1e-12) return std::nullopt;
        n[i] = Vec2{-dir.y, dir.x}.Normalized();
        d[i] = n[i].Dot(lines[i].first);
    }
    std::optional<TangentCircle> best;
    double bestMiss = 1e300;
    for (int signs = 0; signs < 8; ++signs) {
        const std::array<double, 3> s{signs & 1 ? -1.0 : 1.0, signs & 2 ? -1.0 : 1.0, signs & 4 ? -1.0 : 1.0};
        // [nx ny -s] [cx cy r]^T = d, by Cramer's rule.
        const auto det3 = [](const std::array<std::array<double, 3>, 3>& m) {
            return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                   m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
        };
        std::array<std::array<double, 3>, 3> m{};
        for (std::size_t i = 0; i < 3; ++i) m[i] = {n[i].x, n[i].y, -s[i]};
        const double det = det3(m);
        if (std::abs(det) < 1e-12) continue;
        std::array<double, 3> sol{};
        for (std::size_t k = 0; k < 3; ++k) {
            auto mk = m;
            for (std::size_t i = 0; i < 3; ++i) mk[i][k] = d[i];
            sol[k] = det3(mk) / det;
        }
        if (sol[2] <= 1e-9) continue;
        const Vec2 c{sol[0], sol[1]};
        double miss = 0;
        for (std::size_t i = 0; i < 3; ++i) miss += (c - n[i] * (n[i].Dot(c) - d[i]) - picks[i]).Length();
        if (miss < bestMiss) {
            bestMiss = miss;
            best = TangentCircle{c, sol[2]};
        }
    }
    return best;
}

} // namespace bz::sketch
