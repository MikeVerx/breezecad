#include "../Fixtures.h"
#include "assembly/Solver.h"
#include "document/BzcFile.h"
#include "kernel/AutoDrop.h"

#include <doctest.h>

#include <filesystem>

using namespace bz;
using namespace bz::assembly;
using doctest::Approx;
namespace parts = bz::doc::parts;

namespace {

/// 40 x 40 plate from z = 0 up, with a round hole of `hole` diameter at (20, 20).
doc::Document HolePlate(const std::string& thickness, double hole) {
    doc::Document d;
    auto& sk = test::AddSketch(d);
    test::AddRect(d, sk, {0, 0}, {40, 40});
    test::AddCircle(d, sk, {20, 20}, hole / 2);
    test::AddExtrude(d, sk.id, thickness);
    return d;
}

/// Two plates, 10 and 5 thick, the thin one under the thick one; holes ⌀11.
struct Stack {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "bz_autodrop";
    std::filesystem::path assemblyFile = dir / "unit.bza";
    Assembly              a;
    ObjectId              top, bottom;
    kernel::AssemblyModel model;

    Stack() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        REQUIRE(doc::SaveBzc(HolePlate("10", 11), dir / "top.bzc", "tests"));
        REQUIRE(doc::SaveBzc(HolePlate("5", 11), dir / "bottom.bzc", "tests"));
        Component& t = a.AddComponent("top.bzc", "Плита");
        t.grounded = true;
        top = t.id;
        Component& b = a.AddComponent("bottom.bzc", "Планка", {Rotation{}, {0, 0, -5}});
        b.grounded = true;
        bottom = b.id;
        model.Update(a, assemblyFile);
    }
    ~Stack() { std::filesystem::remove_all(dir); }

    GeometryRef TopRim() const {
        auto rim = model.GeometryAt(a, top, {25.5, 20, 10}, kernel::AssemblyPick::Circle);
        REQUIRE(rim);
        return *rim;
    }
};

} // namespace

TEST_CASE("AutoDrop: size by the hole, length from the table") {
    const parts::StandardInfo* bolt = parts::FindStandard("gost7798");
    REQUIRE(bolt);
    CHECK(parts::SizeForHole(*bolt, 11) == "М10");
    CHECK(parts::SizeForHole(*bolt, 10) == "М10");    // a tight hole still takes it
    CHECK(parts::SizeForHole(*bolt, 9) == "М8");
    CHECK(parts::SizeForHole(*bolt, 2).empty());      // smaller than the smallest
    CHECK(parts::SizeForHole(*bolt, 40).empty());     // far too wide for any
    CHECK(parts::LengthFor(*bolt, "М10", 28) == Approx(28));
    CHECK(parts::LengthFor(*bolt, "М10", 28.5) == Approx(30));
    CHECK(parts::LengthFor(*bolt, "М10", 1000) == 0);
    CHECK(parts::HeightOf(*parts::FindStandard("gost5915"), "М10") == Approx(8));
    CHECK(parts::HeightOf(*parts::FindStandard("gost11371"), "10") == Approx(2));
    CHECK_FALSE(parts::SeatOf({"iso10642", "M10", 30}));   // countersunk: not this way yet

    // A seat put onto a rim facing +X: the seat's normal turns to -X.
    const parts::Seat seat{{0, 0, 0}, {0, 0, -1}, 5};
    const Placement p = kernel::SeatOnRim(seat, {1, 2, 3}, {1, 0, 0});
    CHECK((p.Point({0, 0, 0}) - Vec3{1, 2, 3}).Length() < 1e-9);
    CHECK(p.Direction({0, 0, -1}).Dot({1, 0, 0}) == Approx(-1));
}

TEST_CASE("AutoDrop: the stack under a hole goes through touching components") {
    Stack s;
    const GeometryRef rim = s.TopRim();
    CHECK(rim.radius == Approx(5.5));
    const auto stack = s.model.StackAt(s.a, rim);
    REQUIRE(stack);
    CHECK(stack->thickness == Approx(15).epsilon(1e-4));
    CHECK(stack->last == s.bottom);
    REQUIRE(stack->farRim.IsSet());
    CHECK(stack->farRim.component == s.bottom);
    CHECK(stack->farRim.radius == Approx(5.5));
}

TEST_CASE("AutoDrop: bolt, washer and nut placed and tied by Вставка") {
    Stack s;
    const GeometryRef rim = s.TopRim();
    auto plan = kernel::PlanAutoDrop(s.model, s.a, rim, {"gost7798", "gost11371", "gost5915"});
    REQUIRE_MESSAGE(plan, (plan ? "" : plan.GetError().message));
    CHECK(plan->size == "М10");
    CHECK(plan->stack == Approx(15).epsilon(1e-4));
    CHECK(plan->length == Approx(28));   // 15 + washer 2 + nut 8 + 2 pitches 3 = 28 (in the ГОСТ row)
    REQUIRE(plan->items.size() == 3);
    CHECK(plan->items[0].request.standard == "gost11371");
    CHECK(plan->items[1].seatOn == 0);
    CHECK(plan->items[2].insert.first.component == s.bottom);

    // What the application does: make the parts, add components and constraints.
    std::vector<ObjectId> ids;
    for (const kernel::DropItem& item : plan->items) {
        auto made = parts::MakePart(item.request);
        REQUIRE(made);
        const std::string file = parts::Designation(item.request) + ".bzc";
        REQUIRE(doc::SaveBzc(made->doc, s.dir / file, "tests"));
        Component& c = s.a.AddComponent(file, "Изделие", item.placement);
        Constraint k = item.insert;
        if (item.seatOn >= 0) k.first.component = ids[std::size_t(item.seatOn)];
        k.second.component = c.id;
        ids.push_back(c.id);
        s.a.AddConstraint(k);
    }
    // Already in place: every Вставка holds before solving.
    for (const Constraint& c : s.a.constraints) CHECK(ConstraintError(s.a, c) < 1e-6);
    const SolveResult solved = Solve(s.a);
    CHECK(solved.converged);
    CHECK_FALSE(solved.moved);

    s.a.Touch();
    s.model.Update(s.a, s.assemblyFile);
    const auto washer = s.model.ComponentBounds(ids[0]);
    const auto bolt = s.model.ComponentBounds(ids[1]);
    const auto nut = s.model.ComponentBounds(ids[2]);
    REQUIRE(washer);
    REQUIRE(bolt);
    REQUIRE(nut);
    CHECK(washer->min.z == Approx(10).epsilon(1e-3));             // on the plate
    CHECK(washer->max.z == Approx(12).epsilon(1e-3));
    CHECK(bolt->min.z == Approx(12 - 28).epsilon(1e-3));           // head on the washer, shank down
    CHECK((bolt->min.x + bolt->max.x) / 2 == Approx(20).epsilon(1e-3));   // on the hole axis
    CHECK((bolt->min.y + bolt->max.y) / 2 == Approx(20).epsilon(1e-3));
    CHECK(nut->max.z == Approx(-5).epsilon(1e-3));                 // under the thin plate
    CHECK(nut->min.z == Approx(-13).epsilon(1e-3));
    // The bolt and the nut share no material with the plates.
    for (const kernel::ComponentClash& clash : s.model.Clashes()) {
        const bool plate = clash.first == s.top || clash.second == s.top || clash.first == s.bottom || clash.second == s.bottom;
        CHECK_FALSE(plate);
    }
}

TEST_CASE("AutoDrop: without a nut the screw just goes through the stack") {
    Stack s;
    auto plan = kernel::PlanAutoDrop(s.model, s.a, s.TopRim(), {"iso4762", "", ""});
    REQUIRE(plan);
    CHECK(plan->size == "M10");
    CHECK(plan->length == Approx(16));   // 15 -> 16 (ISO row)
    REQUIRE(plan->items.size() == 1);
    CHECK(plan->items[0].seatOn == -1);
    CHECK(plan->items[0].insert.first.component == s.top);

    auto unknown = kernel::PlanAutoDrop(s.model, s.a, s.TopRim(), {"nope", "", ""});
    CHECK_FALSE(unknown);
}
