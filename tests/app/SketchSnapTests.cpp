// 0.2.5: the cursor in a sketch snaps as in Inventor — middles of arcs,
// crossings of curves, a point on a curve that is also lined up with another
// point or level with the previous one, the tangent of the arc a line leaves,
// parallel / square to a line the cursor rested on.
#include "app/sketch/SketchPick.h"
#include "sketch/Solver.h"

#include <doctest.h>

#include <cmath>

using namespace bz;
using namespace bz::app;
using sketch::ConstraintType;

namespace {

/// Looking straight down at the sketch, 0.1 mm per pixel, (0,0) in the middle.
SketchView TopView() {
    SketchView v;
    v.vf.min = {0, 0};
    v.vf.max = {1000, 1000};
    ViewProjection& p = v.vf.projection;
    p.width = p.height = 1000;
    p.mmPerPixel = 0.1;
    p.matrix = {1.0 / 50, 0, 0, 0,  0, 1.0 / 50, 0, 0,  0, 0, -0.01, 0,  0, 0, 0, 1};
    return v;
}

ImVec2 At(const SketchView& v, Vec2 p) { return v.ToScreen(p); }

bool Near(Vec2 a, Vec2 b, double tol = 1e-6) { return (a - b).Length() < tol; }

SnapOptions NoGrid() {
    SnapOptions o;
    o.grid = false;
    return o;
}

} // namespace

TEST_CASE("привязка: середина дуги") {
    const SketchView v = TopView();
    sketch::Sketch sk;
    IdGenerator ids;
    const ObjectId c = sk.AddPoint(ids, {0, 0}), s = sk.AddPoint(ids, {10, 0}), e = sk.AddPoint(ids, {0, 10});
    const ObjectId arc = sk.AddArc(ids, c, s, e);
    const Vec2 mid{10 / std::sqrt(2.0), 10 / std::sqrt(2.0)};
    const Snap snap = SnapCursor(sk, v, At(v, mid + Vec2{0.3, -0.2}), NoGrid());
    CHECK(snap.kind == SnapKind::Midpoint);
    CHECK(snap.curve == arc);
    CHECK(Near(snap.pos, mid));
}

TEST_CASE("зависимость «середина» держит точку посередине дуги") {
    sketch::Sketch sk;
    IdGenerator ids;
    const ObjectId c = sk.AddPoint(ids, {0, 0}), s = sk.AddPoint(ids, {10, 0}), e = sk.AddPoint(ids, {0, 10});
    const ObjectId arc = sk.AddArc(ids, c, s, e);
    const ObjectId p = sk.AddPoint(ids, {6, 8});
    sketch::Constraint m;
    m.type = ConstraintType::Midpoint;
    m.refs = {p, arc, {}};
    sk.AddConstraint(ids, m);
    REQUIRE(sketch::Solve(sk).converged);
    const Vec2 at = sk.PositionOf(p), centre = sk.PositionOf(c);
    CHECK(std::abs((at - centre).Length() - (sk.PositionOf(s) - centre).Length()) < 1e-6);
    CHECK(std::abs((at - sk.PositionOf(s)).Length() - (at - sk.PositionOf(e)).Length()) < 1e-6);
}

TEST_CASE("привязка: пересечение двух кривых") {
    const SketchView v = TopView();
    sketch::Sketch sk;
    IdGenerator ids;
    const ObjectId l1 = sk.AddLine(ids, sk.AddPoint(ids, {-10, 1}), sk.AddPoint(ids, {10, 1}));
    const ObjectId l2 = sk.AddLine(ids, sk.AddPoint(ids, {3, -10}), sk.AddPoint(ids, {3, 10}));
    const Snap snap = SnapCursor(sk, v, At(v, {3.2, 1.3}), NoGrid());
    CHECK(snap.kind == SnapKind::Intersection);
    CHECK(((snap.curve == l1 && snap.curve2 == l2) || (snap.curve == l2 && snap.curve2 == l1)));
    CHECK(Near(snap.pos, {3, 1}));

    // A line crossing a circle.
    const ObjectId circle = sk.AddCircle(ids, sk.AddPoint(ids, {30, 0}), 5);
    const ObjectId l3 = sk.AddLine(ids, sk.AddPoint(ids, {20, 3}), sk.AddPoint(ids, {40, 3}));
    const Snap onCircle = SnapCursor(sk, v, At(v, {34.2, 3.1}), NoGrid());
    CHECK(onCircle.kind == SnapKind::Intersection);
    CHECK(((onCircle.curve == circle && onCircle.curve2 == l3) || (onCircle.curve == l3 && onCircle.curve2 == circle)));
    CHECK(Near(onCircle.pos, {34, 3}));
}

TEST_CASE("привязка: точка на кривой и вровень с другой точкой одновременно") {
    const SketchView v = TopView();
    sketch::Sketch sk;
    IdGenerator ids;
    const ObjectId line = sk.AddLine(ids, sk.AddPoint(ids, {-20, 0}), sk.AddPoint(ids, {20, 0}));
    sk.AddCircle(ids, sk.AddPoint(ids, {5, 10}), 2);
    const Snap snap = SnapCursor(sk, v, At(v, {5.3, 0.1}), NoGrid());
    CHECK(snap.kind == SnapKind::Curve);
    CHECK(snap.curve == line);
    REQUIRE(snap.alignedX.has_value());
    CHECK(Near(snap.pos, {5, 0}));
}

TEST_CASE("привязка: точка на окружности вровень с предыдущей точкой — горизонталь") {
    const SketchView v = TopView();
    sketch::Sketch sk;
    IdGenerator ids;
    const ObjectId circle = sk.AddCircle(ids, sk.AddPoint(ids, {0, 0}), 10);
    SnapOptions o = NoGrid();
    o.from = Vec2{-20, 6};
    const Snap snap = SnapCursor(sk, v, At(v, {8.0, 6.3}), o);
    CHECK(snap.kind == SnapKind::Curve);
    CHECK(snap.curve == circle);
    CHECK(snap.horizontal);
    CHECK(Near(snap.pos, {8, 6}));
}

TEST_CASE("привязка: отрезок из конца дуги по касательной") {
    const SketchView v = TopView();
    sketch::Sketch sk;
    IdGenerator ids;
    const double k = 10 / std::sqrt(2.0);
    const ObjectId e = sk.AddPoint(ids, {k, k});
    const ObjectId arc = sk.AddArc(ids, sk.AddPoint(ids, {0, 0}), sk.AddPoint(ids, {10, 0}), e);
    SnapOptions o = NoGrid();
    o.from = Vec2{k, k};
    o.fromPoint = e;
    const Snap snap = SnapCursor(sk, v, At(v, Vec2{k, k} + Vec2{-10, 10.3}), o);
    CHECK(snap.tangentTo == arc);
    const Vec2 d = (snap.pos - Vec2{k, k}).Normalized();
    CHECK(std::abs(d.Dot(Vec2{k, k}.Normalized())) < 1e-9);   // square to the radius
}

TEST_CASE("привязка: параллельно отрезку, на который наводили курсор, и вровень с точкой") {
    const SketchView v = TopView();
    sketch::Sketch sk;
    IdGenerator ids;
    const ObjectId ref = sk.AddLine(ids, sk.AddPoint(ids, {0, -30}), sk.AddPoint(ids, {10, -20}));
    SnapOptions o = NoGrid();
    o.from = Vec2{-30, 0};
    o.reference = ref;
    const Snap parallel = SnapCursor(sk, v, At(v, {-20, 10.3}), o);
    CHECK(parallel.parallelTo == ref);
    const Vec2 d = parallel.pos - Vec2{-30, 0};
    CHECK(std::abs(d.x - d.y) < 1e-9);

    // A point above: the end slides along the direction to line up with it.
    sk.AddPoint(ids, {-15, 30});
    const Snap both = SnapCursor(sk, v, At(v, {-15.2, 15.4}), o);
    CHECK(both.parallelTo == ref);
    REQUIRE(both.alignedX.has_value());
    CHECK(Near(both.pos, {-15, 15}));

    // Square to it as well.
    const Snap square = SnapCursor(sk, v, At(v, {-40, 10.2}), o);
    CHECK(square.perpendicularTo == ref);
}

TEST_CASE("привязка: существующая точка важнее всего остального") {
    const SketchView v = TopView();
    sketch::Sketch sk;
    IdGenerator ids;
    const ObjectId a = sk.AddPoint(ids, {0, 0});
    sk.AddLine(ids, a, sk.AddPoint(ids, {10, 0}));
    sk.AddLine(ids, a, sk.AddPoint(ids, {0, 10}));
    const Snap snap = SnapCursor(sk, v, At(v, {0.2, 0.1}), NoGrid());
    CHECK(snap.kind == SnapKind::Point);
    CHECK(snap.point == a);
}
