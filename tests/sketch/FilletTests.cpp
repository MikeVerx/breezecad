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
    CHECK(sk.FindPoint(a));                           // the corner stays: a virtual sharp
    CHECK(sk.points.size() == pointsBefore + 3);      // two tangent points and a centre
    const Arc* arc = sk.FindArc(*arcId);
    REQUIRE(arc);
    CHECK((sk.PositionOf(arc->center) - Vec2{5, 5}).Length() == doctest::Approx(0.0));
    CHECK(sk.RadiusOf(*arcId) == doctest::Approx(5.0));
    REQUIRE(Solve(sk).converged);
    CHECK(sk.RadiusOf(*arcId) == doctest::Approx(5.0));
    // Each tangent point is an end of one of the sides and of one phantom.
    int ends = 0, phantomEnds = 0;
    for (const Line& l : sk.lines)
        for (const ObjectId p : {l.start, l.end})
            if (p == arc->start || p == arc->end) (l.construction ? phantomEnds : ends) += 1;
    CHECK(ends == 2);
    CHECK(phantomEnds == 2);
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

namespace {

Constraint Dim(ConstraintType type, double value, ObjectId a, ObjectId b = {}) {
    Constraint c;
    c.type = type;
    c.refs = {a, b, {}};
    c.parameter.value = value;
    return c;
}

} // namespace

TEST_CASE("fillet a corner of a centred rectangle: the diagonals are helpers, not sides") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId centre = sk.AddPoint(ids, {0, 0});
    const RectangleIds r = AddCentredRectangle(sk, ids, centre, {-20, 10});
    int diagonals = 0;
    for (const Line& l : sk.lines) diagonals += l.construction ? 1 : 0;
    CHECK(diagonals == 2);   // both diagonals, as in Inventor
    REQUIRE(Solve(sk).converged);
    // Every corner has two sides and one diagonal: each can be rounded.
    for (const ObjectId corner : r.corners) {
        const auto lines = CornerLines(sk, corner);
        REQUIRE(lines);
        CHECK_FALSE(sk.FindLine(lines->first)->construction);
        CHECK_FALSE(sk.FindLine(lines->second)->construction);
    }
    REQUIRE(FilletCorner(sk, ids, r.corners[0], 4.0));
    REQUIRE(Solve(sk).converged);
}

TEST_CASE("fillet moves nothing: dimensions on the corner and the sides keep holding") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId origin = sk.EnsureOrigin(ids);
    const ObjectId a = sk.AddPoint(ids, {10, 5}), b = sk.AddPoint(ids, {50, 35});
    const RectangleIds r = AddCornerRectangle(sk, ids, a, b);
    // The corner placed from the origin, the sides sized: fully defined.
    sk.AddConstraint(ids, Dim(ConstraintType::DistanceX, 10, origin, a));
    sk.AddConstraint(ids, Dim(ConstraintType::DistanceY, 5, origin, a));
    sk.AddConstraint(ids, Dim(ConstraintType::Length, 40, r.sides[0]));
    sk.AddConstraint(ids, Dim(ConstraintType::Length, 30, r.sides[3]));
    REQUIRE(Solve(sk).converged);
    std::vector<std::pair<ObjectId, Vec2>> before;
    for (const Point& p : sk.points) before.push_back({p.id, p.pos});

    const auto arcId = FilletCorner(sk, ids, a, 6.0);
    REQUIRE(arcId);
    sk.AddConstraint(ids, Dim(ConstraintType::Radius, 6, *arcId));
    REQUIRE(Solve(sk).converged);
    for (const auto& [id, pos] : before) {
        REQUIRE(sk.FindPoint(id));
        CHECK((sk.PositionOf(id) - pos).Length() == doctest::Approx(0.0).epsilon(1e-7));
    }
    CHECK(Analyze(sk).degreesOfFreedom == 0);   // still fully defined
    // A side's length now runs to the virtual sharp.
    for (const Constraint& c : sk.constraints) CHECK(c.type != ConstraintType::Length);
    // Changing a dimension still drives the shape through the corner.
    for (Constraint& c : sk.constraints)
        if (c.type == ConstraintType::Distance && c.parameter.value == 40) c.parameter.value = 60;
    REQUIRE(Solve(sk).converged);
    CHECK((sk.PositionOf(r.corners[1]) - Vec2{70, 5}).Length() == doctest::Approx(0.0).epsilon(1e-6));
    CHECK((sk.PositionOf(a) - Vec2{10, 5}).Length() == doctest::Approx(0.0).epsilon(1e-6));
}

TEST_CASE("fillet keeps equal sides equal through a helper line") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId a = sk.AddPoint(ids, {0, 0}), b = sk.AddPoint(ids, {30, 30});
    const RectangleIds r = AddCornerRectangle(sk, ids, a, b);
    Constraint eq;
    eq.type = ConstraintType::Equal;
    eq.refs = {r.sides[0], r.sides[1], {}};
    sk.AddConstraint(ids, eq);
    REQUIRE(Solve(sk).converged);
    REQUIRE(FilletCorner(sk, ids, a, 5.0));
    REQUIRE(Solve(sk).converged);
    CHECK((sk.PositionOf(b) - Vec2{30, 30}).Length() == doctest::Approx(0.0).epsilon(1e-7));
    // The virtual sharp is not a corner to round again.
    CHECK_FALSE(CornerLines(sk, a));
}
