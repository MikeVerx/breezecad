#include "sketch/Offset.h"
#include "sketch/Shapes.h"
#include "sketch/Solver.h"

#include <doctest.h>

#include <cmath>

using namespace bz;
using namespace bz::sketch;

namespace {

/// Box of the points of the given curves.
Box2 BoxOf(const Sketch& sk, const std::vector<ObjectId>& curves) {
    Box2 box;
    for (const ObjectId id : curves) {
        if (const Line* l = sk.FindLine(id)) {
            box.Include(sk.PositionOf(l->start));
            box.Include(sk.PositionOf(l->end));
        }
    }
    return box;
}

} // namespace

TEST_CASE("offset a rectangle outward and inward") {
    Sketch sk;
    IdGenerator ids;
    const RectangleIds r = AddCornerRectangle(sk, ids, sk.AddPoint(ids, {0, 0}), sk.AddPoint(ids, {40, 20}));
    const auto chain = ChainOf(sk, r.sides[0]);
    REQUIRE(chain);
    CHECK(chain->closed);
    CHECK(chain->pieces.size() == 4);

    const int outside = SideOf(sk, *chain, {50, 10});
    const auto out = OffsetChain(sk, ids, *chain, 5.0 * outside);
    REQUIRE(out);
    CHECK(out->curves.size() == 4);
    CHECK(out->distances.size() == 4);
    Box2 box = BoxOf(sk, out->curves);
    CHECK(box.min.x == doctest::Approx(-5));
    CHECK(box.max.y == doctest::Approx(25));
    REQUIRE(Solve(sk).converged);

    const auto in = OffsetChain(sk, ids, *chain, -5.0 * outside);
    REQUIRE(in);
    box = BoxOf(sk, in->curves);
    CHECK(box.min.x == doctest::Approx(5));
    CHECK(box.max.y == doctest::Approx(15));
    CHECK_FALSE(OffsetChain(sk, ids, *chain, -15.0 * outside));   // the short sides would turn around
}

TEST_CASE("offset a slot: lines and arcs keep meeting") {
    Sketch sk;
    IdGenerator ids;
    const SlotIds slot = AddStraightSlot(sk, ids, sk.AddPoint(ids, {0, 0}), sk.AddPoint(ids, {30, 0}), 10);
    const auto chain = ChainOf(sk, slot.curves[0]);
    REQUIRE(chain);
    CHECK(chain->closed);
    CHECK(chain->pieces.size() == 4);
    const int outside = SideOf(sk, *chain, {15, 20});
    const auto out = OffsetChain(sk, ids, *chain, 2.0 * outside);
    REQUIRE(out);
    for (const ObjectId id : out->curves)
        if (sk.FindArc(id)) CHECK(sk.RadiusOf(id) == doctest::Approx(7.0));
    CHECK(Solve(sk).converged);
}

TEST_CASE("offset a circle and an open chain") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId circle = sk.AddCircle(ids, sk.AddPoint(ids, {0, 0}), 10);
    const auto c = ChainOf(sk, circle);
    REQUIRE(c);
    const auto ring = OffsetChain(sk, ids, *c, -3.0 * SideOf(sk, *c, {0, 0}));
    REQUIRE(ring);
    CHECK(sk.RadiusOf(ring->curves[0]) == doctest::Approx(13.0));

    const ObjectId a = sk.AddPoint(ids, {100, 0}), b = sk.AddPoint(ids, {120, 0}), d = sk.AddPoint(ids, {120, 20});
    sk.AddLine(ids, a, b);
    const ObjectId second = sk.AddLine(ids, b, d);
    const auto open = ChainOf(sk, second);
    REQUIRE(open);
    CHECK_FALSE(open->closed);
    CHECK(open->pieces.size() == 2);
    const auto off = OffsetChain(sk, ids, *open, 2.0 * SideOf(sk, *open, {110, 10}));
    REQUIRE(off);
    const Line* first = sk.FindLine(off->curves[0]);
    CHECK((sk.PositionOf(first->end) - Vec2{118, 2}).Length() == doctest::Approx(0.0));   // the inner corner
}
