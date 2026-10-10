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

TEST_CASE("extend a line end to the nearest curve ahead") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId line = AddSegment(sk, ids, {0, 0}, {10, 0});
    const ObjectId near = AddSegment(sk, ids, {20, -5}, {20, 5});
    AddSegment(sk, ids, {30, -5}, {30, 5});
    AddSegment(sk, ids, {15, 10}, {15, 20});   // its carrier crosses, the segment does not

    const auto e = FindExtension(sk, line, {9, 0.2});
    REQUIRE(e);
    CHECK(e->target == near);
    CHECK((e->to - Vec2{20, 0}).Length() == doctest::Approx(0.0));
    Extend(sk, ids, *e);
    REQUIRE(Solve(sk).converged);
    CHECK(LengthOf(sk, *sk.FindLine(line)) == doctest::Approx(20.0));

    CHECK_FALSE(FindExtension(sk, line, {0.5, 0}));   // nothing behind the start
}

TEST_CASE("split a line and an arc where clicked") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId line = AddSegment(sk, ids, {0, 0}, {30, 0});
    CHECK_FALSE(Split(sk, ids, line, {0.1, 0}));   // at an end: nothing
    const auto p = Split(sk, ids, line, {10, 0.5});
    REQUIRE(p);
    CHECK(sk.lines.size() == 2);
    CHECK((sk.PositionOf(*p) - Vec2{10, 0}).Length() == doctest::Approx(0.0));
    REQUIRE(Solve(sk).converged);
    CHECK(LengthOf(sk, sk.lines[0]) + LengthOf(sk, sk.lines[1]) == doctest::Approx(30.0));

    const ObjectId c = sk.AddPoint(ids, {100, 0});
    const ObjectId arc = sk.AddArc(ids, c, sk.AddPoint(ids, {110, 0}), sk.AddPoint(ids, {90, 0}));   // the upper half
    const auto q = Split(sk, ids, arc, {100, 12});
    REQUIRE(q);
    CHECK(sk.arcs.size() == 2);
    CHECK((sk.PositionOf(*q) - Vec2{100, 10}).Length() == doctest::Approx(0.0));
    CHECK(Solve(sk).converged);
}

TEST_CASE("trim a spline and an ellipse; lines are cut by them and extend to them") {
    IdGenerator ids;
    Sketch sk;
    const ObjectId s0 = sk.AddPoint(ids, {0, 0}), s2 = sk.AddPoint(ids, {20, 0});
    const ObjectId spline = sk.AddSpline(ids, {s0, sk.AddPoint(ids, {10, 6}), s2}, true);
    const ObjectId cutter = sk.AddLine(ids, sk.AddPoint(ids, {10, -10}), sk.AddPoint(ids, {10, 20}));

    // The vertical line is cut where the spline crosses it.
    const std::vector<Cut> lineCuts = CutsOf(sk, cutter);
    REQUIRE(lineCuts.size() == 1);
    CHECK(lineCuts[0].by == spline);
    CHECK(lineCuts[0].pos.x == doctest::Approx(10.0).epsilon(1e-6));

    // Trimming the right half of the spline leaves a spline from its start to the line.
    const auto span = FindTrimSpan(sk, spline, {16, 4});
    REQUIRE(span);
    REQUIRE(span->from);
    CHECK_FALSE(span->to);
    REQUIRE(Trim(sk, ids, *span));
    REQUIRE(sk.splines.size() == 1);
    const Spline& left = sk.splines.front();
    CHECK(left.points.front() == s0);
    CHECK(sk.PositionOf(left.points.back()).x == doctest::Approx(10.0).epsilon(1e-6));

    // An ellipse cut twice by a line keeps the far side as one open spline.
    const ObjectId e = sk.AddEllipse(ids, sk.AddPoint(ids, {50, 0}), sk.AddPoint(ids, {60, 0}), sk.AddPoint(ids, {50, 5}));
    sk.AddLine(ids, sk.AddPoint(ids, {55, -10}), sk.AddPoint(ids, {55, 10}));
    const auto ellipseSpan = FindTrimSpan(sk, e, {59, 0});
    REQUIRE(ellipseSpan);
    REQUIRE(Trim(sk, ids, *ellipseSpan));
    CHECK(sk.ellipses.empty());
    REQUIRE(sk.splines.size() == 2);
    const Spline& rest = sk.splines.back();
    CHECK_FALSE(rest.Closed());
    CHECK(sk.PositionOf(rest.points.front()).x == doctest::Approx(55.0).epsilon(1e-6));
    CHECK(sk.PositionOf(rest.points.back()).x == doctest::Approx(55.0).epsilon(1e-6));

    // A line pointing at an ellipse extends to it.
    const ObjectId e2 = sk.AddEllipse(ids, sk.AddPoint(ids, {100, 0}), sk.AddPoint(ids, {110, 0}), sk.AddPoint(ids, {100, 5}));
    const ObjectId probe = sk.AddLine(ids, sk.AddPoint(ids, {70, 0}), sk.AddPoint(ids, {80, 0}));
    const auto ext = FindExtension(sk, probe, {79, 0});
    REQUIRE(ext);
    CHECK(ext->target == e2);
    CHECK(ext->to.x == doctest::Approx(90.0).epsilon(1e-4));
}
