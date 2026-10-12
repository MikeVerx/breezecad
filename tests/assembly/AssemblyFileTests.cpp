#include "assembly/AssemblyFile.h"
#include "core/PathText.h"
#include "document/BzcFile.h"
#include "document/DocumentTypes.h"

#include <doctest.h>

#include <filesystem>

using namespace bz;
using namespace bz::assembly;

namespace {

Assembly SampleAssembly() {
    Assembly a;
    a.info = {"Узел крепления", "БРЗ.100.000"};
    Component& base = a.AddComponent("Детали/Основание.bzc", "Основание");
    base.properties = {"БРЗ.100.001", "Основание", "Сталь 20", 1};
    const ObjectId baseId = base.id;   // `base` dangles once the next one is added
    Component& bolt = a.AddComponent("../Стандартные/Болт М8.bzc", "Болт М8",
                                     {Rotation::AxisAngle({0, 0, 1}, kPi / 2), {10, 20, 30}});
    bolt.properties.quantity = 4;
    bolt.visible = false;

    Constraint c;
    c.type   = ConstraintType::Insert;
    c.first  = {baseId, GeometryKind::Circle, {5, 5, 10}, {0, 0, 1}, 4.0, {9, 5, 10}};
    c.second = {bolt.id, GeometryKind::Circle, {0, 0, 0}, {0, 0, -1}, 4.0, {4, 0, 0}};
    c.value  = 1.5;
    c.flipped = true;
    a.AddConstraint(c);
    return a;
}

} // namespace

TEST_CASE("document types are looked up, not assumed") {
    const auto& reg = doc::DocumentTypeRegistry::Default();
    REQUIRE(reg.Find("part"));
    REQUIRE(reg.Find("assembly"));
    REQUIRE(reg.Find("drawing"));
    CHECK(reg.Find("assembly")->canOpen);
    CHECK_FALSE(reg.Find("drawing")->canOpen);   // placeholder until its module exists
    CHECK(reg.Find("nonsense") == nullptr);
    CHECK(reg.Find("part")->extension == ".bzc");
    CHECK(reg.Find("assembly")->extension == ".bza");   // assemblies stand out among part files
    CHECK(reg.OpenPatterns() == "*.bzc *.bza");          // the drawing cannot be opened yet

    doc::DocumentTypeRegistry custom;   // a module may add its own kind
    custom.Register({.id = "schematic", .title = "Схема", .untitledBase = "Схема"});
    custom.Register({.id = "schematic", .title = "Схема электрическая", .untitledBase = "Схема"});
    REQUIRE(custom.All().size() == 1);
    CHECK(custom.Find("schematic")->title == "Схема электрическая");
}

TEST_CASE("assembly JSON round trip") {
    const Assembly a = SampleAssembly();
    const std::string text = AssemblyToJson(a);
    Expected<Assembly> b = AssemblyFromJson(text);
    REQUIRE(b);
    CHECK(b->info.name == "Узел крепления");
    REQUIRE(b->components.size() == 2);
    CHECK(b->components[0].grounded);   // the first one is fixed
    CHECK_FALSE(b->components[1].grounded);
    CHECK(b->components[1].path == "../Стандартные/Болт М8.bzc");
    CHECK(b->components[1].properties.quantity == 4);
    CHECK_FALSE(b->components[1].visible);
    CHECK(b->components[0].properties.material == "Сталь 20");
    const Placement& p = b->components[1].placement;
    CHECK(p.translation.x == doctest::Approx(10));
    CHECK(p.Point({1, 0, 0}).y == doctest::Approx(21));   // rotated 90° about Z
    REQUIRE(b->constraints.size() == 1);
    const Constraint& c = b->constraints[0];
    CHECK(c.type == ConstraintType::Insert);
    CHECK(c.name == "Вставка:1");
    CHECK(c.flipped);
    CHECK(c.value == doctest::Approx(1.5));
    CHECK(c.second.radius == doctest::Approx(4));
    CHECK(c.second.direction.z == doctest::Approx(-1));
    CHECK(AssemblyToJson(*b) == text);   // stable

    // New ids never collide with loaded ones.
    CHECK(b->Ids().Next().value > c.id.value);
}

TEST_CASE("assembly file skips what it cannot read, rejects what is broken") {
    std::vector<std::string> warnings;
    auto a = AssemblyFromJson(R"({"components":[{"id":1,"name":"A:1","path":"a.bzc"}],
        "constraints":[{"id":2,"type":"gear","first":{},"second":{}}]})", &warnings);
    REQUIRE(a);
    CHECK(a->components.size() == 1);
    CHECK(a->constraints.empty());
    CHECK(warnings.size() == 1);

    CHECK_FALSE(AssemblyFromJson(R"({"components":[{"id":1,"name":"A:1"}]})"));   // no part file
    CHECK_FALSE(AssemblyFromJson("not json"));
}

TEST_CASE("assembly is saved in the .bzc container with its type") {
    const auto dir = std::filesystem::temp_directory_path() / PathFromUtf8("bz_сборка_tests");
    std::filesystem::create_directories(dir);
    const auto file = dir / PathFromUtf8("Узел.bza");
    REQUIRE(SaveAssembly(SampleAssembly(), file, "tests"));

    auto type = doc::ReadDocumentType(file);
    REQUIRE(type);
    CHECK(*type == "assembly");
    auto loaded = LoadAssembly(file);
    REQUIRE(loaded);
    CHECK(loaded->components.size() == 2);

    // A part loader refuses it with a clear message, and vice versa.
    auto asPart = doc::LoadBzc(file);
    REQUIRE_FALSE(asPart);
    CHECK(asPart.GetError().message.find("Сборка") != std::string::npos);
    const auto partFile = dir / "part.bzc";
    REQUIRE(doc::SaveBzc(doc::Document{}, partFile, "tests"));
    CHECK(*doc::ReadDocumentType(partFile) == "part");
    CHECK_FALSE(LoadAssembly(partFile));
    // The kind comes from the content, not the name: a renamed file still opens.
    const auto renamed = dir / "renamed.bzc";
    std::filesystem::copy_file(file, renamed);
    CHECK(*doc::ReadDocumentType(renamed) == "assembly");
    std::filesystem::remove_all(dir);
}

TEST_CASE("component paths are relative to the assembly") {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "bz_paths";
    const auto assemblyFile = root / "asm" / "unit.bzc";
    CHECK(RelativePartPath(assemblyFile, root / "asm" / "parts" / "a.bzc") == "parts/a.bzc");
    CHECK(RelativePartPath(assemblyFile, root / "lib" / "b.bzc") == "../lib/b.bzc");
    CHECK(ResolvePartPath(assemblyFile, "../lib/b.bzc") == (root / "lib" / "b.bzc").lexically_normal());

    // Unsaved assembly: absolute paths, made relative when it gets a file.
    Assembly a;
    a.AddComponent(RelativePartPath({}, root / "lib" / "b.bzc"), "B");
    CHECK(PathFromUtf8(a.components[0].path).is_absolute());
    RebasePaths(a, {}, assemblyFile);
    CHECK(a.components[0].path == "../lib/b.bzc");
    // Moving the assembly to another folder keeps pointing at the same part.
    RebasePaths(a, assemblyFile, root / "asm" / "sub" / "unit.bzc");
    CHECK(a.components[0].path == "../../lib/b.bzc");
}

TEST_CASE("removing a component removes its constraints") {
    Assembly a = SampleAssembly();
    const ObjectId bolt = a.components[1].id;
    CHECK(a.ConstraintsOf(bolt).size() == 1);
    a.RemoveComponent(bolt);
    CHECK(a.components.size() == 1);
    CHECK(a.constraints.empty());
    // Names keep counting from what is free.
    CHECK(a.AddComponent("x.bzc", "Основание").name == "Основание:2");
}

TEST_CASE("joint: lock, position, limits and frame X survive a round trip") {
    Assembly a = SampleAssembly();
    Constraint j;
    j.type   = ConstraintType::Rotational;
    j.first  = {a.components[0].id, GeometryKind::Circle, {0, 0, 10}, {0, 0, 1}, 5.0, {5, 0, 10}};
    j.second = {a.components[1].id, GeometryKind::Circle, {0, 0, 0}, {0, 0, -1}, 5.0, {5, 0, 0}, {0, 1, 0}};
    j.locked = true;
    j.position = 0.5;
    j.limits = {true, -1.0, 1.25};
    a.AddConstraint(j);
    const std::string text = AssemblyToJson(a);
    Expected<Assembly> b = AssemblyFromJson(text);
    REQUIRE(b);
    REQUIRE(b->constraints.size() == 2);
    const Constraint& c = b->constraints[1];
    CHECK(c.type == ConstraintType::Rotational);
    CHECK(c.name == "Вращение:1");
    CHECK(c.locked);
    CHECK(c.position == doctest::Approx(0.5));
    CHECK(c.limits == JointLimits{true, -1.0, 1.25});
    CHECK(c.second.reference.y == doctest::Approx(1));
    CHECK(b->constraints[0].second.reference.Length() == 0);   // not written for plain constraints
    CHECK(AssemblyToJson(*b) == text);
}

TEST_CASE("an assembly keeps its own appearances and their pictures") {
    Assembly a = SampleAssembly();
    const ObjectId base = a.components.front().id, bolt = a.components.back().id;
    doc::Appearance red;
    red.id = "user.red";
    red.name = "Красный";
    red.face = Color::Hex(0xE5484D);
    doc::Appearance logo;
    logo.id = "user.logo";
    logo.name = "Лого";
    logo.picture = "user.logo.png";
    a.look.SetBody(base, &red);
    a.look.SetFace(bolt, doc::FaceReference{{0, 0, 1}, {1, 2, 3}, true}, &logo);
    a.look.pictures["user.logo.png"] = {1, 2, 3, 4};
    a.look.pictures["unused.png"] = {9};   // not used by any look: not saved

    // JSON (undo steps): the looks without the bytes.
    auto copy = AssemblyFromJson(AssemblyToJson(a));
    REQUIRE(copy);
    CHECK(copy->look.OfBody(base) != nullptr);
    REQUIRE(copy->look.faces.size() == 1);
    CHECK(copy->look.faces.front().body == bolt);
    CHECK(copy->look.faces.front().face.point == Vec3{1, 2, 3});
    CHECK(copy->look.pictures.empty());

    // The file: the pictures in use come with it.
    const auto dir = std::filesystem::temp_directory_path() / PathFromUtf8("bz_сборка_вид_tests");
    std::filesystem::create_directories(dir);
    const auto file = dir / PathFromUtf8("Узел.bza");
    REQUIRE(SaveAssembly(a, file, "tests"));
    auto loaded = LoadAssembly(file);
    REQUIRE(loaded);
    CHECK(loaded->look.OfBody(base)->id == "user.red");
    REQUIRE(loaded->look.pictures.contains("user.logo.png"));
    CHECK(loaded->look.pictures.at("user.logo.png") == std::vector<std::uint8_t>{1, 2, 3, 4});
    CHECK_FALSE(loaded->look.pictures.contains("unused.png"));

    // An assembly without looks writes none (older versions read it as before).
    CHECK(AssemblyToJson(SampleAssembly()).find("\"look\"") == std::string::npos);
    std::filesystem::remove_all(dir);
}
