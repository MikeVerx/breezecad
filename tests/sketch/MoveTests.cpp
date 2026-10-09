#include "sketch/Move.h"
#include "sketch/Shapes.h"
#include "sketch/Solver.h"

#include <doctest.h>

#include <cmath>

using namespace bz;
using namespace bz::sketch;

TEST_CASE("move a rectangle and rotate it, it stays a rectangle") {
    Sketch sk;
    IdGenerator ids;
    const RectangleIds r = AddCornerRectangle(sk, ids, sk.AddPoint(ids, {0, 0}), sk.AddPoint(ids, {40, 20}));
    const std::vector<ObjectId> all(r.sides.begin(), r.sides.end());

    REQUIRE(MoveEntities(sk, ids, all, {{0, 0}, 0, {10, 5}}));
    REQUIRE(Solve(sk).converged);
    CHECK(sk.PositionOf(r.corners[0]).x == doctest::Approx(10.0));
    CHECK(sk.PositionOf(r.corners[2]).y == doctest::Approx(25.0));

    REQUIRE(MoveEntities(sk, ids, all, {sk.PositionOf(r.corners[0]), kPi / 6, {}}));
    REQUIRE(Solve(sk).converged);
    const Vec2 s0 = sk.PositionOf(r.corners[1]) - sk.PositionOf(r.corners[0]);
    const Vec2 s1 = sk.PositionOf(r.corners[2]) - sk.PositionOf(r.corners[1]);
    CHECK(std::atan2(s0.y, s0.x) == doctest::Approx(kPi / 6));   // turned, not snapped back
    CHECK(s0.Dot(s1) == doctest::Approx(0.0).epsilon(1e-6));
    CHECK(s0.Length() == doctest::Approx(40.0));
}

TEST_CASE("the origin stays put") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId origin = sk.EnsureOrigin(ids);
    const ObjectId line = sk.AddLine(ids, origin, sk.AddPoint(ids, {10, 0}));
    const ObjectId lone = line;
    REQUIRE(MoveEntities(sk, ids, std::span(&lone, 1), {{0, 0}, 0, {5, 5}}));
    CHECK(sk.PositionOf(origin).x == doctest::Approx(0.0));
    CHECK(sk.PositionOf(sk.FindLine(line)->end).x == doctest::Approx(15.0));
}

TEST_CASE("copies keep their shape and follow the original's size") {
    Sketch sk;
    IdGenerator ids;
    const RectangleIds r = AddCornerRectangle(sk, ids, sk.AddPoint(ids, {0, 0}), sk.AddPoint(ids, {10, 5}));
    Constraint len;
    len.type = ConstraintType::Length;
    len.refs = {r.sides[0], {}, {}};
    len.parameter.value = 10;
    sk.AddConstraint(ids, len);
    const std::vector<ObjectId> all(r.sides.begin(), r.sides.end());
    const auto copies = CopyEntities(sk, ids, all, {{0, 0}, 0, {20, 0}});
    REQUIRE(copies.size() == 4);
    for (Constraint& c : sk.constraints)
        if (c.type == ConstraintType::Length) c.parameter.value = 14;
    REQUIRE(Solve(sk).converged);
    const Line* copy = sk.FindLine(copies[0]);
    CHECK((sk.PositionOf(copy->end) - sk.PositionOf(copy->start)).Length() == doctest::Approx(14.0));
}

TEST_CASE("scaling a circle and a line about a point") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId circle = sk.AddCircle(ids, sk.AddPoint(ids, {10, 0}), 3);
    const ObjectId line = sk.AddLine(ids, sk.AddPoint(ids, {0, 0}), sk.AddPoint(ids, {4, 0}));
    const ObjectId chosen[] = {circle, line};
    REQUIRE(ScaleEntities(sk, chosen, {0, 0}, 2));
    CHECK(sk.RadiusOf(circle) == doctest::Approx(6.0));
    CHECK(sk.PositionOf(sk.FindCircle(circle)->center).x == doctest::Approx(20.0));
    CHECK(sk.PositionOf(sk.FindLine(line)->end).x == doctest::Approx(8.0));
}
