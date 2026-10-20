#include "../Fixtures.h"
#include "assembly/AssemblyFile.h"
#include "assembly/Solver.h"
#include "document/BzcFile.h"
#include "kernel/AutoDrop.h"
#include "kernel/ComponentPattern.h"

#include <doctest.h>

#include <filesystem>

using namespace bz;
using namespace bz::assembly;
using doctest::Approx;
namespace parts = bz::doc::parts;

namespace {

/// 100 x 40 plate from z = 0 up with ⌀11 holes at y = 20 and the given x,
/// and a smaller ⌀6 hole at (90, 34) that is not "such" a hole.
doc::Document Plate(const std::string& thickness, std::vector<double> xs) {
    doc::Document d;
    auto& sk = test::AddSketch(d);
    test::AddRect(d, sk, {0, 0}, {100, 40});
    for (double x : xs) test::AddCircle(d, sk, {x, 20}, 5.5);
    test::AddCircle(d, sk, {90, 34}, 3);
    test::AddExtrude(d, sk.id, thickness);
    return d;
}

/// A 10 mm plate over a 5 mm one; the top has holes at x = 20, 50, 80, the
/// bottom only at 20 and 50. An M10 bolt with a washer and a nut in the first.
struct Joint {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "bz_pattern";
    std::filesystem::path assemblyFile = dir / "unit.bza";
    Assembly              a;
    ObjectId              top, bottom;
    std::vector<ObjectId> bolt;   // washer, bolt, nut
    kernel::AssemblyModel model;

    Joint() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        REQUIRE(doc::SaveBzc(Plate("10", {20, 50, 80}), dir / "top.bzc", "tests"));
        REQUIRE(doc::SaveBzc(Plate("5", {20, 50}), dir / "bottom.bzc", "tests"));
        top = a.AddComponent("top.bzc", "Плита").id;
        Component& b = a.AddComponent("bottom.bzc", "Планка", {Rotation{}, {0, 0, -5}});
        b.grounded = true;
        bottom = b.id;
        model.Update(a, assemblyFile);

        const auto rim = model.GeometryAt(a, top, {25.5, 20, 10}, kernel::AssemblyPick::Circle);
        REQUIRE(rim);
        auto plan = kernel::PlanAutoDrop(model, a, *rim, {"gost7798", "gost11371", "gost5915"});
        REQUIRE_MESSAGE(plan, (plan ? "" : plan.GetError().message));
        for (const kernel::DropItem& item : plan->items) {
            auto made = parts::MakePart(item.request);
            REQUIRE(made);
            const std::string file = parts::Designation(item.request) + ".bzc";
            REQUIRE(doc::SaveBzc(made->doc, dir / file, "tests"));
            Component& c = a.AddComponent(file, "Изделие", item.placement);
            Constraint k = item.insert;
            if (item.seatOn >= 0) k.first.component = bolt[std::size_t(item.seatOn)];
            k.second.component = c.id;
            bolt.push_back(c.id);
            a.AddConstraint(k);
        }
        Update();
    }
    ~Joint() { std::filesystem::remove_all(dir); }
    void Update() {
        a.Touch();
        model.Update(a, assemblyFile);
    }
    double CentreX(ObjectId c) const {
        const auto b = model.ComponentBounds(c);
        REQUIRE(b);
        return (b->min.x + b->max.x) / 2;
    }
};

const auto kAll = [](const Component&) { return true; };

} // namespace

TEST_CASE("Component pattern: the holes like the picked one") {
    Joint j;
    const auto rim = kernel::SeatHoleOf(j.a, {j.bolt[0]});
    REQUIRE(rim);
    CHECK(rim->component == j.top);
    const auto holes = j.model.SameHoles(j.a, *rim);
    REQUIRE(holes.size() == 3);   // not the ⌀6 one, not the rims on the underside
    CHECK(holes[0].origin.x == Approx(20));
    CHECK(holes[1].origin.x == Approx(50));
    CHECK(holes[2].origin.x == Approx(80));
    for (const GeometryRef& h : holes) CHECK(h.direction.z == Approx(1));
}

TEST_CASE("Component pattern: a bolt brings its washer and the nut in the same hole") {
    Joint j;
    const auto group = kernel::PatternGroup(j.a, j.bolt[1], kAll);
    REQUIRE(group.size() == 3);
    CHECK(group[0] == j.bolt[1]);
    CHECK(std::find(group.begin(), group.end(), j.bolt[0]) != group.end());
    CHECK(std::find(group.begin(), group.end(), j.bolt[2]) != group.end());   // tied to the bottom plate only
    CHECK(std::find(group.begin(), group.end(), j.top) == group.end());       // the plate does not come along
    CHECK(std::find(group.begin(), group.end(), j.bottom) == group.end());
}

TEST_CASE("Component pattern: into the other holes, each tied by its Вставка") {
    Joint j;
    ComponentPattern p;
    p.kind = PatternKind::Holes;
    p.seed = kernel::PatternGroup(j.a, j.bolt[1], kAll);
    p.rim = *kernel::SeatHoleOf(j.a, p.seed);
    const ObjectId id = j.a.AddPattern(p).id;
    CHECK(j.a.FindPattern(id)->name == "Массив:1");
    const std::size_t parts0 = j.a.components.size(), ties0 = j.a.constraints.size();

    auto made = kernel::ApplyPattern(j.model, j.a, id);
    REQUIRE_MESSAGE(made, (made ? "" : made.GetError().message));
    CHECK(*made == 2);
    const ComponentPattern& pat = *j.a.FindPattern(id);
    REQUIRE(pat.instances.size() == 2);
    CHECK(j.a.components.size() == parts0 + 6);
    // At x = 50 all three Вставки; at x = 80 the bottom plate has no hole:
    // the nut's is dropped, the nut just stays under the plate.
    CHECK(j.a.constraints.size() == ties0 + 3 + 2);
    for (const Constraint& c : j.a.constraints) CHECK(ConstraintError(j.a, c) < 1e-6);
    const SolveResult solved = Solve(j.a);
    CHECK(solved.converged);
    CHECK_FALSE(solved.moved);

    j.Update();
    const ObjectId bolt50 = pat.instances[0][0], bolt80 = pat.instances[1][0];
    CHECK(j.a.PatternOfInstance(bolt50) == &pat);
    CHECK(j.a.PatternOfInstance(j.bolt[1]) == nullptr);
    CHECK(j.CentreX(bolt50) == Approx(50).epsilon(1e-4));
    CHECK(j.CentreX(bolt80) == Approx(80).epsilon(1e-4));
    CHECK(j.a.FindComponent(bolt50)->name == "Изделие:4");
    CHECK(j.a.FindComponent(bolt50)->properties == j.a.FindComponent(j.bolt[1])->properties);
    // Each instance's washer sits in its own hole of the plate.
    int intoTop = 0;
    for (const Constraint& c : j.a.constraints)
        if (c.first.component == j.top && pat.IsInstance(c.second.component)) {
            ++intoTop;
            CHECK((c.first.origin.x == Approx(50) || c.first.origin.x == Approx(80)));
        }
    CHECK(intoTop == 2);

    // Edited (applied again): the same count, not doubled.
    REQUIRE(kernel::ApplyPattern(j.model, j.a, id));
    CHECK(j.a.components.size() == parts0 + 6);
    CHECK(j.a.constraints.size() == ties0 + 5);

    // Saved and read back.
    std::vector<std::string> warnings;
    auto back = AssemblyFromJson(AssemblyToJson(j.a), &warnings);
    CHECK(warnings.empty());
    REQUIRE(back);
    REQUIRE(back->patterns.size() == 1);
    CHECK(back->patterns[0].kind == PatternKind::Holes);
    CHECK(back->patterns[0].seed == pat.seed);
    CHECK(back->patterns[0].instances == pat.instances);
    CHECK(back->patterns[0].rim.origin.x == Approx(20));

    // Removed as a whole: the seed stays.
    j.a.RemovePattern(id);
    CHECK(j.a.patterns.empty());
    CHECK(j.a.components.size() == parts0);
    CHECK(j.a.constraints.size() == ties0);
}

TEST_CASE("Component pattern: removing the seed removes the pattern, an instance leaves it") {
    Joint j;
    ComponentPattern p;
    p.seed = kernel::PatternGroup(j.a, j.bolt[1], kAll);
    p.rim = *kernel::SeatHoleOf(j.a, p.seed);
    const ObjectId id = j.a.AddPattern(p).id;
    REQUIRE(kernel::ApplyPattern(j.model, j.a, id));
    const std::size_t parts = j.a.components.size();
    j.a.RemoveComponent(j.a.FindPattern(id)->instances[1][2]);   // a nut of the second
    CHECK(j.a.FindPattern(id)->instances[1].size() == 2);
    j.a.RemoveComponent(j.bolt[0]);   // the seed's washer
    CHECK(j.a.patterns.empty());
    CHECK(j.a.components.size() == parts - 1 - 1 - 5);
}

TEST_CASE("Component pattern: rectangular and circular") {
    Assembly a;
    a.AddComponent("base.bzc", "Основание");
    const ObjectId pin = a.AddComponent("pin.bzc", "Штифт", {Rotation{}, {10, 0, 0}}).id;
    kernel::AssemblyModel model;   // no geometry: placements only

    ComponentPattern r;
    r.kind = PatternKind::Rectangular;
    r.seed = {pin};
    r.count1 = 3, r.spacing1 = 15;
    r.count2 = 2, r.spacing2 = -8;
    const ObjectId rect = a.AddPattern(r).id;
    auto made = kernel::ApplyPattern(model, a, rect);
    REQUIRE(made);
    CHECK(*made == 5);
    const auto& inst = a.FindPattern(rect)->instances;
    CHECK(a.FindComponent(inst[1][0])->placement.translation.x == Approx(40));
    CHECK(a.FindComponent(inst[4][0])->placement.translation.y == Approx(-8));
    CHECK(a.FindComponent(inst[4][0])->placement.translation.x == Approx(40));

    ComponentPattern c;
    c.kind = PatternKind::Circular;
    c.seed = {pin};
    c.count = 4;
    const ObjectId circ = a.AddPattern(c).id;
    REQUIRE(kernel::ApplyPattern(model, a, circ));
    const auto& ring = a.FindPattern(circ)->instances;
    REQUIRE(ring.size() == 3);
    const Placement& quarter = a.FindComponent(ring[0][0])->placement;
    CHECK(quarter.translation.x == Approx(0).epsilon(1e-9));
    CHECK(quarter.translation.y == Approx(10));
    CHECK(quarter.rotation.col[0].y == Approx(1));   // turned with it
    CHECK(a.FindComponent(ring[1][0])->placement.translation.x == Approx(-10));

    a.FindPattern(circ)->angle = kPi / 2;   // three over a quarter turn: 0°, 45°, 90°
    a.FindPattern(circ)->count = 3;
    REQUIRE(kernel::ApplyPattern(model, a, circ));
    CHECK(a.FindPattern(circ)->instances.size() == 2);
    CHECK(a.FindComponent(a.FindPattern(circ)->instances[1][0])->placement.translation.y == Approx(10));

    ComponentPattern bad;
    bad.seed = {pin};   // a pattern of holes without a hole
    const ObjectId none = a.AddPattern(bad).id;
    CHECK_FALSE(kernel::ApplyPattern(model, a, none));
}

TEST_CASE("Component pattern: 500 x 500 is refused (the limit is on the instances made)") {
    Assembly a;
    const ObjectId pin = a.AddComponent("pin.bzc", "Штифт").id;
    kernel::AssemblyModel model;
    ComponentPattern r;
    r.kind = PatternKind::Rectangular;
    r.seed = {pin};
    r.count1 = 500, r.spacing1 = 1;
    r.count2 = 500, r.spacing2 = 1;
    const ObjectId rect = a.AddPattern(r).id;
    CHECK_FALSE(kernel::ApplyPattern(model, a, rect));
    CHECK(a.components.size() == 1);
    ComponentPattern c;
    c.kind = PatternKind::Circular;
    c.seed = {pin};
    c.count = 100000;
    CHECK_FALSE(kernel::ApplyPattern(model, a, a.AddPattern(c).id));
}
