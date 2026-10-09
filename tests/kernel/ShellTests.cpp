#include "../Fixtures.h"
#include "document/BzcFile.h"
#include "document/ShellFeature.h"
#include "kernel/Model.h"

#include <doctest.h>

using namespace bz;
using namespace bz::test;
using doc::FeatureState;

namespace {

/// A 100 x 60 x 40 box with a shell after it.
struct Box {
    doc::Document         d;
    doc::ExtrudeFeature*  extrude = nullptr;
    doc::ShellFeature*    shell = nullptr;
    Box() {
        auto& sk = AddSketch(d);
        AddRect(d, sk, {0, 0}, {100, 60});
        extrude = &AddExtrude(d, sk.id, "40");
        auto s = std::make_unique<doc::ShellFeature>();
        s->id = d.Ids().Next();
        s->name = d.UniqueFeatureName("Оболочка");
        shell = s.get();
        d.Insert(std::move(s), d.FeatureCount());
    }
};

constexpr doc::FaceReference kTop{{0, 0, 1}, {30, 20, 40}};

} // namespace

TEST_CASE("shell with an open top makes a tray") {
    Box b;
    b.shell->openFaces = {kTop};
    b.shell->thickness.expression = "2";
    kernel::Model model;
    model.Rebuild(b.d);
    REQUIRE(b.shell->status.state == FeatureState::Ok);
    // Removed: the inside, 96 x 56 x 38 (open at the top).
    CHECK(model.Volume() == doctest::Approx(100.0 * 60 * 40 - 96.0 * 56 * 38).epsilon(1e-6));
}

TEST_CASE("closed shell and outward walls") {
    Box b;
    b.shell->thickness.expression = "2";
    kernel::Model model;
    model.Rebuild(b.d);
    REQUIRE(b.shell->status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(100.0 * 60 * 40 - 96.0 * 56 * 36).epsilon(1e-6));

    // Outward with an open top: the box becomes the cavity. Walls grow out
    // on the sides and the bottom; edges are rounded by the offset.
    b.shell->openFaces = {kTop};
    b.shell->outward = true;
    b.d.MarkChanged(2);
    model.Rebuild(b.d);
    REQUIRE(b.shell->status.state == FeatureState::Ok);
    const auto p = model.Properties();
    REQUIRE(p);
    CHECK(p->bounds.min.x == doctest::Approx(-2.0));
    CHECK(p->bounds.min.z == doctest::Approx(-2.0));
    CHECK(p->bounds.max.z == doctest::Approx(40.0));
}

TEST_CASE("the open face follows the part when it gets taller") {
    Box b;
    b.shell->openFaces = {kTop};
    kernel::Model model;
    model.Rebuild(b.d);
    REQUIRE(b.shell->status.state == FeatureState::Ok);

    b.extrude->distance.expression = "50";
    b.d.MarkChanged(1);
    model.Rebuild(b.d);
    REQUIRE(b.shell->status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(100.0 * 60 * 50 - 96.0 * 56 * 48).epsilon(1e-6));
}

TEST_CASE("shell errors and file round trip") {
    Box b;
    b.shell->thickness.expression = "0";
    kernel::Model model;
    model.Rebuild(b.d);
    CHECK(b.shell->status.state == FeatureState::Error);

    b.shell->thickness.expression = "3";
    b.shell->openFaces = {kTop, {{0, -1, 0}, {50, 0, 20}}};
    b.shell->outward = true;
    const auto dir = std::filesystem::temp_directory_path() / "bz_shell_tests";
    std::filesystem::create_directories(dir);
    const auto file = dir / "shell.bzc";
    REQUIRE(doc::SaveBzc(b.d, file, "tests").IsOk());
    auto loaded = doc::LoadBzc(file);
    REQUIRE(loaded);
    const auto* s = loaded->FindAs<doc::ShellFeature>(b.shell->id);
    REQUIRE(s);
    CHECK(s->name == b.shell->name);
    CHECK(s->openFaces == b.shell->openFaces);
    CHECK(s->outward);
    CHECK(s->thickness.expression == "3");
    std::filesystem::remove_all(dir);
}
