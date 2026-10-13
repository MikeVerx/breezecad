#include "sketch/Solver.h"
#include "sketch/SplineGeometry.h"

#include <doctest.h>

#include <chrono>
#include <cmath>

using namespace bz;
using namespace bz::sketch;

namespace {

Constraint Geo(ConstraintType type, ObjectId a, ObjectId b = {}) {
    Constraint c;
    c.type = type;
    c.refs = {a, b};
    return c;
}

Constraint Dim(ConstraintType type, double value, ObjectId a, ObjectId b = {}) {
    Constraint c = Geo(type, a, b);
    c.parameter.value = value;
    c.parameter.expression = std::to_string(value);
    return c;
}

/// Slightly crooked quadrilateral that constraints turn into a rectangle.
struct Rect {
    Sketch      sk;
    IdGenerator ids;
    ObjectId    p[4];
    ObjectId    l[4];
    ObjectId    origin;

    Rect() {
        origin = sk.EnsureOrigin(ids);
        p[0] = sk.AddPoint(ids, {0.5, -0.3});
        p[1] = sk.AddPoint(ids, {52.0, 1.0});
        p[2] = sk.AddPoint(ids, {49.0, 33.0});
        p[3] = sk.AddPoint(ids, {-1.0, 31.0});
        for (int i = 0; i < 4; ++i) l[i] = sk.AddLine(ids, p[i], p[(i + 1) % 4]);
        sk.AddConstraint(ids, Geo(ConstraintType::Horizontal, l[0]));
        sk.AddConstraint(ids, Geo(ConstraintType::Vertical, l[1]));
        sk.AddConstraint(ids, Geo(ConstraintType::Horizontal, l[2]));
        sk.AddConstraint(ids, Geo(ConstraintType::Vertical, l[3]));
    }
};

bool Near(Vec2 a, Vec2 b, double tol = 1e-6) { return (a - b).Length() < tol; }

} // namespace

TEST_CASE("an under-defined rectangle keeps its freedom") {
    Rect r;
    const SolveResult res = Solve(r.sk);
    REQUIRE(res.converged);
    CHECK(res.degreesOfFreedom == 4);   // position (2) + width + height
    CHECK(std::abs(r.sk.PositionOf(r.p[0]).y - r.sk.PositionOf(r.p[1]).y) < 1e-9);
    CHECK(std::abs(r.sk.PositionOf(r.p[1]).x - r.sk.PositionOf(r.p[2]).x) < 1e-9);
}

TEST_CASE("a dimensioned rectangle at the origin is fully defined") {
    Rect r;
    r.sk.AddConstraint(r.ids, Geo(ConstraintType::Coincident, r.p[0], r.origin));
    r.sk.AddConstraint(r.ids, Dim(ConstraintType::Length, 60.0, r.l[0]));
    r.sk.AddConstraint(r.ids, Dim(ConstraintType::Length, 40.0, r.l[1]));

    const SolveResult res = Solve(r.sk);
    REQUIRE(res.converged);
    CHECK(res.degreesOfFreedom == 0);
    CHECK(Near(r.sk.PositionOf(r.p[0]), {0, 0}));
    CHECK(Near(r.sk.PositionOf(r.p[1]), {60, 0}));
    CHECK(Near(r.sk.PositionOf(r.p[2]), {60, 40}));
    CHECK(Near(r.sk.PositionOf(r.p[3]), {0, 40}));
    for (ObjectId line : r.l) CHECK(res.fullyDefined.count(line) == 1);
}

TEST_CASE("redundant constraints are detected") {
    Rect r;
    r.sk.AddConstraint(r.ids, Geo(ConstraintType::Coincident, r.p[0], r.origin));
    r.sk.AddConstraint(r.ids, Dim(ConstraintType::Length, 60.0, r.l[0]));
    r.sk.AddConstraint(r.ids, Dim(ConstraintType::Length, 40.0, r.l[1]));
    REQUIRE(Solve(r.sk).converged);

    CHECK(IsRedundant(r.sk, Geo(ConstraintType::Parallel, r.l[0], r.l[2])));
    CHECK(IsRedundant(r.sk, Dim(ConstraintType::Length, 60.0, r.l[2])));
    CHECK(IsRedundant(r.sk, Geo(ConstraintType::Fix, r.p[2])));

    Rect free;
    REQUIRE(Solve(free.sk).converged);
    CHECK_FALSE(IsRedundant(free.sk, Dim(ConstraintType::Length, 60.0, free.l[0])));
    CHECK_FALSE(IsRedundant(free.sk, Geo(ConstraintType::Equal, free.l[0], free.l[1])));
}

TEST_CASE("conflicting dimensions fail and leave the sketch untouched") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId a = sk.AddPoint(ids, {0, 0});
    const ObjectId b = sk.AddPoint(ids, {10, 0});
    const ObjectId line = sk.AddLine(ids, a, b);
    sk.AddConstraint(ids, Dim(ConstraintType::Length, 20.0, line));
    const ObjectId bad = sk.AddConstraint(ids, Dim(ConstraintType::Distance, 30.0, a, b));

    const SolveResult res = Solve(sk);
    CHECK_FALSE(res.converged);
    CHECK(Near(sk.PositionOf(b), {10, 0}));
    CHECK_FALSE(res.failing.empty());
    bool reportsBad = false;
    for (ObjectId id : res.failing) reportsBad = reportsBad || id == bad;
    CHECK(reportsBad);
}

TEST_CASE("angle, tangency and radius") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId o = sk.EnsureOrigin(ids);
    const ObjectId a = sk.AddPoint(ids, {0, 0});
    const ObjectId b = sk.AddPoint(ids, {50, 3});
    const ObjectId c = sk.AddPoint(ids, {40, 20});
    const ObjectId base = sk.AddLine(ids, a, b);
    const ObjectId slope = sk.AddLine(ids, a, c);
    sk.AddConstraint(ids, Geo(ConstraintType::Coincident, a, o));
    sk.AddConstraint(ids, Geo(ConstraintType::Horizontal, base));
    sk.AddConstraint(ids, Dim(ConstraintType::Angle, kPi / 6, base, slope));

    const ObjectId center = sk.AddPoint(ids, {30, 12});
    const ObjectId circle = sk.AddCircle(ids, center, 8.0);
    sk.AddConstraint(ids, Geo(ConstraintType::Tangent, base, circle));
    sk.AddConstraint(ids, Dim(ConstraintType::Radius, 10.0, circle));

    REQUIRE(Solve(sk).converged);
    const Vec2 dir = (sk.PositionOf(c) - sk.PositionOf(a)).Normalized();
    CHECK(std::atan2(dir.y, dir.x) == doctest::Approx(kPi / 6));
    CHECK(sk.RadiusOf(circle) == doctest::Approx(10.0));
    CHECK(sk.PositionOf(center).y == doctest::Approx(10.0));   // touches the x axis from above
}

TEST_CASE("arc ends stay on one circle") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId c = sk.AddPoint(ids, {0, 0});
    const ObjectId s = sk.AddPoint(ids, {10, 0});
    const ObjectId e = sk.AddPoint(ids, {0, 12});
    const ObjectId arc = sk.AddArc(ids, c, s, e);
    sk.AddConstraint(ids, Dim(ConstraintType::Radius, 15.0, arc));
    REQUIRE(Solve(sk).converged);
    CHECK((sk.PositionOf(s) - sk.PositionOf(c)).Length() == doctest::Approx(15.0));
    CHECK((sk.PositionOf(e) - sk.PositionOf(c)).Length() == doctest::Approx(15.0));
}

TEST_CASE("dragging moves the dragged point and keeps constraints") {
    Rect r;
    r.sk.AddConstraint(r.ids, Geo(ConstraintType::Coincident, r.p[0], r.origin));
    REQUIRE(Solve(r.sk).converged);

    const SolveResult res = Solve(r.sk, {.drags = {Drag{r.p[2], {80, 50}}}});
    REQUIRE(res.converged);
    CHECK(Near(r.sk.PositionOf(r.p[2]), {80, 50}));
    CHECK(Near(r.sk.PositionOf(r.p[0]), {0, 0}));
    CHECK(r.sk.PositionOf(r.p[1]).x == doctest::Approx(80.0));
    CHECK(r.sk.PositionOf(r.p[3]).y == doctest::Approx(50.0));
}

TEST_CASE("dragging a fully defined point does not break the sketch") {
    Rect r;
    r.sk.AddConstraint(r.ids, Geo(ConstraintType::Coincident, r.p[0], r.origin));
    r.sk.AddConstraint(r.ids, Dim(ConstraintType::Length, 60.0, r.l[0]));
    r.sk.AddConstraint(r.ids, Dim(ConstraintType::Length, 40.0, r.l[1]));
    REQUIRE(Solve(r.sk).converged);

    const SolveResult res = Solve(r.sk, {.drags = {Drag{r.p[2], {90, 90}}}});
    REQUIRE(res.converged);
    CHECK(Near(r.sk.PositionOf(r.p[2]), {60, 40}));
}

TEST_CASE("removing a point removes its curves and constraints") {
    Rect r;
    r.sk.Remove(r.p[1]);
    CHECK(r.sk.lines.size() == 2);   // the two lines through p1 are gone
    CHECK(r.sk.FindPoint(r.p[1]) == nullptr);
    for (const Constraint& c : r.sk.constraints) {
        CHECK(c.refs[0] != r.l[0]);
        CHECK(c.refs[0] != r.l[1]);
    }
    r.sk.Remove(r.origin);
    CHECK(r.sk.Origin() != nullptr);
}

TEST_CASE("merging points reconnects curves") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId a = sk.AddPoint(ids, {0, 0});
    const ObjectId b = sk.AddPoint(ids, {10, 0});
    const ObjectId c = sk.AddPoint(ids, {10, 0.001});
    const ObjectId d = sk.AddPoint(ids, {10, 10});
    sk.AddLine(ids, a, b);
    const ObjectId second = sk.AddLine(ids, c, d);
    sk.MergePoints(c, b);
    CHECK(sk.FindPoint(c) == nullptr);
    CHECK(sk.FindLine(second)->start == b);
}

TEST_CASE("extents cover curves, scaling keeps the shape") {
    Sketch sk;
    IdGenerator ids;
    sk.EnsureOrigin(ids);
    CHECK(sk.Extents().IsEmpty());
    const ObjectId a = sk.AddPoint(ids, {2, 1});
    const ObjectId b = sk.AddPoint(ids, {12, 1});
    sk.AddLine(ids, a, b);
    const ObjectId centre = sk.AddPoint(ids, {20, 5});
    const ObjectId circle = sk.AddCircle(ids, centre, 3);

    Box2 box = sk.Extents();
    CHECK(Near(box.min, {2, 1}));
    CHECK(Near(box.max, {23, 8}));

    sk.ScaleAbout({0, 0}, 2.0);
    CHECK(Near(sk.PositionOf(a), {4, 2}));
    CHECK(Near(sk.PositionOf(b), {24, 2}));
    CHECK(sk.RadiusOf(circle) == doctest::Approx(6.0));
    CHECK(Near(sk.Origin()->pos, {0, 0}));
}

TEST_CASE("a distance to an осевая is a diameter") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId axis = sk.AddLine(ids, sk.AddPoint(ids, {0, 0}), sk.AddPoint(ids, {0, 10}));
    sk.FindLine(axis)->centerline = true;
    Constraint hold;
    hold.type = ConstraintType::Fix;
    hold.refs = {sk.FindLine(axis)->start, {}, {}};
    hold.fixedAt = {0, 0};
    sk.AddConstraint(ids, hold);
    sk.AddConstraint(ids, Geo(ConstraintType::Vertical, axis));
    const ObjectId p = sk.AddPoint(ids, {7, 3});
    Constraint d = Dim(ConstraintType::PointLineDistance, 30, p, axis);
    CHECK(IsDiametric(sk, d));
    sk.AddConstraint(ids, d);
    REQUIRE(Solve(sk).converged);
    CHECK(std::abs(sk.PositionOf(p).x) == doctest::Approx(15.0));   // radius, from a diameter of 30
}

namespace {

/// Distance from `p` to the drawn spline (dense samples).
double DistanceToSpline(const Sketch& sk, const Spline& s, Vec2 p) {
    double best = 1e300;
    const std::vector<Vec2> pts = SampleSpline(SplineShape(sk, s), 400);
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const Vec2 a = pts[i], d = pts[i + 1] - a;
        const double t = std::clamp((p - a).Dot(d) / std::max(d.Dot(d), 1e-18), 0.0, 1.0);
        best = std::min(best, (p - (a + d * t)).Length());
    }
    return best;
}

Constraint Make2(ConstraintType t, ObjectId a, ObjectId b) {
    Constraint c;
    c.type = t;
    c.refs = {a, b, {}};
    return c;
}

} // namespace

TEST_CASE("a point is held on a spline and on an ellipse") {
    IdGenerator ids;
    Sketch sk;
    const ObjectId s = sk.AddSpline(ids, {sk.AddPoint(ids, {0, 0}), sk.AddPoint(ids, {10, 6}), sk.AddPoint(ids, {20, 0})}, true);
    const ObjectId p = sk.AddPoint(ids, {9, 12});
    sk.AddConstraint(ids, Make2(ConstraintType::PointOnCurve, p, s));
    const ObjectId e = sk.AddEllipse(ids, sk.AddPoint(ids, {40, 0}), sk.AddPoint(ids, {50, 0}), sk.AddPoint(ids, {40, 4}));
    const ObjectId q = sk.AddPoint(ids, {47, 6});
    sk.AddConstraint(ids, Make2(ConstraintType::PointOnCurve, q, e));
    REQUIRE(Solve(sk).converged);
    CHECK(DistanceToSpline(sk, *sk.FindSpline(s), sk.PositionOf(p)) < 1e-3);
    const Ellipse& el = *sk.FindEllipse(e);
    const Vec2 c = sk.PositionOf(el.center), ma = sk.PositionOf(el.major) - c, mi = sk.PositionOf(el.minor) - c;
    const Vec2 d = sk.PositionOf(q) - c;
    const double u = d.Dot(ma) / ma.Dot(ma), v = d.Dot(mi) / mi.Dot(mi);
    CHECK(u * u + v * v == doctest::Approx(1.0).epsilon(1e-4));
}

TEST_CASE("a line is tangent to a spline at its end, and to an ellipse") {
    IdGenerator ids;
    Sketch sk;
    const ObjectId start = sk.AddPoint(ids, {0, 0});
    const ObjectId s = sk.AddSpline(ids, {start, sk.AddPoint(ids, {10, 6}), sk.AddPoint(ids, {20, 0})}, true);
    const ObjectId l = sk.AddLine(ids, sk.AddPoint(ids, {-10, 2}), start);
    sk.AddConstraint(ids, Make2(ConstraintType::Tangent, l, s));
    const ObjectId e = sk.AddEllipse(ids, sk.AddPoint(ids, {40, 0}), sk.AddPoint(ids, {50, 0}), sk.AddPoint(ids, {40, 4}));
    const ObjectId l2 = sk.AddLine(ids, sk.AddPoint(ids, {30, 5}), sk.AddPoint(ids, {50, 6}));
    sk.AddConstraint(ids, Make2(ConstraintType::Tangent, l2, e));
    REQUIRE(Solve(sk).converged);

    const BSpline b = SplineShape(sk, *sk.FindSpline(s));
    const Vec2 t = (b.poles[1] - b.poles[0]).Normalized();
    const Line& line = *sk.FindLine(l);
    const Vec2 dl = (sk.PositionOf(line.end) - sk.PositionOf(line.start)).Normalized();
    CHECK(std::abs(t.Cross(dl)) < 1e-6);

    // The ellipse touches the line: the nearest sample is on it, none beyond.
    const Line& tl = *sk.FindLine(l2);
    const Vec2 a = sk.PositionOf(tl.start), n = (sk.PositionOf(tl.end) - a).Normalized();
    double lo = 1e300, hi = -1e300;
    for (const Vec2& pt : SampleEllipse(sk, *sk.FindEllipse(e), 2000)) {
        lo = std::min(lo, n.Cross(pt - a));
        hi = std::max(hi, n.Cross(pt - a));
    }
    CHECK(std::min(std::abs(lo), std::abs(hi)) < 1e-3);
    CHECK(lo * hi >= -1e-6);
}

TEST_CASE("removing a constraint removes only the constraint") {
    IdGenerator ids;
    Sketch sk;
    const ObjectId a = sk.AddPoint(ids, {0, 0}), b = sk.AddPoint(ids, {20, 0}), c = sk.AddPoint(ids, {20, 10}), d = sk.AddPoint(ids, {0, 10});
    const ObjectId bottom = sk.AddLine(ids, a, b);
    sk.AddLine(ids, b, c);
    sk.AddLine(ids, c, d);
    const ObjectId left = sk.AddLine(ids, d, a);
    for (Line& l : sk.lines)
        if (l.id == left) l.centerline = true;
    const ObjectId inner = sk.AddLine(ids, sk.AddPoint(ids, {10, 0}), sk.AddPoint(ids, {10, 8}));
    const ObjectId perp = sk.AddConstraint(ids, Make2(ConstraintType::Perpendicular, bottom, inner));
    REQUIRE(Solve(sk).converged);
    sk.Remove(perp);
    CHECK(sk.lines.size() == 5);
    CHECK(sk.points.size() == 6);
    CHECK(sk.FindLine(left)->centerline);
    CHECK(sk.FindConstraint(perp) == nullptr);
}

TEST_CASE("B-03: a huge angle dimension does not hang the solver") {
    // Audit 2026-10-13: 1e9 deg took a second, 1e15 deg never finished.
    for (const double degrees : {1e9, 1e15, 1e300}) {
        CAPTURE(degrees);
        Sketch sk;
        IdGenerator ids;
        const ObjectId a = sk.AddPoint(ids, {0, 0}), b = sk.AddPoint(ids, {10, 0});
        const ObjectId c = sk.AddPoint(ids, {0, 0.1}), d = sk.AddPoint(ids, {10, 5});
        const ObjectId l1 = sk.AddLine(ids, a, b), l2 = sk.AddLine(ids, c, d);
        sk.AddConstraint(ids, Dim(ConstraintType::Angle, degrees * kPi / 180.0, l1, l2));
        const auto start = std::chrono::steady_clock::now();
        (void)Solve(sk);
        CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
    }
}
