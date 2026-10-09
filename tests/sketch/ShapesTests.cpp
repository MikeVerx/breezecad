#include "sketch/Shapes.h"
#include "sketch/Solver.h"

#include <doctest.h>

#include <cmath>

using namespace bz;
using namespace bz::sketch;

namespace {

Constraint Dim(ConstraintType type, double value, ObjectId a, ObjectId b = {}) {
    Constraint c;
    c.type = type;
    c.refs = {a, b};
    c.parameter.value = value;
    c.parameter.expression = std::to_string(value);
    return c;
}

double LengthOf(const Sketch& sk, ObjectId line) {
    const Line* l = sk.FindLine(line);
    return (sk.PositionOf(l->end) - sk.PositionOf(l->start)).Length();
}

} // namespace

TEST_CASE("circle through three points") {
    const auto c = CircleThroughPoints({0, 0}, {2, 0}, {0, 2});
    REQUIRE(c);
    CHECK(c->centre.x == doctest::Approx(1.0));
    CHECK(c->centre.y == doctest::Approx(1.0));
    CHECK(c->radius == doctest::Approx(std::sqrt(2.0)));
    CHECK_FALSE(CircleThroughPoints({0, 0}, {1, 1}, {2, 2}));
}

TEST_CASE("centred rectangle keeps its centre when sized") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId centre = sk.AddPoint(ids, {5, 5});
    const RectangleIds r = AddCentredRectangle(sk, ids, centre, {12, 9});
    sk.AddConstraint(ids, Dim(ConstraintType::Length, 40, r.sides[0]));
    sk.AddConstraint(ids, Dim(ConstraintType::Length, 20, r.sides[1]));
    Constraint fix;
    fix.type = ConstraintType::Fix;
    fix.refs = {centre, {}};
    fix.fixedAt = {5, 5};
    sk.AddConstraint(ids, fix);
    REQUIRE(Solve(sk).converged);
    const Vec2 a = sk.PositionOf(r.corners[0]), c = sk.PositionOf(r.corners[2]);
    CHECK((a + c).x / 2 == doctest::Approx(5.0));
    CHECK((a + c).y / 2 == doctest::Approx(5.0));
    CHECK(std::abs(a.x - c.x) == doctest::Approx(40.0));
    CHECK(std::abs(a.y - c.y) == doctest::Approx(20.0));
}

TEST_CASE("three-point rectangle stays square at an angle") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId a = sk.AddPoint(ids, {0, 0});
    const ObjectId b = sk.AddPoint(ids, {30, 10});
    const auto r = AddThreePointRectangle(sk, ids, a, b, {25, 25});
    REQUIRE(r);
    sk.AddConstraint(ids, Dim(ConstraintType::Length, 15, r->sides[1]));
    REQUIRE(Solve(sk).converged);
    const Vec2 s0 = sk.PositionOf(r->corners[1]) - sk.PositionOf(r->corners[0]);
    const Vec2 s1 = sk.PositionOf(r->corners[2]) - sk.PositionOf(r->corners[1]);
    CHECK(s0.Dot(s1) == doctest::Approx(0.0).epsilon(1e-6));
    CHECK(LengthOf(sk, r->sides[3]) == doctest::Approx(15.0));
    const Vec2 inLine = sk.PositionOf(b) * 2.0 - sk.PositionOf(a);
    CHECK_FALSE(AddThreePointRectangle(sk, ids, a, b, inLine));
}

TEST_CASE("regular polygons inscribed and circumscribed") {
    for (const bool inscribed : {true, false}) {
        Sketch sk;
        IdGenerator ids;
        const ObjectId centre = sk.AddPoint(ids, {0, 0});
        const PolygonIds p = AddPolygon(sk, ids, centre, {7, 1}, 6, inscribed);
        REQUIRE(p.sides.size() == 6);
        sk.AddConstraint(ids, Dim(ConstraintType::Diameter, 20, p.circle));
        REQUIRE(Solve(sk).converged);
        const double side = inscribed ? 10.0 : 2 * 10 * std::tan(kPi / 6);
        for (ObjectId s : p.sides) CHECK(LengthOf(sk, s) == doctest::Approx(side).epsilon(1e-4));
    }
}

TEST_CASE("three points and centre rectangle keeps its centre") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId centre = sk.AddPoint(ids, {0, 0});
    const auto r = AddThreePointCentredRectangle(sk, ids, centre, {10, 10}, {-3, 3});
    REQUIRE(r);
    REQUIRE(Solve(sk).converged);
    const Vec2 a = sk.PositionOf(r->corners[0]), c = sk.PositionOf(r->corners[2]);
    CHECK((a + c).x == doctest::Approx(0.0).epsilon(1e-6));
    CHECK((a + c).y == doctest::Approx(0.0).epsilon(1e-6));
    CHECK(LengthOf(sk, r->sides[0]) == doctest::Approx(2 * std::sqrt(200.0)));
}

TEST_CASE("straight slot keeps its width when sized") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId c1 = sk.AddPoint(ids, {0, 0}), c2 = sk.AddPoint(ids, {30, 10});
    const SlotIds s = AddStraightSlot(sk, ids, c1, c2, 8);
    REQUIRE(s.curves.size() == 4);
    sk.AddConstraint(ids, Dim(ConstraintType::Diameter, 12, s.cap));
    REQUIRE(Solve(sk).converged);
    const Line* side = sk.FindLine(s.curves[0]);
    const Vec2 p = sk.PositionOf(side->start), q = sk.PositionOf(side->end);
    const Vec2 d = (q - p).Normalized();
    const Vec2 a = sk.PositionOf(c1), b = sk.PositionOf(c2);
    CHECK(std::abs((a - p).Cross(d)) == doctest::Approx(6.0).epsilon(1e-4));   // side 6 off the axis
    CHECK(std::abs((b - p).Cross(d)) == doctest::Approx(6.0).epsilon(1e-4));
}

TEST_CASE("arc slot solves around its centre") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId o = sk.AddPoint(ids, {0, 0}), s = sk.AddPoint(ids, {20, 0}), e = sk.AddPoint(ids, {0, 22});
    const SlotIds slot = AddArcSlot(sk, ids, o, s, e, 4);
    REQUIRE(slot.curves.size() == 4);
    CHECK(sk.PositionOf(e).y == doctest::Approx(20.0));   // moved onto the radius
    REQUIRE(Solve(sk).converged);
    CHECK(sk.RadiusOf(slot.curves[0]) - sk.RadiusOf(slot.curves[2]) == doctest::Approx(4.0).epsilon(1e-4));
    CHECK(ArcSlotOutline({0, 0}, {20, 0}, {0, 20}, 4).size() > 20);
    CHECK(StraightSlotOutline({0, 0}, {10, 0}, 4).size() > 20);
}

TEST_CASE("circle tangent to three lines: the one near the picks") {
    // A 3-4-5 right triangle: its incircle has radius 1, centre (1, 1).
    const std::array<std::pair<Vec2, Vec2>, 3> lines{{{{0, 0}, {4, 0}}, {{0, 0}, {0, 3}}, {{4, 0}, {0, 3}}}};
    const auto in = CircleTangentToLines(lines, {Vec2{1, 0}, Vec2{0, 1}, Vec2{1.6, 1.8}});
    REQUIRE(in);
    CHECK(in->radius == doctest::Approx(1.0));
    CHECK((in->centre - Vec2{1, 1}).Length() == doctest::Approx(0.0));
    // Picks outside, beyond the hypotenuse: the excircle there (radius 6).
    const auto ex = CircleTangentToLines(lines, {Vec2{6, 0}, Vec2{0, 6}, Vec2{2.4, 4.8}});
    REQUIRE(ex);
    CHECK(ex->radius == doctest::Approx(6.0));
}
