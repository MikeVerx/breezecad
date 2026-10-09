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
