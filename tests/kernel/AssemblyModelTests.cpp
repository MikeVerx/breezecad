#include "../Fixtures.h"
#include "assembly/AssemblyFile.h"
#include "document/BzcFile.h"
#include "kernel/AssemblyModel.h"
#include "kernel/AssemblyReadApi.h"

#include <doctest.h>

#include <chrono>
#include <cmath>
#include <filesystem>

using namespace bz;
using namespace bz::assembly;
using doctest::Approx;

namespace {

/// 40 x 40 x `thickness` plate from the origin up.
doc::Document Plate(const std::string& thickness = "10") {
    doc::Document d;
    auto& sk = test::AddSketch(d);
    test::AddRect(d, sk, {0, 0}, {40, 40});
    test::AddExtrude(d, sk.id, thickness);
    d.info = {"Плита", "БРЗ.001", "Сталь 3"};
    return d;
}

/// Pin of radius 5, 20 long, along Z from the origin.
doc::Document Pin() {
    doc::Document d;
    auto& sk = test::AddSketch(d);
    test::AddCircle(d, sk, {0, 0}, 5);
    test::AddExtrude(d, sk.id, "20");
    return d;
}

struct Fixture {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "bz_assembly_model";
    std::filesystem::path assemblyFile = dir / "unit.bzc";
    Assembly              a;
    ObjectId              plate, pin;

    Fixture() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir / "parts");
        REQUIRE(doc::SaveBzc(Plate(), dir / "parts" / "plate.bzc", "tests"));
        REQUIRE(doc::SaveBzc(Pin(), dir / "parts" / "pin.bzc", "tests"));
        plate = a.AddComponent("parts/plate.bzc", "Плита").id;
        pin   = a.AddComponent("parts/pin.bzc", "Штифт", {Rotation{}, {100, 0, 0}}).id;
    }
    ~Fixture() { std::filesystem::remove_all(dir); }
};

} // namespace

TEST_CASE("assembly shows each component as a placed body") {
    Fixture f;
    kernel::AssemblyModel m;
    std::vector<std::string> problems;
    m.Update(f.a, f.assemblyFile, &problems);
    CHECK(problems.empty());
    REQUIRE(m.Shown().Bodies().size() == 2);
    CHECK(m.Shown().Bodies()[1].id == f.pin);
    CHECK(m.Shown().Volume() == Approx(40 * 40 * 10 + kPi * 25 * 20).epsilon(1e-3));
    auto box = m.ComponentBounds(f.pin);
    REQUIRE(box);
    CHECK(box->min.x == Approx(95).epsilon(1e-3));
    CHECK(box->max.z == Approx(20).epsilon(1e-3));
    REQUIRE(m.PartOf(f.plate));
    CHECK(m.PartOf(f.plate)->info.material == "Сталь 3");

    // Moving a component moves its body only when the assembly says so.
    const auto revision = m.Shown().Revision();
    m.Update(f.a, f.assemblyFile);
    CHECK(m.Shown().Revision() == revision);   // nothing changed: nothing rebuilt
    f.a.FindComponent(f.pin)->placement = {Rotation::AxisAngle({1, 0, 0}, kPi / 2), {100, 0, 0}};
    f.a.Touch();
    m.Update(f.a, f.assemblyFile);
    box = m.ComponentBounds(f.pin);
    CHECK(box->min.y == Approx(-20).epsilon(1e-3));   // lies along -Y now
    CHECK(box->max.z == Approx(5).epsilon(1e-3));

    // Hidden components are not shown.
    f.a.FindComponent(f.plate)->visible = false;
    f.a.Touch();
    m.Update(f.a, f.assemblyFile);
    CHECK(m.Shown().Bodies().size() == 1);
}

TEST_CASE("geometry for constraints is picked in the part's coordinates") {
    Fixture f;
    kernel::AssemblyModel m;
    m.Update(f.a, f.assemblyFile);

    auto top = m.GeometryAt(f.a, f.plate, {20, 20, 10}, kernel::AssemblyPick::Face);
    REQUIRE(top);
    CHECK(top->kind == GeometryKind::Plane);
    CHECK(top->direction.z == Approx(1));
    CHECK(top->origin.z == Approx(10));

    auto side = m.GeometryAt(f.a, f.pin, {105, 0, 12}, kernel::AssemblyPick::Face);
    REQUIRE(side);
    CHECK(side->kind == GeometryKind::Cylinder);
    CHECK(side->radius == Approx(5));
    CHECK(std::abs(side->direction.z) == Approx(1));
    CHECK(side->origin.x == Approx(0).epsilon(1e-6));   // local: the pin's own axis
    CHECK(side->origin.z == Approx(12));

    auto axis = m.GeometryAt(f.a, f.pin, {105, 0, 12}, kernel::AssemblyPick::Axis);
    REQUIRE(axis);
    CHECK(axis->kind == GeometryKind::Axis);
    CHECK_FALSE(m.GeometryAt(f.a, f.plate, {20, 20, 10}, kernel::AssemblyPick::Axis));   // a plane has none

    auto rim = m.GeometryAt(f.a, f.pin, {105, 0, 0.3}, kernel::AssemblyPick::Circle);
    REQUIRE(rim);
    CHECK(rim->kind == GeometryKind::Circle);
    CHECK(rim->origin.z == Approx(0).epsilon(1e-6));
    CHECK(rim->direction.z == Approx(-1));   // outward of the bottom face
    CHECK(rim->radius == Approx(5));
}

TEST_CASE("components that overlap are reported as clashes") {
    Fixture f;
    kernel::AssemblyModel m;
    m.Update(f.a, f.assemblyFile);
    CHECK(m.Clashes().empty());
    f.a.FindComponent(f.pin)->placement.translation = {20, 20, 5};   // half in the plate
    f.a.Touch();
    m.Update(f.a, f.assemblyFile);
    auto clashes = m.Clashes();
    REQUIRE(clashes.size() == 1);
    CHECK(clashes[0].first == f.plate);
    CHECK(clashes[0].second == f.pin);
    CHECK(clashes[0].volume == Approx(kPi * 25 * 5).epsilon(1e-3));
    CHECK(clashes[0].centre.z == Approx(7.5).epsilon(1e-3));

    f.a.FindComponent(f.pin)->placement.translation = {20, 20, 10};   // standing on it: touching only
    f.a.Touch();
    m.Update(f.a, f.assemblyFile);
    CHECK(m.Clashes().empty());
}

TEST_CASE("a missing part is reported once, a changed one is reloaded") {
    Fixture f;
    f.a.AddComponent("parts/nothing.bzc", "Нет");
    kernel::AssemblyModel m;
    std::vector<std::string> problems;
    m.Update(f.a, f.assemblyFile, &problems);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].find("nothing.bzc") != std::string::npos);
    CHECK(m.Shown().Bodies().size() == 2);
    CHECK_FALSE(m.ProblemOf(f.a.components[2].id).empty());
    problems.clear();
    m.Update(f.a, f.assemblyFile, &problems);
    CHECK(problems.empty());

    // A constraint on the plate's top follows it when the plate gets thicker.
    Constraint c;
    c.type = ConstraintType::Mate;
    c.first = *m.GeometryAt(f.a, f.plate, {20, 20, 10}, kernel::AssemblyPick::Face);
    c.second = *m.GeometryAt(f.a, f.pin, {100, 0, 0}, kernel::AssemblyPick::Face);
    f.a.AddConstraint(c);
    const auto file = f.dir / "parts" / "plate.bzc";
    REQUIRE(doc::SaveBzc(Plate("15"), file, "tests"));
    std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) + std::chrono::seconds(2));
    const auto revision = f.a.Revision();
    m.Update(f.a, f.assemblyFile);
    CHECK(f.a.constraints[0].first.origin.z == Approx(15));
    CHECK(f.a.constraints[0].first.direction.z == Approx(1));
    CHECK(f.a.Revision() != revision);   // to be solved again
    CHECK(m.Shown().Volume() == Approx(40 * 40 * 15 + kPi * 25 * 20).epsilon(1e-3));
}

TEST_CASE("clashes are reported under the components' tree names") {
    Fixture f;
    kernel::AssemblyModel m;
    f.a.FindComponent(f.pin)->placement.translation = {20, 20, 5};
    f.a.FindComponent(f.pin)->name = "Ось:7";   // renamed in the tree
    f.a.Touch();
    m.Update(f.a, f.assemblyFile);
    const auto& bodies = m.Shown().Bodies();
    const auto& found  = m.Shown().Interferences();   // what the "Коллизии" button lists
    REQUIRE(found.size() == 1);
    CHECK(bodies[found[0].first].name == "Плита:1");
    CHECK(bodies[found[0].second].name == "Ось:7");
    CHECK(bodies[found[0].second].id == f.pin);
}

TEST_CASE("read-only API: components, properties from the part, placed bodies") {
    Fixture f;
    f.a.info = {"Узел", "БРЗ.100"};
    f.a.FindComponent(f.pin)->properties.material = "Латунь";
    f.a.FindComponent(f.pin)->properties.quantity = 4;
    f.a.AddComponent("parts/nothing.bzc", "Нет");
    kernel::AssemblyModel m;
    m.Update(f.a, f.assemblyFile);
    const kernel::AssemblyReader read(f.a, m);
    CHECK(read.Info().name == "Узел");

    const auto all = read.Components();
    REQUIRE(all.size() == 3);
    CHECK(all[0].treeName == "Плита:1");
    CHECK(all[0].name == "Плита");          // from the part
    CHECK(all[0].designation == "БРЗ.001");
    CHECK(all[0].material == "Сталь 3");
    CHECK(all[0].grounded);
    CHECK(all[0].hasGeometry);
    CHECK(all[1].name == "pin");            // part has no name: the file's
    CHECK(all[1].material == "Латунь");     // the component's own value wins
    CHECK(all[1].quantity == 4);
    CHECK(all[1].placement.translation.x == Approx(100));
    CHECK_FALSE(all[2].hasGeometry);
    CHECK_FALSE(all[2].problem.empty());
    CHECK_FALSE(read.Component(ObjectId{}).has_value());

    const auto pin = read.BodiesOf(f.pin);
    REQUIRE(pin.has_value());
    CHECK(pin->Volume() == Approx(kPi * 25 * 20).epsilon(1e-3));
    const auto box = pin->BoundingBox();
    REQUIRE(box.has_value());
    CHECK(box->min.x == Approx(95).epsilon(1e-3));
    CHECK(box->max.x == Approx(105).epsilon(1e-3));
    CHECK(box->max.z == Approx(20).epsilon(1e-3));
    CHECK_FALSE(read.BodiesOf(all[2].id).has_value());
    // Reading changes nothing.
    CHECK(m.Shown().Bodies().size() == 2);
}
