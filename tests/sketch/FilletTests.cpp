#include "sketch/Fillet.h"
#include "sketch/Shapes.h"
#include "sketch/Solver.h"

#include <doctest.h>

#include <cmath>

using namespace bz;
using namespace bz::sketch;

TEST_CASE("fillet a rectangle corner: tangent arc, shortened sides, solvable") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId a = sk.AddPoint(ids, {0, 0}), b = sk.AddPoint(ids, {40, 20});
    AddCornerRectangle(sk, ids, a, b);
    const std::size_t pointsBefore = sk.points.size();

    const auto arcId = FilletCorner(sk, ids, a, 5.0);
    REQUIRE(arcId);
    CHECK_FALSE(sk.FindPoint(a));                     // the corner is gone
    CHECK(sk.points.size() == pointsBefore + 2);      // two tangent points and a centre, minus the corner
    const Arc* arc = sk.FindArc(*arcId);
    REQUIRE(arc);
    CHECK((sk.PositionOf(arc->center) - Vec2{5, 5}).Length() == doctest::Approx(0.0));
    CHECK(sk.RadiusOf(*arcId) == doctest::Approx(5.0));
    REQUIRE(Solve(sk).converged);
    CHECK(sk.RadiusOf(*arcId) == doctest::Approx(5.0));
    // Each tangent point is an end of one of the sides.
    int ends = 0;
    for (const Line& l : sk.lines)
        for (const ObjectId p : {l.start, l.end}) ends += (p == arc->start || p == arc->end) ? 1 : 0;
    CHECK(ends == 2);
}

TEST_CASE("fillet refuses what does not fit") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId a = sk.AddPoint(ids, {0, 0}), b = sk.AddPoint(ids, {10, 10});
    AddCornerRectangle(sk, ids, a, b);
    CHECK_FALSE(PlanFillet(sk, a, 12.0));   // longer than the sides
    CHECK_FALSE(PlanFillet(sk, a, 0.0));
    const ObjectId lone = sk.AddPoint(ids, {50, 50});
    CHECK_FALSE(PlanFillet(sk, lone, 1.0));   // not a corner
    // Two lines in one line make no corner.
    const ObjectId m = sk.AddPoint(ids, {100, 0});
    sk.AddLine(ids, sk.AddPoint(ids, {90, 0}), m);
    sk.AddLine(ids, m, sk.AddPoint(ids, {110, 0}));
    CHECK_FALSE(PlanFillet(sk, m, 1.0));
}
