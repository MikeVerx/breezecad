#include "sketch/Solver.h"
#include "sketch/Trim.h"

#include <doctest.h>

#include <cmath>

using namespace bz;
using namespace bz::sketch;

namespace {

ObjectId AddSegment(Sketch& sk, IdGenerator& ids, Vec2 a, Vec2 b) {
    return sk.AddLine(ids, sk.AddPoint(ids, a), sk.AddPoint(ids, b));
}

double LengthOf(const Sketch& sk, const Line& l) { return (sk.PositionOf(l.end) - sk.PositionOf(l.start)).Length(); }

} // namespace

TEST_CASE("trim the middle of a line between two crossings") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId line = AddSegment(sk, ids, {0, 0}, {30, 0});
    AddSegment(sk, ids, {10, -5}, {10, 5});
    AddSegment(sk, ids, {20, -5}, {20, 5});

    const auto span = FindTrimSpan(sk, line, {15, 0.1});
    REQUIRE(span);
    REQUIRE(span->from);
    REQUIRE(span->to);
    CHECK(span->from->pos.x == doctest::Approx(10.0));
    CHECK(span->to->pos.x == doctest::Approx(20.0));
    REQUIRE(Trim(sk, ids, *span));
    CHECK_FALSE(sk.FindLine(line));
    REQUIRE(sk.lines.size() == 4);   // two cutters and two pieces
    double kept = 0;
    for (const Line& l : sk.lines)
        if (std::abs(sk.PositionOf(l.start).y) < 1e-9 && std::abs(sk.PositionOf(l.end).y) < 1e-9) kept += LengthOf(sk, l);
    CHECK(kept == doctest::Approx(20.0));
    CHECK(Solve(sk).converged);   // new ends stay on the cutters
}

TEST_CASE("trim an end of a line and a free line") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId line = AddSegment(sk, ids, {0, 0}, {30, 0});
    AddSegment(sk, ids, {10, -5}, {10, 5});
    REQUIRE(Trim(sk, ids, *FindTrimSpan(sk, line, {25, 0})));
    REQUIRE(sk.lines.size() == 2);
    const Line& piece = sk.lines.back();
    CHECK(LengthOf(sk, piece) == doctest::Approx(10.0));

    const ObjectId lonely = AddSegment(sk, ids, {0, 20}, {5, 20});
    REQUIRE(Trim(sk, ids, *FindTrimSpan(sk, lonely, {2, 20})));
    CHECK_FALSE(sk.FindLine(lonely));
}

TEST_CASE("trim a circle cut by a line into an arc") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId circle = sk.AddCircle(ids, sk.AddPoint(ids, {0, 0}), 10);
    AddSegment(sk, ids, {-20, 5}, {20, 5});
    const auto span = FindTrimSpan(sk, circle, {0, 10});   // the cap above the line
    REQUIRE(span);
    REQUIRE(Trim(sk, ids, *span));
    CHECK(sk.circles.empty());
    REQUIRE(sk.arcs.size() == 1);
    const Arc& arc = sk.arcs.front();
    // What is left is the big part below y = 5.
    const Vec2 s = sk.PositionOf(arc.start), e = sk.PositionOf(arc.end);
    CHECK(s.y == doctest::Approx(5.0));
    CHECK(e.y == doctest::Approx(5.0));
    // Counter-clockwise from the left crossing round the bottom: 240 degrees.
    CHECK(s.x < 0);
    CHECK(e.x > 0);
    CHECK(Solve(sk).converged);
}
