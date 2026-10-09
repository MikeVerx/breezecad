#include "sketch/Mirror.h"
#include "sketch/Shapes.h"
#include "sketch/Solver.h"

#include <doctest.h>

using namespace bz;
using namespace bz::sketch;

TEST_CASE("mirrored geometry follows its original") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId axisStart = sk.AddPoint(ids, {0, -10});
    const ObjectId axis = sk.AddLine(ids, axisStart, sk.AddPoint(ids, {0, 10}), true);
    // The axis stays put: fixed at one end, vertical.
    Constraint hold;
    hold.type = ConstraintType::Fix;
    hold.refs = {axisStart, {}, {}};
    hold.fixedAt = {0, -10};
    sk.AddConstraint(ids, hold);
    Constraint upright;
    upright.type = ConstraintType::Vertical;
    upright.refs = {axis, {}, {}};
    sk.AddConstraint(ids, upright);
    const RectangleIds r = AddCornerRectangle(sk, ids, sk.AddPoint(ids, {5, 0}), sk.AddPoint(ids, {15, 8}));
    const ObjectId arcCentre = sk.AddPoint(ids, {20, 0});
    const ObjectId arc = sk.AddArc(ids, arcCentre, sk.AddPoint(ids, {23, 0}), sk.AddPoint(ids, {20, 3}));
    std::vector<ObjectId> chosen(r.sides.begin(), r.sides.end());
    chosen.push_back(arc);

    const std::vector<ObjectId> copies = MirrorEntities(sk, ids, chosen, axis);
    REQUIRE(copies.size() == 5);
    REQUIRE(Solve(sk).converged);
    const Line* copy = sk.FindLine(copies[0]);
    CHECK(sk.PositionOf(copy->start).x == doctest::Approx(-5.0));

    // Move the original: the copy follows.
    Constraint fix;
    fix.type = ConstraintType::Fix;
    fix.refs = {r.corners[0], {}, {}};
    fix.fixedAt = {7, 1};
    sk.AddConstraint(ids, fix);
    REQUIRE(Solve(sk).converged);
    CHECK(sk.PositionOf(copy->start).x == doctest::Approx(-7.0));
    CHECK(sk.PositionOf(copy->start).y == doctest::Approx(1.0));
    // The arc still turns counter-clockwise: its copy runs from the image of the end.
    const Arc* arcCopy = sk.FindArc(copies[4]);
    REQUIRE(arcCopy);
    const Arc* arcOriginal = sk.FindArc(arc);
    CHECK(sk.PositionOf(arcCopy->start).y == doctest::Approx(sk.PositionOf(arcOriginal->end).y));
    CHECK(sk.PositionOf(arcCopy->start).x == doctest::Approx(-sk.PositionOf(arcOriginal->end).x));

    CHECK(MirrorEntities(sk, ids, chosen, chosen[0]).empty());   // the axis cannot be mirrored over itself
}
