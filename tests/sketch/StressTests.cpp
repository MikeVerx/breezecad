// Hard tests of sketch building (patch 0.2.4.1): every shape tool combined
// with fillets, trims, offsets, mirrors, copies and size changes. After each
// step the sketch must be whole (no dangling ids, no NaN), solvable, and a
// solve of a solved sketch must move nothing. What Inventor does is the
// reference where the owner did not say otherwise.

#include "sketch/Clipboard.h"
#include "sketch/Fillet.h"
#include "sketch/Mirror.h"
#include "sketch/Move.h"
#include "sketch/Offset.h"
#include "sketch/Shapes.h"
#include "sketch/Solver.h"
#include "sketch/Trim.h"

#include <doctest.h>

#include <cmath>
#include <cstdlib>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

using namespace bz;
using namespace bz::sketch;

namespace {

Constraint Make(ConstraintType type, ObjectId a, ObjectId b = {}, ObjectId c = {}) {
    Constraint k;
    k.type = type;
    k.refs = {a, b, c};
    return k;
}

Constraint Dim(ConstraintType type, double value, ObjectId a, ObjectId b = {}) {
    Constraint k = Make(type, a, b);
    k.parameter.value = value;
    k.parameter.expression = std::to_string(value);
    return k;
}

/// Everything a sketch refers to exists, every position is a number, every
/// id is used once, and arcs keep their ends on their circle.
void CheckWhole(const Sketch& sk) {
    std::unordered_set<ObjectId> ids;
    const auto unique = [&](ObjectId id) {
        CHECK(id);
        CHECK(ids.insert(id).second);
    };
    for (const Point& p : sk.points) {
        unique(p.id);
        CHECK(std::isfinite(p.pos.x));
        CHECK(std::isfinite(p.pos.y));
    }
    const auto point = [&](ObjectId id) { CHECK(sk.FindPoint(id) != nullptr); };
    for (const Line& l : sk.lines) {
        unique(l.id);
        point(l.start);
        point(l.end);
        CHECK(l.start != l.end);
        CHECK((sk.PositionOf(l.end) - sk.PositionOf(l.start)).Length() > 1e-6);   // nothing collapsed
    }
    for (const Circle& c : sk.circles) {
        unique(c.id);
        point(c.center);
        CHECK(std::isfinite(c.radius));
        CHECK(c.radius > 0);
    }
    for (const Arc& a : sk.arcs) {
        unique(a.id);
        point(a.center);
        point(a.start);
        point(a.end);
        const Vec2 c = sk.PositionOf(a.center);
        CHECK((sk.PositionOf(a.start) - c).Length() == doctest::Approx((sk.PositionOf(a.end) - c).Length()).epsilon(1e-6));
    }
    for (const Constraint& k : sk.constraints) {
        unique(k.id);
        for (const ObjectId r : k.refs)
            if (r) CHECK(sk.KindOf(r) != EntityKind::None);
        if (IsDimensional(k.type)) CHECK(std::isfinite(k.parameter.value));
    }
}

/// The sketch as text, to see what a failed random step started from.
std::string Describe(const Sketch& sk) {
    std::string s;
    const auto id = [](ObjectId i) { return std::to_string(i.value); };
    for (const Point& p : sk.points)
        s += "P" + id(p.id) + " (" + std::to_string(p.pos.x) + ", " + std::to_string(p.pos.y) + ")" + (p.isOrigin ? " origin" : "") + "\n";
    for (const Line& l : sk.lines) s += "L" + id(l.id) + " " + id(l.start) + "-" + id(l.end) + (l.construction ? " helper" : "") + "\n";
    for (const Circle& c : sk.circles) s += "C" + id(c.id) + " c" + id(c.center) + " r" + std::to_string(c.radius) + "\n";
    for (const Arc& a : sk.arcs) s += "A" + id(a.id) + " c" + id(a.center) + " " + id(a.start) + "->" + id(a.end) + "\n";
    for (const Constraint& k : sk.constraints)
        s += "K" + id(k.id) + " type" + std::to_string(static_cast<int>(k.type)) + " " + id(k.refs[0]) + "," + id(k.refs[1]) + "," + id(k.refs[2]) +
             (IsDimensional(k.type) ? " = " + std::to_string(k.parameter.value) : std::string()) + "\n";
    return s;
}

std::vector<std::pair<ObjectId, Vec2>> Positions(const Sketch& sk) {
    std::vector<std::pair<ObjectId, Vec2>> out;
    for (const Point& p : sk.points) out.push_back({p.id, p.pos});
    return out;
}

/// Solves; then a second solve must not move a thing (a stable sketch).
void SolveStable(Sketch& sk) {
    REQUIRE(Solve(sk).converged);
    const auto before = Positions(sk);
    REQUIRE(Solve(sk).converged);
    for (const auto& [id, pos] : before) CHECK((sk.PositionOf(id) - pos).Length() < 1e-7);
}

/// Points that did not move, of those listed.
void CheckUnmoved(const Sketch& sk, const std::vector<std::pair<ObjectId, Vec2>>& before) {
    for (const auto& [id, pos] : before) {
        if (!sk.FindPoint(id)) continue;
        CHECK((sk.PositionOf(id) - pos).Length() < 1e-6);
    }
}

/// The outline corners of a shape: points where exactly two outline lines meet.
std::vector<ObjectId> OutlineCorners(const Sketch& sk) {
    std::vector<ObjectId> out;
    for (const Point& p : sk.points)
        if (CornerLines(sk, p.id)) out.push_back(p.id);
    return out;
}

/// Every shape the rectangle tool makes, built around (0, 0).
enum class Shape { Corners, Centre, ThreePoints, ThreePointsCentre, Triangle, Hexagon, Octagon };
constexpr Shape kShapes[] = {Shape::Corners, Shape::Centre, Shape::ThreePoints, Shape::ThreePointsCentre,
                             Shape::Triangle, Shape::Hexagon, Shape::Octagon};

void Build(Sketch& sk, IdGenerator& ids, Shape shape) {
    switch (shape) {
        case Shape::Corners: AddCornerRectangle(sk, ids, sk.AddPoint(ids, {-30, -20}), sk.AddPoint(ids, {30, 20})); break;
        case Shape::Centre: AddCentredRectangle(sk, ids, sk.AddPoint(ids, {0, 0}), {-30, 20}); break;
        case Shape::ThreePoints:
            REQUIRE(AddThreePointRectangle(sk, ids, sk.AddPoint(ids, {-20, -25}), sk.AddPoint(ids, {25, -5}), {10, 30}));
            break;
        case Shape::ThreePointsCentre:
            REQUIRE(AddThreePointCentredRectangle(sk, ids, sk.AddPoint(ids, {0, 0}), {25, 10}, {-5, 20}));
            break;
        case Shape::Triangle: AddPolygon(sk, ids, sk.AddPoint(ids, {0, 0}), {0, 30}, 3, true); break;
        case Shape::Hexagon: AddPolygon(sk, ids, sk.AddPoint(ids, {0, 0}), {30, 0}, 6, false); break;
        case Shape::Octagon: AddPolygon(sk, ids, sk.AddPoint(ids, {0, 0}), {30, 5}, 8, true); break;
    }
}

const char* NameOf(Shape s) {
    switch (s) {
        case Shape::Corners: return "rectangle by corners";
        case Shape::Centre: return "rectangle from the centre";
        case Shape::ThreePoints: return "rectangle by three points";
        case Shape::ThreePointsCentre: return "rectangle by three points from the centre";
        case Shape::Triangle: return "triangle";
        case Shape::Hexagon: return "hexagon";
        case Shape::Octagon: return "octagon";
    }
    return "";
}

} // namespace

TEST_CASE("stress: every shape, every corner rounded, nothing moves") {
    for (const Shape shape : kShapes) {
        CAPTURE(NameOf(shape));
        Sketch sk;
        IdGenerator ids;
        sk.EnsureOrigin(ids);
        Build(sk, ids, shape);
        SolveStable(sk);
        CheckWhole(sk);
        const std::vector<ObjectId> corners = OutlineCorners(sk);
        const std::size_t sides = shape == Shape::Triangle ? 3 : shape == Shape::Hexagon ? 6 : shape == Shape::Octagon ? 8 : 4;
        REQUIRE(corners.size() == sides);
        const auto before = Positions(sk);
        for (const ObjectId corner : corners) {
            CAPTURE(corner.value);
            const auto arc = FilletCorner(sk, ids, corner, 3.0);
            REQUIRE(arc);
            sk.AddConstraint(ids, Dim(ConstraintType::Radius, 3.0, *arc));
            SolveStable(sk);
            CheckWhole(sk);
            CheckUnmoved(sk, before);
            CHECK(sk.FindPoint(corner));   // the virtual sharp stays
        }
        CHECK(sk.arcs.size() == sides);
        // The rounded outline is one closed chain, helper lines or not.
        const auto chain = ChainOf(sk, sk.arcs.front().id);
        REQUIRE(chain);
        CHECK(chain->closed);
        CHECK(chain->pieces.size() == 2 * sides);
        // …and offsets as a whole.
        const auto offset = OffsetChain(sk, ids, *chain, SideOf(sk, *chain, {0, 0}) * 2.0);
        REQUIRE(offset);
        CHECK(offset->curves.size() == 2 * sides);
        SolveStable(sk);
        CheckWhole(sk);
    }
}

TEST_CASE("stress: a centred rectangle offsets through its diagonals' corners") {
    Sketch sk;
    IdGenerator ids;
    const RectangleIds r = AddCentredRectangle(sk, ids, sk.AddPoint(ids, {0, 0}), {-20, 10});
    const auto chain = ChainOf(sk, r.sides[0]);
    REQUIRE(chain);
    CHECK(chain->closed);
    CHECK(chain->pieces.size() == 4);
    REQUIRE(OffsetChain(sk, ids, *chain, 3.0));
    SolveStable(sk);
    CheckWhole(sk);
    // A diagonal on its own is not a chain of the outline.
    for (const Line& l : sk.lines)
        if (l.construction) {
            const auto helper = ChainOf(sk, l.id);
            REQUIRE(helper);
            CHECK(helper->pieces.size() == 1);
        }
}

TEST_CASE("stress: fully defined rectangle, fillets, then every size changed") {
    for (const Shape shape : {Shape::Corners, Shape::Centre}) {
        CAPTURE(NameOf(shape));
        Sketch sk;
        IdGenerator ids;
        const ObjectId origin = sk.EnsureOrigin(ids);
        RectangleIds r{};
        ObjectId centre;
        if (shape == Shape::Corners) {
            r = AddCornerRectangle(sk, ids, sk.AddPoint(ids, {5, 5}), sk.AddPoint(ids, {45, 35}));
            sk.AddConstraint(ids, Dim(ConstraintType::DistanceX, 5, origin, r.corners[0]));
            sk.AddConstraint(ids, Dim(ConstraintType::DistanceY, 5, origin, r.corners[0]));
        } else {
            centre = sk.AddPoint(ids, {0, 0});
            r = AddCentredRectangle(sk, ids, centre, {-20, 15});
            sk.AddConstraint(ids, Make(ConstraintType::Coincident, centre, origin));
        }
        const ObjectId width = sk.AddConstraint(ids, Dim(ConstraintType::Length, 40, r.sides[0]));
        const ObjectId height = sk.AddConstraint(ids, Dim(ConstraintType::Length, 30, r.sides[1]));
        SolveStable(sk);
        CHECK(Analyze(sk).degreesOfFreedom == 0);
        std::vector<ObjectId> radii;
        for (const ObjectId corner : r.corners) {
            const auto arc = FilletCorner(sk, ids, corner, 4.0);
            REQUIRE(arc);
            radii.push_back(sk.AddConstraint(ids, Dim(ConstraintType::Radius, 4.0, *arc)));
            SolveStable(sk);
        }
        CHECK(Analyze(sk).degreesOfFreedom == 0);   // still fully defined
        CheckWhole(sk);
        // Sizes change: the shape follows, the fillets stay tangent and whole.
        sk.FindConstraint(width)->parameter.value = 70;
        sk.FindConstraint(height)->parameter.value = 50;
        for (const ObjectId id : radii) sk.FindConstraint(id)->parameter.value = 9;
        SolveStable(sk);
        CheckWhole(sk);
        const Vec2 a = sk.PositionOf(r.corners[0]), c = sk.PositionOf(r.corners[2]);
        CHECK(std::abs(c.x - a.x) == doctest::Approx(70).epsilon(1e-6));
        CHECK(std::abs(c.y - a.y) == doctest::Approx(50).epsilon(1e-6));
        for (const Arc& arc : sk.arcs) CHECK(sk.RadiusOf(arc.id) == doctest::Approx(9).epsilon(1e-6));
        if (shape == Shape::Centre) CHECK((sk.PositionOf(centre) - (a + c) * 0.5).Length() < 1e-6);
    }
}

TEST_CASE("stress: fillet, then trim, mirror, move, copy and paste stay whole") {
    Sketch sk;
    IdGenerator ids;
    sk.EnsureOrigin(ids);
    const RectangleIds r = AddCentredRectangle(sk, ids, sk.AddPoint(ids, {20, 0}), {0, 10});
    REQUIRE(FilletCorner(sk, ids, r.corners[0], 3.0));
    REQUIRE(FilletCorner(sk, ids, r.corners[2], 3.0));
    SolveStable(sk);

    // A line across the shape, trimmed to the outline on one side.
    const ObjectId cross = sk.AddLine(ids, sk.AddPoint(ids, {20, -20}), sk.AddPoint(ids, {20, 20}));
    const auto span = FindTrimSpan(sk, cross, {20, 18});
    REQUIRE(span);
    REQUIRE(Trim(sk, ids, *span));
    SolveStable(sk);
    CheckWhole(sk);

    // Mirror everything across a vertical axis at x = -5.
    const ObjectId axis = sk.AddLine(ids, sk.AddPoint(ids, {-5, -30}), sk.AddPoint(ids, {-5, 30}), true);
    std::vector<ObjectId> all;
    for (const Line& l : sk.lines)
        if (l.id != axis) all.push_back(l.id);
    for (const Arc& a : sk.arcs) all.push_back(a.id);
    const auto mirrored = MirrorEntities(sk, ids, all, axis);
    CHECK_FALSE(mirrored.empty());
    SolveStable(sk);
    CheckWhole(sk);

    // Move the original half, then copy it, then paste it twice.
    REQUIRE(MoveEntities(sk, ids, all, Motion{{}, 0.0, {0, 15}}));
    SolveStable(sk);
    CheckWhole(sk);
    const auto copies = CopyEntities(sk, ids, all, Motion{{20, 0}, kPi / 6, {}});
    CHECK_FALSE(copies.empty());
    SolveStable(sk);
    CheckWhole(sk);
    const Clipboard clip = CopyToClipboard(sk, all, {20, 15});
    REQUIRE_FALSE(clip.Empty());
    int n = 0;
    const auto name = [&] { return "p" + std::to_string(++n); };
    for (const Vec2 at : {Vec2{100, 0}, Vec2{-100, 40}}) {
        const auto pasted = PasteFromClipboard(sk, ids, clip, at, name);
        CHECK_FALSE(pasted.empty());
        SolveStable(sk);
        CheckWhole(sk);
    }
}

TEST_CASE("stress: removing pieces of a rounded shape leaves no dangling references") {
    for (int victim = 0; victim < 6; ++victim) {
        CAPTURE(victim);
        Sketch sk;
        IdGenerator ids;
        const RectangleIds r = AddCentredRectangle(sk, ids, sk.AddPoint(ids, {0, 0}), {-20, 10});
        const auto arc = FilletCorner(sk, ids, r.corners[1], 4.0);
        REQUIRE(arc);
        sk.AddConstraint(ids, Dim(ConstraintType::Radius, 4.0, *arc));
        sk.AddConstraint(ids, Dim(ConstraintType::Length, 40.0, r.sides[0]));   // becomes a distance to the corner? no: side 0 ends at corner 1
        SolveStable(sk);
        const ObjectId targets[] = {*arc, r.sides[0], r.sides[1], r.corners[1], sk.arcs.front().center, r.sides[2]};
        sk.Remove(targets[victim]);
        CheckWhole(sk);
        REQUIRE(Solve(sk).converged);
    }
}

TEST_CASE("stress: arcs dimensioned by diameter and sweep, chained with tangent lines") {
    Sketch sk;
    IdGenerator ids;
    const ObjectId origin = sk.EnsureOrigin(ids);
    // A hook: a line, a tangent arc of 200°, another tangent line.
    const ObjectId p0 = sk.AddPoint(ids, {-30, 0}), p1 = sk.AddPoint(ids, {0, 0});
    const ObjectId l1 = sk.AddLine(ids, p0, p1);
    const ObjectId c = sk.AddPoint(ids, {0, 10});
    const ObjectId e = sk.AddPoint(ids, {-3.4, 19.4});
    const ObjectId arc = sk.AddArc(ids, c, p1, e);
    const ObjectId p2 = sk.AddPoint(ids, {-20, 25});
    const ObjectId l2 = sk.AddLine(ids, e, p2);
    sk.AddConstraint(ids, Make(ConstraintType::Tangent, l1, arc));
    sk.AddConstraint(ids, Make(ConstraintType::Tangent, l2, arc));
    sk.AddConstraint(ids, Make(ConstraintType::Horizontal, l1));
    sk.AddConstraint(ids, Make(ConstraintType::Coincident, p1, origin));
    sk.AddConstraint(ids, Dim(ConstraintType::Length, 30, l1));
    sk.AddConstraint(ids, Dim(ConstraintType::Length, 20, l2));
    const ObjectId d = sk.AddConstraint(ids, Dim(ConstraintType::Diameter, 20, arc));
    const ObjectId sweep = sk.AddConstraint(ids, Dim(ConstraintType::ArcAngle, 200 * kPi / 180, arc));
    SolveStable(sk);
    CHECK(Analyze(sk).degreesOfFreedom == 0);
    for (const double degrees : {90.0, 179.0, 181.0, 300.0}) {
        CAPTURE(degrees);
        sk.FindConstraint(sweep)->parameter.value = degrees * kPi / 180;
        sk.FindConstraint(d)->parameter.value = 10 + degrees / 30;
        SolveStable(sk);
        CheckWhole(sk);
        const Vec2 cc = sk.PositionOf(c), u = sk.PositionOf(p1) - cc, v = sk.PositionOf(e) - cc;
        double s = std::atan2(u.Cross(v), u.Dot(v));
        if (s <= 0) s += 2 * kPi;
        CHECK(s * 180 / kPi == doctest::Approx(degrees).epsilon(1e-6));
    }
}

TEST_CASE("stress: random sequences of operations keep the sketch whole and solvable") {
    // BZ_SEED=<n> runs another series (the default one is what CI checks).
    std::mt19937 rng(std::getenv("BZ_SEED") ? static_cast<std::mt19937::result_type>(std::strtoul(std::getenv("BZ_SEED"), nullptr, 10)) : 20261025u);
    std::uniform_real_distribution<double> coord(-50, 50);
    std::uniform_int_distribution<std::size_t> pick(0, std::size_t{1} << 20);
    for (int round = 0; round < 300; ++round) {
        CAPTURE(round);
        Sketch sk;
        IdGenerator ids;
        sk.EnsureOrigin(ids);
        for (int step = 0; step < 20; ++step) {
            CAPTURE(step);
            const int op = static_cast<int>(pick(rng) % 9);
            CAPTURE(op);
            const Sketch start = sk;
            switch (op) {
                case 0: Build(sk, ids, kShapes[pick(rng) % std::size(kShapes)]); break;
                case 1: {
                    const auto corners = OutlineCorners(sk);
                    if (!corners.empty()) FilletCorner(sk, ids, corners[pick(rng) % corners.size()], 1.0 + static_cast<double>(pick(rng) % 4));
                    break;
                }
                case 2: sk.AddLine(ids, sk.AddPoint(ids, {coord(rng), coord(rng)}), sk.AddPoint(ids, {coord(rng), coord(rng)})); break;
                case 3:
                    if (!sk.lines.empty()) {
                        const Line& l = sk.lines[pick(rng) % sk.lines.size()];
                        const Vec2 mid = (sk.PositionOf(l.start) + sk.PositionOf(l.end)) * 0.5;
                        if (const auto span = FindTrimSpan(sk, l.id, mid)) Trim(sk, ids, *span);
                    }
                    break;
                case 4:
                    if (!sk.lines.empty() || !sk.arcs.empty()) {
                        const bool arc = sk.lines.empty() || (!sk.arcs.empty() && pick(rng) % 2 == 0);
                        const ObjectId curve = arc ? sk.arcs[pick(rng) % sk.arcs.size()].id : sk.lines[pick(rng) % sk.lines.size()].id;
                        if (sk.KindOf(curve) != EntityKind::None)
                            if (const auto chain = ChainOf(sk, curve)) OffsetChain(sk, ids, *chain, 1.0 + static_cast<double>(pick(rng) % 3));
                    }
                    break;
                case 5: {
                    std::vector<ObjectId> all;
                    for (const Line& l : sk.lines) all.push_back(l.id);
                    for (const Arc& a : sk.arcs) all.push_back(a.id);
                    if (!all.empty()) MoveEntities(sk, ids, all, Motion{{0, 0}, static_cast<double>(pick(rng) % 7) * 0.3, {coord(rng) / 5, coord(rng) / 5}});
                    break;
                }
                case 6:
                    if (!sk.lines.empty()) {
                        const ObjectId id = sk.lines[pick(rng) % sk.lines.size()].id;
                        sk.Remove(id);
                    }
                    break;
                case 7:
                    if (!sk.lines.empty()) {
                        const Line& l = sk.lines[pick(rng) % sk.lines.size()];
                        const Vec2 at = (sk.PositionOf(l.start) * 0.3 + sk.PositionOf(l.end) * 0.7);
                        Split(sk, ids, l.id, at);
                    }
                    break;
                case 8: {
                    std::vector<ObjectId> all;
                    for (const Line& l : sk.lines) all.push_back(l.id);
                    if (!all.empty()) {
                        const ObjectId axis = sk.AddLine(ids, sk.AddPoint(ids, {60, -10}), sk.AddPoint(ids, {60, 10}), true);
                        MirrorEntities(sk, ids, all, axis);
                    }
                    break;
                }
            }
            {
                bool collapsed = false;
                for (const Line& l : sk.lines) collapsed = collapsed || (sk.PositionOf(l.end) - sk.PositionOf(l.start)).Length() < 1e-6;
                if (collapsed) MESSAGE("collapsed; before:\n" << Describe(start) << "after:\n" << Describe(sk));
                REQUIRE_FALSE(collapsed);
            }
            CheckWhole(sk);
            // Built by the tools, never contradictory: always solvable, and
            // a second solve moves nothing.
            const bool solved = Solve(sk).converged;
            if (!solved) MESSAGE("before:\n" << Describe(start) << "after:\n" << Describe(sk));
            REQUIRE(solved);
            {
                bool collapsed = false;
                for (const Line& l : sk.lines) collapsed = collapsed || (sk.PositionOf(l.end) - sk.PositionOf(l.start)).Length() < 1e-6;
                if (collapsed) MESSAGE("collapsed by the solver; before:\n" << Describe(start));
                REQUIRE_FALSE(collapsed);
            }
            const auto before = Positions(sk);
            REQUIRE(Solve(sk).converged);
            for (const auto& [id, pos] : before) CHECK((sk.PositionOf(id) - pos).Length() < 1e-6);
        }
    }
}
