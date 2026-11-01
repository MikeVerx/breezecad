// The assembly solver works on numbers only: these tests need no part files.
#include "assembly/Solver.h"

#include <doctest.h>

#include <cmath>
#include <cstdlib>

using namespace bz;
using namespace bz::assembly;
using doctest::Approx;

namespace {

GeometryRef Plane(ObjectId c, Vec3 point, Vec3 normal) { return {c, GeometryKind::Plane, point, normal.Normalized(), 0, point}; }
GeometryRef Circle(ObjectId c, Vec3 centre, Vec3 axis, double r) { return {c, GeometryKind::Circle, centre, axis.Normalized(), r, centre}; }
GeometryRef Axis(ObjectId c, Vec3 point, Vec3 dir) { return {c, GeometryKind::Axis, point, dir.Normalized(), 0, point}; }
GeometryRef Cylinder(ObjectId c, Vec3 point, Vec3 dir, double r) { return {c, GeometryKind::Cylinder, point, dir.Normalized(), r, point}; }
GeometryRef Sphere(ObjectId c, Vec3 centre, double r) { return {c, GeometryKind::Sphere, centre, {}, r, centre}; }

Constraint Make(ConstraintType type, GeometryRef a, GeometryRef b, double value = 0, bool flipped = false) {
    Constraint c;
    c.type = type;
    c.first = a;
    c.second = b;
    c.value = value;
    c.flipped = flipped;
    return c;
}

/// A grounded base and a free part placed somewhere, a little turned.
struct Pair {
    Assembly a;
    ObjectId base, part;
    Pair() {
        base = a.AddComponent("base.bzc", "Основание").id;
        part = a.AddComponent("part.bzc", "Деталь",
                              {Rotation::AxisAngle(Vec3{1, 1, 0}.Normalized(), 0.3), {5, 5, 30}}).id;
    }
    Placement& P() { return a.FindComponent(part)->placement; }
};

} // namespace

TEST_CASE("mate puts a face against a face and leaves it free to slide") {
    Pair t;
    // Top of the base at z = 10; bottom of the part at its origin, facing down.
    t.a.AddConstraint(Make(ConstraintType::Mate, Plane(t.base, {0, 0, 10}, {0, 0, 1}), Plane(t.part, {0, 0, 0}, {0, 0, -1})));
    const SolveResult r = Solve(t.a);
    CHECK(r.converged);
    CHECK(r.moved);
    CHECK(t.P().translation.z == Approx(10).epsilon(1e-6));
    CHECK(t.P().Direction({0, 0, 1}).z == Approx(1).epsilon(1e-6));   // standing upright
    // Only what the constraint needs changed: it did not wander sideways.
    CHECK(t.P().translation.x == Approx(5).epsilon(0.05));
    CHECK(t.P().translation.y == Approx(5).epsilon(0.05));
    CHECK(t.a.constraints[0].state == ConstraintState::Satisfied);

    const auto f = AnalyzeFreedom(t.a);
    CHECK(f.at(t.base).Total() == 0);   // grounded
    CHECK(f.at(t.part).translations == 2);
    CHECK(f.at(t.part).rotations == 1);

    // Solving again moves nothing.
    CHECK_FALSE(Solve(t.a).moved);
}

TEST_CASE("mate with a gap, and flush") {
    Pair t;
    t.a.AddConstraint(Make(ConstraintType::Mate, Plane(t.base, {0, 0, 10}, {0, 0, 1}), Plane(t.part, {0, 0, 0}, {0, 0, -1}), 2.5));
    REQUIRE(Solve(t.a).converged);
    CHECK(t.P().translation.z == Approx(12.5).epsilon(1e-6));

    Pair u;   // flush: the part's top (z 8 in it) level with the base's top
    u.a.AddConstraint(Make(ConstraintType::Mate, Plane(u.base, {0, 0, 10}, {0, 0, 1}), Plane(u.part, {0, 0, 8}, {0, 0, 1}), 0, true));
    REQUIRE(Solve(u.a).converged);
    CHECK(u.P().Point({0, 0, 8}).z == Approx(10).epsilon(1e-6));
    CHECK(u.P().Direction({0, 0, 1}).z == Approx(1).epsilon(1e-6));
}

TEST_CASE("insert: a bolt in a hole keeps only its turn") {
    Pair t;
    // Hole on the base's top at (20, 20); the bolt's head underside at its origin.
    t.a.AddConstraint(Make(ConstraintType::Insert, Circle(t.base, {20, 20, 10}, {0, 0, 1}, 4), Circle(t.part, {0, 0, 0}, {0, 0, -1}, 4)));
    REQUIRE(Solve(t.a).converged);
    CHECK(t.P().translation.x == Approx(20).epsilon(1e-6));
    CHECK(t.P().translation.y == Approx(20).epsilon(1e-6));
    CHECK(t.P().translation.z == Approx(10).epsilon(1e-6));
    CHECK(t.P().Direction({0, 0, 1}).z == Approx(1).epsilon(1e-6));
    const Freedom f = AnalyzeFreedom(t.a).at(t.part);
    CHECK(f.translations == 0);
    CHECK(f.rotations == 1);
}

TEST_CASE("axis mate leaves sliding along and turning about the axis") {
    Pair t;
    t.a.AddConstraint(Make(ConstraintType::AxisMate, Axis(t.base, {20, 0, 0}, {0, 0, 1}), Axis(t.part, {0, 0, 0}, {0, 0, 1})));
    REQUIRE(Solve(t.a).converged);
    CHECK(t.P().translation.x == Approx(20).epsilon(1e-6));
    CHECK(t.P().translation.y == Approx(0).epsilon(1e-6));
    CHECK(std::abs(t.P().Direction({0, 0, 1}).z) == Approx(1).epsilon(1e-6));
    const Freedom f = AnalyzeFreedom(t.a).at(t.part);
    CHECK(f.translations == 1);
    CHECK(f.rotations == 1);
}

TEST_CASE("angle between two faces") {
    Pair t;
    // The part stands on the base and its side face makes 30° with the base's front face.
    t.a.AddConstraint(Make(ConstraintType::Mate, Plane(t.base, {0, 0, 10}, {0, 0, 1}), Plane(t.part, {0, 0, 0}, {0, 0, -1})));
    t.a.AddConstraint(Make(ConstraintType::Angle, Plane(t.base, {0, 0, 0}, {0, -1, 0}), Plane(t.part, {0, 0, 0}, {0, -1, 0}), kPi / 6));
    REQUIRE(Solve(t.a).converged);
    CHECK(t.P().Direction({0, -1, 0}).Dot({0, -1, 0}) == Approx(std::cos(kPi / 6)).epsilon(1e-6));
    const Freedom f = AnalyzeFreedom(t.a).at(t.part);
    CHECK(f.translations == 2);
    CHECK(f.rotations == 0);
}

TEST_CASE("tangent: cylinder and sphere on a plane, two cylinders") {
    Pair t;   // a roller of radius 5 lying on the base
    t.a.AddConstraint(Make(ConstraintType::Tangent, Plane(t.base, {0, 0, 10}, {0, 0, 1}), Cylinder(t.part, {0, 0, 0}, {1, 0, 0}, 5)));
    REQUIRE(Solve(t.a).converged);
    CHECK(t.P().translation.z == Approx(15).epsilon(1e-6));
    CHECK(t.P().Direction({1, 0, 0}).z == Approx(0).epsilon(1e-6));
    Freedom f = AnalyzeFreedom(t.a).at(t.part);
    CHECK(f.translations == 2);
    CHECK(f.rotations == 2);

    Pair s;   // a ball
    s.a.AddConstraint(Make(ConstraintType::Tangent, Sphere(s.part, {0, 0, 0}, 3), Plane(s.base, {0, 0, 10}, {0, 0, 1})));
    REQUIRE(Solve(s.a).converged);
    CHECK(s.P().translation.z == Approx(13).epsilon(1e-6));
    f = AnalyzeFreedom(s.a).at(s.part);
    CHECK(f.translations == 2);
    CHECK(f.rotations == 3);

    Pair g;   // two gears' pitch cylinders, 10 and 6
    g.a.AddConstraint(Make(ConstraintType::Tangent, Cylinder(g.base, {0, 0, 0}, {0, 0, 1}, 10), Cylinder(g.part, {0, 0, 0}, {0, 0, 1}, 6)));
    REQUIRE(Solve(g.a).converged);
    const Vec3 c = g.P().translation;
    CHECK(std::hypot(c.x, c.y) == Approx(16).epsilon(1e-6));
}

TEST_CASE("contradicting constraints are marked, repeated ones are redundant") {
    Pair t;
    t.a.AddConstraint(Make(ConstraintType::Mate, Plane(t.base, {0, 0, 10}, {0, 0, 1}), Plane(t.part, {0, 0, 0}, {0, 0, -1})));
    t.a.AddConstraint(Make(ConstraintType::Mate, Plane(t.base, {0, 0, 10}, {0, 0, 1}), Plane(t.part, {0, 0, 0}, {0, 0, -1})));
    REQUIRE(Solve(t.a).converged);
    CHECK(t.a.constraints[0].state == ConstraintState::Satisfied);
    CHECK(t.a.constraints[1].state == ConstraintState::Redundant);

    Pair u;   // the same face on two levels at once
    u.a.AddConstraint(Make(ConstraintType::Mate, Plane(u.base, {0, 0, 10}, {0, 0, 1}), Plane(u.part, {0, 0, 0}, {0, 0, -1})));
    u.a.AddConstraint(Make(ConstraintType::Mate, Plane(u.base, {0, 0, 20}, {0, 0, 1}), Plane(u.part, {0, 0, 0}, {0, 0, -1})));
    CHECK_FALSE(Solve(u.a).converged);
    const bool marked = u.a.constraints[0].state == ConstraintState::Failed || u.a.constraints[1].state == ConstraintState::Failed;
    CHECK(marked);

    // Suppressed constraints are left out.
    u.a.constraints[1].suppressed = true;
    CHECK(Solve(u.a).converged);
    CHECK(u.a.constraints[1].state == ConstraintState::Unknown);
}

TEST_CASE("three mates fix a part completely") {
    Pair t;
    t.a.AddConstraint(Make(ConstraintType::Mate, Plane(t.base, {0, 0, 10}, {0, 0, 1}), Plane(t.part, {0, 0, 0}, {0, 0, -1})));
    t.a.AddConstraint(Make(ConstraintType::Mate, Plane(t.base, {0, 0, 0}, {0, -1, 0}), Plane(t.part, {0, 0, 0}, {0, -1, 0}), 0, true));
    t.a.AddConstraint(Make(ConstraintType::Mate, Plane(t.base, {0, 0, 0}, {-1, 0, 0}), Plane(t.part, {0, 0, 0}, {-1, 0, 0}), 0, true));
    REQUIRE(Solve(t.a).converged);
    CHECK(t.P().translation.x == Approx(0).epsilon(1e-6));
    CHECK(t.P().translation.y == Approx(0).epsilon(1e-6));
    CHECK(t.P().translation.z == Approx(10).epsilon(1e-6));
    CHECK(AnalyzeFreedom(t.a).at(t.part).Total() == 0);
}

TEST_CASE("a free part without constraints has six freedoms; the kept one gives way last") {
    Assembly a;
    const ObjectId base = a.AddComponent("b.bzc", "B").id;
    a.FindComponent(base)->grounded = false;   // two free parts mated to each other
    const ObjectId other = a.AddComponent("o.bzc", "O", {Rotation{}, {0, 0, 50}}).id;
    CHECK(AnalyzeFreedom(a).at(other).Total() == 6);
    a.AddConstraint(Make(ConstraintType::Mate, Plane(base, {0, 0, 10}, {0, 0, 1}), Plane(other, {0, 0, 0}, {0, 0, -1})));
    SolveOptions keep;
    keep.keep = other;   // the user drags `other`: `base` moves instead
    REQUIRE(Solve(a, keep).converged);
    CHECK(a.FindComponent(other)->placement.translation.z == Approx(50).epsilon(0.02));
    CHECK(a.FindComponent(base)->placement.translation.z == Approx(40).epsilon(0.02));
}

// --- joints --------------------------------------------------------------------------

namespace {

/// A joint made where the parts stand, as the UI makes it.
ObjectId AddJoint(Assembly& a, ConstraintType type, GeometryRef first, GeometryRef second, double value = 0) {
    Constraint c = Make(type, first, second, value);
    FitJoint(a, c);
    return a.AddConstraint(c).id;
}

} // namespace

TEST_CASE("joints leave exactly the motion they name") {
    struct Case { ConstraintType type; int translations, rotations; };
    for (const Case k : {Case{ConstraintType::Rotational, 0, 1}, Case{ConstraintType::Slider, 1, 0},
                         Case{ConstraintType::Cylindrical, 1, 1}, Case{ConstraintType::Planar, 2, 1},
                         Case{ConstraintType::Ball, 0, 3}, Case{ConstraintType::Rigid, 0, 0}}) {
        CAPTURE(static_cast<int>(k.type));
        Pair t;
        AddJoint(t.a, k.type, Circle(t.base, {0, 0, 10}, {0, 0, 1}, 4), Circle(t.part, {0, 0, 0}, {0, 0, -1}, 4));
        const SolveResult r = Solve(t.a);
        CHECK(r.converged);
        CHECK(t.a.constraints[0].state == ConstraintState::Satisfied);
        const Freedom f = AnalyzeFreedom(t.a).at(t.part);
        CHECK(f.translations == k.translations);
        CHECK(f.rotations == k.rotations);
    }
}

TEST_CASE("rotational joint: puts the axis on the axis, then turns when driven") {
    Pair t;
    const ObjectId id = AddJoint(t.a, ConstraintType::Rotational, Circle(t.base, {20, 0, 10}, {0, 0, 1}, 4),
                                 Circle(t.part, {0, 0, 0}, {0, 0, -1}, 4));
    CHECK_FALSE(t.a.constraints[0].flipped);   // the part's Z looks down at the base's: facing
    REQUIRE(Solve(t.a).converged);
    CHECK(t.P().Point({0, 0, 0}).x == Approx(20).epsilon(1e-6));
    CHECK(t.P().Point({0, 0, 0}).z == Approx(10).epsilon(1e-6));
    const double start = *JointPosition(t.a, t.a.constraints[0]);
    CHECK(std::abs(start) < 0.35);   // turned only by what the axis needed

    const Vec3 tip0 = t.P().Point({10, 0, 0});
    const SolveResult r = DriveJoint(t.a, id, kPi / 2);
    CHECK(r.converged);
    CHECK(*JointPosition(t.a, t.a.constraints[0]) == Approx(kPi / 2).epsilon(1e-6));
    CHECK_FALSE(t.a.constraints[0].locked);   // driving does not lock
    // Still on the axis; a point 10 mm out went a quarter round it.
    CHECK(t.P().Point({0, 0, 0}).x == Approx(20).epsilon(1e-6));
    const Vec3 tip = t.P().Point({10, 0, 0});
    CHECK(tip.z == Approx(10).epsilon(1e-6));
    CHECK((tip - Vec3{20, 0, 10}).Length() == Approx(10).epsilon(1e-6));
    CHECK((tip - tip0).Length() > 5);
}

TEST_CASE("locked joint holds its position; limits keep it in range") {
    Pair t;
    const ObjectId id = AddJoint(t.a, ConstraintType::Rotational, Circle(t.base, {0, 0, 10}, {0, 0, 1}, 4),
                                 Circle(t.part, {0, 0, 0}, {0, 0, -1}, 4));
    Solve(t.a);
    Constraint& c = *t.a.FindConstraint(id);
    c.locked = true;
    c.position = 0.4;
    CHECK(Solve(t.a).converged);
    CHECK(*JointPosition(t.a, c) == Approx(0.4).epsilon(1e-6));
    CHECK(AnalyzeFreedom(t.a).at(t.part).Total() == 0);

    // Driving a locked joint moves the lock.
    DriveJoint(t.a, id, -0.2);
    CHECK(c.locked);
    CHECK(c.position == Approx(-0.2));

    // Limits: driven past them, it stops at the nearer end; they do not take a freedom.
    c.locked = false;
    c.limits = {true, -0.5, 0.5};
    DriveJoint(t.a, id, 1.2);
    CHECK(*JointPosition(t.a, c) == Approx(0.5).epsilon(1e-5));
    DriveJoint(t.a, id, -3.0);   // round past −π: nearer to −0.5
    CHECK(*JointPosition(t.a, c) == Approx(-0.5).epsilon(1e-5));
    CHECK(AnalyzeFreedom(t.a).at(t.part).rotations == 1);
    CHECK(c.state == ConstraintState::Satisfied);

    // Moved outside by hand: the next solve brings it back to the limit.
    Placement& p = t.P();
    p.rotation = Rotation::AxisAngle({0, 0, 1}, -1.0) * p.rotation;
    CHECK(*JointPosition(t.a, c) == Approx(-1.5).epsilon(1e-5));
    const SolveResult back = Solve(t.a);
    CAPTURE(back.error);
    CAPTURE(back.iterations);
    CHECK(back.converged);
    CHECK(*JointPosition(t.a, c) == Approx(-0.5).epsilon(1e-5));
}

TEST_CASE("slider joint: slides along the axis without turning, within limits") {
    Pair t;
    t.P() = {Rotation{}, {0, 0, 30}};
    const ObjectId id = AddJoint(t.a, ConstraintType::Slider, Plane(t.base, {0, 0, 10}, {0, 0, 1}),
                                 Plane(t.part, {0, 0, 0}, {0, 0, 1}));
    CHECK(t.a.constraints[0].flipped);   // both normals up
    CHECK(t.a.constraints[0].position == Approx(20));
    CHECK(Solve(t.a).converged);
    CHECK_FALSE(t.a.FindComponent(t.part)->placement.translation.z != Approx(30));   // already holds

    t.a.FindConstraint(id)->limits = {true, 0.0, 25.0};
    DriveJoint(t.a, id, 5.0);
    CHECK(t.P().translation.z == Approx(15).epsilon(1e-6));
    CHECK(t.P().Direction({1, 0, 0}).x == Approx(1).epsilon(1e-6));
    DriveJoint(t.a, id, 100.0);
    CHECK(*JointPosition(t.a, *t.a.FindConstraint(id)) == Approx(25).epsilon(1e-6));
}

TEST_CASE("joint with both parts free moves the second, keeps the first") {
    Assembly a;
    const ObjectId g = a.AddComponent("g.bzc", "Земля").id;   // grounded, ties nothing
    const ObjectId p1 = a.AddComponent("a.bzc", "А", {Rotation{}, {0, 0, 0}}).id;
    const ObjectId p2 = a.AddComponent("b.bzc", "Б", {Rotation{}, {0, 0, 5}}).id;
    (void)g;
    const ObjectId id = AddJoint(a, ConstraintType::Rotational, Circle(p1, {0, 0, 5}, {0, 0, 1}, 3),
                                 Circle(p2, {0, 0, 0}, {0, 0, -1}, 3));
    REQUIRE(Solve(a).converged);
    DriveJoint(a, id, 1.0);
    CHECK(a.FindComponent(p1)->placement.IsIdentity(1e-2));   // moved least, not nothing
    CHECK(*JointPosition(a, *a.FindConstraint(id)) == Approx(1.0).epsilon(1e-6));
}

// --- 0.2.5: parts flung metres away by constraints that cannot all hold ---

namespace {

/// The farthest any component's origin is from where it started.
double FarthestMove(const Assembly& a, const std::vector<Placement>& start) {
    double far = 0;
    for (std::size_t i = 0; i < a.components.size(); ++i)
        far = std::max(far, (a.components[i].placement.translation - start[i].translation).Length());
    return far;
}

std::vector<Placement> Placements(const Assembly& a) {
    std::vector<Placement> out;
    for (const Component& c : a.components) out.push_back(c.placement);
    return out;
}

} // namespace

TEST_CASE("a shaft, a coupling half mated and a motor tied to it: nothing flies off, solving again moves nothing") {
    // The report: a coupling only put on the shaft's axis (its end could not
    // be caught on the shaft's end), then a motor mated to the coupling — the
    // coupling, 5 cm long, landed 8 m away.
    Assembly a;
    const ObjectId shaft = a.AddComponent("shaft.bzc", "Вал").id;   // grounded, first
    const ObjectId coupling = a.AddComponent("coupling.bzc", "Муфта", {Rotation{}, {3, 2, 120}}).id;
    const ObjectId motor = a.AddComponent("motor.bzc", "Мотор", {Rotation::AxisAngle(Vec3{0, 1, 0}, 0.2), {40, -10, 200}}).id;
    // Shaft along Z, 100 long; coupling's bore along its Z; motor's shaft along its Z.
    a.AddConstraint(Make(ConstraintType::AxisMate, Axis(shaft, {0, 0, 0}, {0, 0, 1}), Axis(coupling, {0, 0, 0}, {0, 0, 1})));
    a.AddConstraint(Make(ConstraintType::AxisMate, Axis(coupling, {0, 0, 0}, {0, 0, 1}), Axis(motor, {0, 0, 0}, {0, 0, 1})));
    a.AddConstraint(Make(ConstraintType::Mate, Plane(coupling, {0, 0, 50}, {0, 0, 1}), Plane(motor, {0, 0, 0}, {0, 0, -1})));
    // The motor's flange also mated to a plane of the shaft's part that the
    // axis cannot reach (parallel to the axis, 30 mm off it): cannot hold.
    a.AddConstraint(Make(ConstraintType::Mate, Plane(shaft, {30, 0, 0}, {1, 0, 0}), Plane(motor, {0, 0, 0}, {-1, 0, 0})));
    const std::vector<Placement> start = Placements(a);
    Solve(a);
    CHECK(FarthestMove(a, start) < 500.0);
    // The same assembly solved again and again (the screen does it each time
    // something is touched): it must not creep further.
    const std::vector<Placement> once = Placements(a);
    for (int i = 0; i < 30; ++i) Solve(a);
    CHECK(FarthestMove(a, once) < 1e-3);
}

TEST_CASE("random sets of constraints, many impossible: no component goes far, solving again moves nothing") {
    // A fixed seed: the same "random" assemblies every run.
    std::uint64_t seed = 0x2025;
    const auto rnd = [&] {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>((seed >> 11) & ((1ull << 53) - 1)) / static_cast<double>(1ull << 53);
    };
    const auto dir = [&] {
        Vec3 v{rnd() - 0.5, rnd() - 0.5, rnd() - 0.5};
        if (rnd() < 0.6) {   // most faces and axes of real parts are along the part's axes
            const int k = static_cast<int>(rnd() * 3);
            v = {k == 0 ? 1.0 : 0.0, k == 1 ? 1.0 : 0.0, k == 2 ? 1.0 : 0.0};
            if (rnd() < 0.5) v = v * -1.0;
        }
        return v.Length() < 1e-6 ? Vec3{0, 0, 1} : v.Normalized();
    };
    const auto point = [&] { return Vec3{(rnd() - 0.5) * 60, (rnd() - 0.5) * 60, (rnd() - 0.5) * 60}; };
    int worst = 0;
    double worstMove = 0;
    for (int trial = 0; trial < 300; ++trial) {
        Assembly a;
        const int n = 2 + static_cast<int>(rnd() * 4);
        std::vector<ObjectId> ids;
        // Every draw in its own statement: the order of function arguments
        // differs between compilers (clang left to right, GCC right to left),
        // and the assemblies must be the same everywhere.
        const auto plane = [&](ObjectId c) { const Vec3 p = point(); const Vec3 d = dir(); return Plane(c, p, d); };
        const auto axis = [&](ObjectId c) { const Vec3 p = point(); const Vec3 d = dir(); return Axis(c, p, d); };
        const auto circle = [&](ObjectId c) { const Vec3 p = point(); const Vec3 d = dir(); return Circle(c, p, d, 3); };
        for (int i = 0; i < n; ++i) {
            const Vec3 about = dir();
            const double angle = rnd() * 3;
            const Vec3 at = point();
            ids.push_back(a.AddComponent("p.bzc", "Деталь", {Rotation::AxisAngle(about, angle), at}).id);
        }
        const int m = 1 + static_cast<int>(rnd() * 6);
        for (int j = 0; j < m; ++j) {
            const ObjectId x = ids[static_cast<std::size_t>(rnd() * n) % n];
            ObjectId y = ids[static_cast<std::size_t>(rnd() * n) % n];
            if (x == y) y = ids[(std::find(ids.begin(), ids.end(), x) - ids.begin() + 1) % n];
            const double kind = rnd();
            if (kind < 0.4) {
                const auto e1 = plane(x);
                const auto e2 = plane(y);
                const double offset = rnd() < 0.3 ? rnd() * 10 : 0;
                a.AddConstraint(Make(ConstraintType::Mate, e1, e2, offset));
            } else if (kind < 0.7) {
                const auto e1 = axis(x);
                const auto e2 = axis(y);
                a.AddConstraint(Make(ConstraintType::AxisMate, e1, e2));
            } else if (kind < 0.85) {
                const auto e1 = circle(x);
                const auto e2 = circle(y);
                a.AddConstraint(Make(ConstraintType::Insert, e1, e2));
            } else {
                const auto e1 = axis(x);
                const auto e2 = plane(y);
                a.AddConstraint(Make(ConstraintType::Mate, e1, e2));
            }
        }
        const std::vector<Placement> start = Placements(a);
        const SolveResult first = Solve(a);
        const double moved = FarthestMove(a, start);
        if (moved > 1000 && std::getenv("BZ_SOLVER_DEBUG"))
            MESSAGE("trial ", trial, " converged ", first.converged, " error ", first.error, " iterations ", first.iterations, " parts ", n, " constraints ", m);
        if (moved > worstMove) worstMove = moved, worst = trial;
        // Parts within 60 mm of each other: a solution never needs more than a few times that.
        CHECK_MESSAGE(moved < 1000.0, "trial ", trial, ": moved ", moved, " mm");
        // Solving again may finish what the iteration limit cut short, but
        // only a little, and then it settles: nothing moves any more.
        const std::vector<Placement> once = Placements(a);
        for (int i = 0; i < 10; ++i) Solve(a);
        CHECK_MESSAGE(FarthestMove(a, once) < 100.0, "trial ", trial, ": creeps ", FarthestMove(a, once), " mm on solving again");
        const std::vector<Placement> settled = Placements(a);
        for (int i = 0; i < 10; ++i) Solve(a);
        CHECK_MESSAGE(FarthestMove(a, settled) < 0.05, "trial ", trial, ": still creeps ", FarthestMove(a, settled), " mm");
    }
    MESSAGE("farthest move: ", worstMove, " mm (trial ", worst, ")");
}

TEST_CASE("a part drawn by its neighbours (a follower) is never moved by constraints") {
    Pair t;
    t.a.FindComponent(t.base)->grounded = false;
    t.a.FindComponent(t.part)->follower = true;   // as the program marks a part with projections from the others
    const Placement was = t.P();
    t.a.AddConstraint(Make(ConstraintType::Mate, Plane(t.base, {0, 0, 10}, {0, 0, 1}), Plane(t.part, {0, 0, 0}, {0, 0, -1})));
    Solve(t.a);
    CHECK((t.P().translation - was.translation).Length() < 1e-12);
    // The other one gave way instead.
    CHECK(t.a.constraints[0].state == ConstraintState::Satisfied);
    CHECK(AnalyzeFreedom(t.a).at(t.part).Total() == 0);
}

TEST_CASE("a part others are drawn from (it leads) is not moved to a compromise of impossible constraints") {
    // Its left face against the base's right, its right face against the
    // base's left: it cannot be on both sides. A plain part goes to the least
    // bad place between; one the others are drawn from stays where it is.
    for (const bool leads : {false, true}) {
        CAPTURE(leads);
        Assembly a;
        const ObjectId base = a.AddComponent("base.bzc", "Основание").id;   // 0..60 along X, grounded
        const ObjectId part = a.AddComponent("part.bzc", "Сосед", {Rotation{}, {60, 0, 0}}).id;   // 0..40 of its own
        a.FindComponent(part)->leads = leads;
        a.AddConstraint(Make(ConstraintType::Mate, Plane(base, {60, 20, 5}, {1, 0, 0}), Plane(part, {0, 20, 5}, {-1, 0, 0})));
        a.AddConstraint(Make(ConstraintType::Mate, Plane(base, {0, 20, 5}, {-1, 0, 0}), Plane(part, {40, 20, 5}, {1, 0, 0})));
        const std::vector<Placement> start = Placements(a);
        Solve(a);
        if (leads) {
            CHECK(FarthestMove(a, start) < 1e-9);
            CHECK(a.constraints[0].state == ConstraintState::Satisfied);
            CHECK(a.constraints[1].state == ConstraintState::Failed);
        } else {
            CHECK(FarthestMove(a, start) > 1.0);   // the compromise, as before
        }
    }
}
