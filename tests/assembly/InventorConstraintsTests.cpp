// Constraints at Inventor's level: the assembly origin as a target, mates
// between any of plane / line / point, symmetry, ranges, transmissions
// between joints, and the solver's speed on large assemblies.
#include "assembly/AssemblyFile.h"
#include "assembly/Solver.h"
#include "document/Origin.h"

#include <doctest.h>

#include <chrono>
#include <cmath>

using namespace bz;
using namespace bz::assembly;
using doctest::Approx;

namespace {

GeometryRef Plane(ObjectId c, Vec3 point, Vec3 normal) { return {c, GeometryKind::Plane, point, normal.Normalized(), 0, point}; }
GeometryRef Circle(ObjectId c, Vec3 centre, Vec3 axis, double r) { return {c, GeometryKind::Circle, centre, axis.Normalized(), r, centre}; }
GeometryRef Line(ObjectId c, Vec3 point, Vec3 dir) {
    GeometryRef g{c, GeometryKind::Axis, point, dir.Normalized(), 0, point};
    g.source = PickSource::Edge;
    return g;
}
GeometryRef Point(ObjectId c, Vec3 p) {
    GeometryRef g{c, GeometryKind::Point, p, {}, 0, p};
    g.source = PickSource::Vertex;
    return g;
}
GeometryRef Origin(ObjectId element) { return *OriginGeometry(kAssemblyOrigin, element); }

Constraint Make(ConstraintType type, GeometryRef a, GeometryRef b, double value = 0, bool flipped = false) {
    Constraint c;
    c.type = type;
    c.first = a;
    c.second = b;
    c.value = value;
    c.flipped = flipped;
    return c;
}

ObjectId AddJoint(Assembly& a, ConstraintType type, GeometryRef first, GeometryRef second) {
    Constraint c = Make(type, first, second);
    FitJoint(a, c);
    return a.AddConstraint(c).id;
}

/// A free part somewhere, a little turned, in an assembly without anything grounded.
struct Free {
    Assembly a;
    ObjectId part;
    Free() {
        a.AddComponent("ground.bzc", "Земля");   // first component: grounded, ties nothing
        part = a.AddComponent("part.bzc", "Деталь", {Rotation::AxisAngle(Vec3{1, 2, 0}.Normalized(), 0.4), {7, -3, 25}}).id;
    }
    Placement& P() { return a.FindComponent(part)->placement; }
};

} // namespace

TEST_CASE("origin geometry: planes, axes and the centre as doc::origin names them") {
    namespace o = doc::origin;
    const GeometryRef xy = Origin(o::kPlaneXY), xz = Origin(o::kPlaneXZ), yz = Origin(o::kPlaneYZ);
    CHECK(xy.kind == GeometryKind::Plane);
    CHECK(xy.direction.z == Approx(1));
    CHECK(xz.direction.y == Approx(-1));
    CHECK(yz.direction.x == Approx(1));
    CHECK(Origin(o::kAxisY).kind == GeometryKind::Axis);
    CHECK(Origin(o::kAxisY).direction.y == Approx(1));
    CHECK(Origin(o::kPoint).kind == GeometryKind::Point);
    CHECK(Origin(o::kPlaneXY).source == PickSource::Element);
    CHECK_FALSE(OriginGeometry(kAssemblyOrigin, ObjectId{42}).has_value());
    Assembly a;
    CHECK(a.Exists(kAssemblyOrigin));
    CHECK(a.IsFixed(kAssemblyOrigin));
    CHECK(a.PlacementOf(kAssemblyOrigin).IsIdentity());
}

TEST_CASE("a part constrained to the assembly origin: three planes fix it") {
    namespace o = doc::origin;
    Free t;
    // The part's own XY/XZ/YZ flush with the assembly's, 10 mm up.
    t.a.AddConstraint(Make(ConstraintType::Mate, Origin(o::kPlaneXY), *OriginGeometry(t.part, o::kPlaneXY), 10, true));
    t.a.AddConstraint(Make(ConstraintType::Mate, Origin(o::kPlaneXZ), *OriginGeometry(t.part, o::kPlaneXZ), 0, true));
    t.a.AddConstraint(Make(ConstraintType::Mate, Origin(o::kPlaneYZ), *OriginGeometry(t.part, o::kPlaneYZ), 0, true));
    const SolveResult r = Solve(t.a);
    CHECK(r.converged);
    CHECK(t.P().translation.x == Approx(0).epsilon(1e-6));
    CHECK(t.P().translation.y == Approx(0).epsilon(1e-6));
    CHECK(t.P().translation.z == Approx(10).epsilon(1e-6));
    CHECK(t.P().Direction({1, 0, 0}).x == Approx(1).epsilon(1e-6));
    CHECK(AnalyzeFreedom(t.a).at(t.part).Total() == 0);
    for (const Constraint& c : t.a.constraints) CHECK(c.state == ConstraintState::Satisfied);
}

TEST_CASE("mate between lines and points: on a line, in a plane, at a point") {
    Free t;
    // A vertex of the part onto the assembly's Z axis: it may still slide along it and turn.
    t.a.AddConstraint(Make(ConstraintType::Mate, Origin(doc::origin::kAxisZ), Point(t.part, {5, 5, 0})));
    REQUIRE(Solve(t.a).converged);
    Vec3 v = t.P().Point({5, 5, 0});
    CHECK(v.x == Approx(0).epsilon(1e-6));
    CHECK(v.y == Approx(0).epsilon(1e-6));
    Freedom f = AnalyzeFreedom(t.a).at(t.part);
    CHECK(f.translations == 1);
    CHECK(f.rotations == 3);

    Free u;   // an edge of the part lies in the assembly's XY plane, 3 mm above it
    u.a.AddConstraint(Make(ConstraintType::Mate, Origin(doc::origin::kPlaneXY), Line(u.part, {0, 0, 0}, {1, 0, 0}), 3));
    REQUIRE(Solve(u.a).converged);
    CHECK(u.P().Point({0, 0, 0}).z == Approx(3).epsilon(1e-6));
    CHECK(u.P().Point({50, 0, 0}).z == Approx(3).epsilon(1e-6));

    Free w;   // two vertices together
    w.a.AddConstraint(Make(ConstraintType::Mate, Origin(doc::origin::kPoint), Point(w.part, {1, 2, 3})));
    REQUIRE(Solve(w.a).converged);
    CHECK(w.P().Point({1, 2, 3}).Length() == Approx(0).epsilon(1e-6));
    f = AnalyzeFreedom(w.a).at(w.part);
    CHECK(f.translations == 0);
    CHECK(f.rotations == 3);

    Free z;   // two edges on one line
    z.a.AddConstraint(Make(ConstraintType::Mate, Origin(doc::origin::kAxisX), Line(z.part, {0, 4, 0}, {0, 0, 1})));
    REQUIRE(Solve(z.a).converged);
    const Vec3 p = z.P().Point({0, 4, 0}), d = z.P().Direction({0, 0, 1});
    CHECK(p.y == Approx(0).epsilon(1e-6));
    CHECK(p.z == Approx(0).epsilon(1e-6));
    CHECK(std::abs(d.x) == Approx(1).epsilon(1e-6));
}

TEST_CASE("symmetry: two parts mirrored in a plane") {
    namespace o = doc::origin;
    Assembly a;
    const ObjectId left = a.AddComponent("l.bzc", "Левая", {Rotation{}, {-30, 0, 0}}).id;   // grounded
    const ObjectId right = a.AddComponent("r.bzc", "Правая", {Rotation{}, {12, 5, 2}}).id;
    // Their outer faces: left's at x -10 facing -X (in its coordinates), right's at x +10 facing +X.
    Constraint s = Make(ConstraintType::Symmetry, Plane(left, {-10, 0, 0}, {-1, 0, 0}), Plane(right, {10, 0, 0}, {1, 0, 0}));
    s.third = Origin(o::kPlaneYZ);
    a.AddConstraint(s);
    REQUIRE(Solve(a).converged);
    // The left face is at x = -40, so the right one at x = +40: the part at 30.
    CHECK(a.FindComponent(right)->placement.translation.x == Approx(30).epsilon(1e-6));
    CHECK(a.constraints[0].state == ConstraintState::Satisfied);

    // Points: a vertex mirrored.
    Assembly b;
    const ObjectId p = b.AddComponent("p.bzc", "P", {Rotation{}, {0, 0, 0}}).id;
    const ObjectId q = b.AddComponent("q.bzc", "Q", {Rotation::AxisAngle({0, 0, 1}, 0.3), {9, 9, 9}}).id;
    Constraint m = Make(ConstraintType::Symmetry, Point(p, {4, 2, 1}), Point(q, {0, 0, 0}));
    m.third = Origin(o::kPlaneXZ);   // normal -Y through the origin
    b.AddConstraint(m);
    REQUIRE(Solve(b).converged);
    const Vec3 v = b.FindComponent(q)->placement.Point({0, 0, 0});
    CHECK(v.x == Approx(4).epsilon(1e-6));
    CHECK(v.y == Approx(-2).epsilon(1e-6));
    CHECK(v.z == Approx(1).epsilon(1e-6));
}

TEST_CASE("mate and angle with a range instead of a value") {
    Free t;
    Constraint c = Make(ConstraintType::Mate, Origin(doc::origin::kPlaneXY), *OriginGeometry(t.part, doc::origin::kPlaneXY));
    c.flipped = true;
    c.limits = {true, 5, 15};
    const ObjectId id = t.a.AddConstraint(c).id;
    // From 25 mm up it comes down to the top of the range, not to one value.
    REQUIRE(Solve(t.a).converged);
    CHECK(t.P().Point({0, 0, 0}).z == Approx(15).epsilon(1e-5));
    // Inside the range it stays where it is put, and the gap is no freedom taken.
    t.P().translation.z = 8;
    t.P().rotation = Rotation{};
    t.a.Touch();
    CHECK_FALSE(Solve(t.a).moved);
    CHECK(t.a.FindConstraint(id)->state == ConstraintState::Satisfied);
    CHECK(AnalyzeFreedom(t.a).at(t.part).translations == 3);

    Free u;   // the part's side face 20..40° from the assembly's XZ plane
    Constraint g = Make(ConstraintType::Angle, Origin(doc::origin::kPlaneXZ), Plane(u.part, {0, 0, 0}, {0, -1, 0}));
    g.limits = {true, 20 * kPi / 180, 40 * kPi / 180};
    u.P() = {Rotation::AxisAngle({0, 0, 1}, 70 * kPi / 180), {}};
    u.a.AddConstraint(g);
    REQUIRE(Solve(u.a).converged);
    const double angle = std::acos(u.P().Direction({0, -1, 0}).Dot({0, -1, 0}));
    CHECK(angle * 180 / kPi == Approx(40).epsilon(1e-4));
}

TEST_CASE("gear: the driven joint turns ratio times the drive, the other way") {
    Assembly a;
    const ObjectId housing = a.AddComponent("h.bzc", "Корпус").id;   // grounded
    const ObjectId pinion = a.AddComponent("p.bzc", "Шестерня", {Rotation{}, {0, 0, 0}}).id;
    const ObjectId wheel = a.AddComponent("w.bzc", "Колесо", {Rotation{}, {30, 0, 0}}).id;
    const ObjectId j1 = AddJoint(a, ConstraintType::Rotational, Circle(housing, {0, 0, 0}, {0, 0, 1}, 5),
                                 Circle(pinion, {0, 0, 0}, {0, 0, -1}, 5));
    const ObjectId j2 = AddJoint(a, ConstraintType::Rotational, Circle(housing, {30, 0, 0}, {0, 0, 1}, 5),
                                 Circle(wheel, {0, 0, 0}, {0, 0, -1}, 5));
    REQUIRE(Solve(a).converged);
    Constraint gear;
    gear.type = ConstraintType::Gear;
    gear.drive = j1;
    gear.driven = j2;
    gear.ratio = 0.5;   // 20 teeth driving 40
    gear.flipped = true;   // external gears turn opposite ways
    FitTransmission(a, gear);
    const ObjectId g = a.AddConstraint(gear).id;
    const double w0 = *JointCoordinate(a, j2);

    REQUIRE(DriveJoint(a, j1, kPi / 2).converged);
    CHECK(*JointCoordinate(a, j1) == Approx(kPi / 2).epsilon(1e-6));
    CHECK(*JointCoordinate(a, j2) - w0 == Approx(-kPi / 4).epsilon(1e-5));
    CHECK(a.FindConstraint(g)->state == ConstraintState::Satisfied);

    // Round and round: several turns in steps keep the ratio (no jump at ±π).
    double total = kPi / 2;
    for (int i = 0; i < 6; ++i) {
        total += 2.0;
        DriveJoint(a, j1, total);
    }
    const double expected = std::remainder(w0 - 0.5 * total, 2 * kPi);
    CHECK(std::abs(std::remainder(*JointCoordinate(a, j2) - expected, 2 * kPi)) < 1e-4);

    // Freedom is counted per component with the others held: held wheel,
    // held pinion — each alone cannot turn (the pair turns together).
    const auto f = AnalyzeFreedom(a);
    CHECK(f.at(pinion).Total() == 0);
    CHECK(f.at(wheel).Total() == 0);
    a.FindConstraint(g)->suppressed = true;
    CHECK(AnalyzeFreedom(a).at(wheel).rotations == 1);
    a.FindConstraint(g)->suppressed = false;

    // Removing a joint removes the transmission with it.
    a.RemoveConstraint(j1);
    CHECK_FALSE(a.FindConstraint(g));
}

TEST_CASE("screw: a slider moves ratio mm per turn of a rotational joint") {
    Assembly a;
    const ObjectId frame = a.AddComponent("f.bzc", "Станина").id;
    const ObjectId screw = a.AddComponent("s.bzc", "Винт", {Rotation{}, {0, 0, 0}}).id;
    const ObjectId nut = a.AddComponent("n.bzc", "Гайка", {Rotation{}, {0, 0, 20}}).id;
    const ObjectId turn = AddJoint(a, ConstraintType::Rotational, Circle(frame, {0, 0, 0}, {0, 0, 1}, 4),
                                   Circle(screw, {0, 0, 0}, {0, 0, -1}, 4));
    const ObjectId slide = AddJoint(a, ConstraintType::Slider, Plane(frame, {0, 0, 0}, {0, 0, 1}),
                                    Plane(nut, {0, 0, 0}, {0, 0, 1}));
    REQUIRE(Solve(a).converged);
    Constraint t;
    t.type = ConstraintType::Screw;
    t.drive = turn;
    t.driven = slide;
    t.ratio = 2.0;   // pitch 2 mm
    FitTransmission(a, t);
    a.AddConstraint(t);
    const double z0 = a.FindComponent(nut)->placement.translation.z;
    REQUIRE(DriveJoint(a, turn, kPi).converged);   // half a turn
    CHECK(a.FindComponent(nut)->placement.translation.z - z0 == Approx(1.0).epsilon(1e-5));

    // Saved and read back: the transmission and where it started.
    const Expected<Assembly> back = AssemblyFromJson(AssemblyToJson(a));
    REQUIRE(back);
    const Constraint* r = nullptr;
    for (const Constraint& c : back->constraints)
        if (c.type == ConstraintType::Screw) r = &c;
    REQUIRE(r);
    CHECK(r->drive == turn);
    CHECK(r->driven == slide);
    CHECK(r->ratio == Approx(2.0));
    CHECK(r->driveAt == Approx(a.constraints.back().driveAt));
}

TEST_CASE("new geometry fields survive a round trip; files without them still open") {
    namespace o = doc::origin;
    Assembly a;
    const ObjectId p = a.AddComponent("p.bzc", "P").id;
    const ObjectId q = a.AddComponent("q.bzc", "Q").id;
    Constraint s = Make(ConstraintType::Symmetry, Point(p, {1, 0, 0}), Line(q, {0, 0, 0}, {0, 1, 0}));
    s.third = Origin(o::kPlaneYZ);
    a.AddConstraint(s);
    Constraint m = Make(ConstraintType::Mate, Origin(o::kPlaneXY), *OriginGeometry(q, o::kPlaneXY), 2.0);
    m.limits = {true, 1, 3};
    a.AddConstraint(m);
    const Expected<Assembly> back = AssemblyFromJson(AssemblyToJson(a));
    REQUIRE(back);
    REQUIRE(back->constraints.size() == 2);
    const Constraint& s2 = back->constraints[0];
    CHECK(s2.type == ConstraintType::Symmetry);
    CHECK(s2.first.source == PickSource::Vertex);
    CHECK(s2.second.source == PickSource::Edge);
    CHECK(s2.third.component == kAssemblyOrigin);
    CHECK(s2.third.element == o::kPlaneYZ);
    CHECK(s2.third.source == PickSource::Element);
    const Constraint& m2 = back->constraints[1];
    CHECK(m2.first.OnOrigin());
    CHECK(m2.second.element == o::kPlaneXY);
    CHECK(m2.limits.enabled);
    CHECK(m2.limits.upper == Approx(3));

    // A constraint on the origin is kept; one on a component that is gone is dropped.
    const std::string text = AssemblyToJson(a);
    Expected<Assembly> again = AssemblyFromJson(text);
    REQUIRE(again);
    again->RemoveComponent(p);
    CHECK(again->constraints.size() == 1);   // the symmetry went with P
    CHECK(again->constraints[0].first.OnOrigin());
}

TEST_CASE("independent groups are solved apart; a large assembly solves quickly") {
    // 200 parts: 100 chains of two, each chain mated to the grounded base.
    Assembly a;
    const ObjectId base = a.AddComponent("base.bzc", "Плита").id;
    std::vector<ObjectId> parts;
    for (int i = 0; i < 200; ++i)
        parts.push_back(a.AddComponent("p.bzc", "Деталь",
                                       {Rotation::AxisAngle(Vec3{1, 0, 1}.Normalized(), 0.05 * (i % 7)), {i * 3.0, 0, 15}}).id);
    for (int i = 0; i < 200; i += 2) {
        const double x = i * 3.0;
        // The first of the pair onto the base (three planes), the second onto the first.
        a.AddConstraint(Make(ConstraintType::Mate, Plane(base, {x, 0, 10}, {0, 0, 1}), Plane(parts[i], {0, 0, 0}, {0, 0, -1})));
        a.AddConstraint(Make(ConstraintType::Mate, Plane(base, {x, 0, 0}, {-1, 0, 0}), Plane(parts[i], {0, 0, 0}, {-1, 0, 0}), 0, true));
        a.AddConstraint(Make(ConstraintType::Mate, Plane(base, {0, 0, 0}, {0, -1, 0}), Plane(parts[i], {0, 0, 0}, {0, -1, 0}), 0, true));
        a.AddConstraint(Make(ConstraintType::Insert, Circle(parts[i], {1, 1, 5}, {0, 0, 1}, 1), Circle(parts[i + 1], {0, 0, 0}, {0, 0, -1}, 1)));
    }
    const auto t0 = std::chrono::steady_clock::now();
    const SolveResult r = Solve(a);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    MESSAGE("200 parts, 400 constraints: solved in ", ms, " ms, ", r.iterations, " iterations");
    CHECK(r.converged);
    CHECK(ms < 3000);   // debug build, no optimisation: generous; release is far faster
    for (int i = 0; i < 200; i += 2) {
        CHECK(a.FindComponent(parts[i])->placement.translation.z == Approx(10).epsilon(1e-6));
        CHECK(a.FindComponent(parts[i + 1])->placement.Point({0, 0, 0}).z == Approx(15).epsilon(1e-6));
    }
    // Solving again (nothing changed) is cheap: no iterations.
    const auto t1 = std::chrono::steady_clock::now();
    const SolveResult again = Solve(a, {{}, 60, 1e-7, false});
    const double ms2 = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    MESSAGE("solve again without changes: ", ms2, " ms");
    CHECK(again.iterations == 0);
    CHECK_FALSE(again.moved);

    // Freedom: everything fixed except the second of each pair turning about its bolt.
    const auto f = AnalyzeFreedom(a);
    CHECK(f.at(parts[0]).Total() == 0);
    CHECK(f.at(parts[1]).rotations == 1);
    CHECK(f.at(parts[1]).translations == 0);
}

TEST_CASE("redundant and failing constraints are found per group") {
    Free t;
    t.a.AddConstraint(Make(ConstraintType::Mate, Origin(doc::origin::kPlaneXY), *OriginGeometry(t.part, doc::origin::kPlaneXY), 0, true));
    t.a.AddConstraint(Make(ConstraintType::Mate, Origin(doc::origin::kPlaneXY), *OriginGeometry(t.part, doc::origin::kPlaneXY), 0, true));
    Solve(t.a);
    CHECK(t.a.constraints[0].state == ConstraintState::Satisfied);
    CHECK(t.a.constraints[1].state == ConstraintState::Redundant);
    // A third one that wants another gap cannot hold with them.
    t.a.AddConstraint(Make(ConstraintType::Mate, Origin(doc::origin::kPlaneXY), *OriginGeometry(t.part, doc::origin::kPlaneXY), 5, true));
    Solve(t.a);
    CHECK(t.a.constraints[2].state == ConstraintState::Failed);
    // A second part, apart from the first, is not touched by their trouble.
    const ObjectId other = t.a.AddComponent("o.bzc", "Другая", {Rotation{}, {100, 0, 3}}).id;
    t.a.AddConstraint(Make(ConstraintType::Mate, Origin(doc::origin::kPlaneXY), *OriginGeometry(other, doc::origin::kPlaneXY), 7, true));
    Solve(t.a);
    CHECK(t.a.constraints[3].state == ConstraintState::Satisfied);
    CHECK(t.a.FindComponent(other)->placement.translation.z == Approx(7).epsilon(1e-6));
}
