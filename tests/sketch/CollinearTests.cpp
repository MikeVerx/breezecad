#include "document/SketchFeature.h"
#include "sketch/Solver.h"

#include <doctest.h>

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

/// Distance from `p` to the infinite line through `a` and `b`.
double OffLine(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 d = b - a;
    return std::abs(d.x * (p.y - a.y) - d.y * (p.x - a.x)) / std::sqrt(d.x * d.x + d.y * d.y);
}

} // namespace

// 0.2.5: «отрезки на одной прямой» (KOMPAS's «коллинеарность»): both ends
// of the second segment lie on the line of the first.
TEST_CASE("коллинеарность: два отдельных отрезка встают на одну прямую") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId a = sk.AddPoint(ids, {0, 0}), b = sk.AddPoint(ids, {10, 1});
    const ObjectId c = sk.AddPoint(ids, {20, 5}), d = sk.AddPoint(ids, {30, 4});
    const ObjectId first = sk.AddLine(ids, a, b), second = sk.AddLine(ids, c, d);
    sk.AddConstraint(ids, Geo(ConstraintType::Collinear, first, second));
    REQUIRE(Solve(sk).converged);
    const Vec2 pa = sk.PositionOf(a), pb = sk.PositionOf(b);
    CHECK(OffLine(sk.PositionOf(c), pa, pb) < 1e-7);
    CHECK(OffLine(sk.PositionOf(d), pa, pb) < 1e-7);
}

TEST_CASE("коллинеарность: отрезки с общим концом выпрямляются, и это не лишняя зависимость") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId a = sk.AddPoint(ids, {0, 0}), b = sk.AddPoint(ids, {10, 0}), c = sk.AddPoint(ids, {20, 3});
    const ObjectId first = sk.AddLine(ids, a, b), second = sk.AddLine(ids, b, c);
    const Constraint collinear = Geo(ConstraintType::Collinear, first, second);
    CHECK_FALSE(IsRedundant(sk, collinear));
    sk.AddConstraint(ids, collinear);
    REQUIRE(Solve(sk).converged);
    CHECK(OffLine(sk.PositionOf(c), sk.PositionOf(a), sk.PositionOf(b)) < 1e-7);
}

TEST_CASE("коллинеарность: лишняя, когда отрезки уже на одной прямой по другим зависимостям") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId a = sk.AddPoint(ids, {0, 0}), b = sk.AddPoint(ids, {10, 0}), c = sk.AddPoint(ids, {20, 0});
    const ObjectId first = sk.AddLine(ids, a, b), second = sk.AddLine(ids, b, c);
    sk.AddConstraint(ids, Geo(ConstraintType::Horizontal, first));
    sk.AddConstraint(ids, Geo(ConstraintType::Horizontal, second));
    REQUIRE(Solve(sk).converged);
    CHECK(IsRedundant(sk, Geo(ConstraintType::Collinear, first, second)));

    // Two horizontal segments apart may still be at different heights: then it is not.
    const ObjectId e = sk.AddPoint(ids, {30, 5}), f = sk.AddPoint(ids, {40, 5});
    const ObjectId third = sk.AddLine(ids, e, f);
    sk.AddConstraint(ids, Geo(ConstraintType::Horizontal, third));
    REQUIRE(Solve(sk).converged);
    const Constraint onOne = Geo(ConstraintType::Collinear, first, third);
    CHECK_FALSE(IsRedundant(sk, onOne));
    sk.AddConstraint(ids, onOne);
    REQUIRE(Solve(sk).converged);
    CHECK(std::abs(sk.PositionOf(e).y - sk.PositionOf(a).y) < 1e-7);
}

TEST_CASE("коллинеарность: перетаскивание конца ведёт оба отрезка вдоль одной прямой") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId a = sk.AddPoint(ids, {0, 0}), b = sk.AddPoint(ids, {10, 0});
    const ObjectId c = sk.AddPoint(ids, {20, 0}), d = sk.AddPoint(ids, {30, 0});
    const ObjectId first = sk.AddLine(ids, a, b), second = sk.AddLine(ids, c, d);
    sk.AddConstraint(ids, Geo(ConstraintType::Fix, a));
    sk.AddConstraint(ids, Geo(ConstraintType::Collinear, first, second));
    REQUIRE(Solve(sk).converged);
    REQUIRE(Solve(sk, {.drags = {Drag{b, {10, 10}}}}).converged);
    const Vec2 pa = sk.PositionOf(a), pb = sk.PositionOf(b);
    CHECK(OffLine(sk.PositionOf(c), pa, pb) < 1e-6);
    CHECK(OffLine(sk.PositionOf(d), pa, pb) < 1e-6);
}

TEST_CASE("коллинеарность сохраняется в файл под своим именем") {
    CHECK(doc::ConstraintTypeId(ConstraintType::Collinear) == "collinear");
    ConstraintType back = ConstraintType::Coincident;
    REQUIRE(doc::ConstraintTypeFromId("collinear", back));
    CHECK(back == ConstraintType::Collinear);
}
