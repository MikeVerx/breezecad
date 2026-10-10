#include "../Fixtures.h"
#include "document/ShellFeature.h"
#include "kernel/Model.h"

#include <doctest.h>

using namespace bz;
using namespace bz::test;
using doc::FeatureState;

namespace {

/// A 20 mm cube from z = 0 to 20 with a shell after it.
struct Cube {
    doc::Document      d;
    doc::ShellFeature* shell = nullptr;
    doc::ExtrudeFeature* extrude = nullptr;
    explicit Cube(const std::string& thickness, bool openTop, bool outward = false) {
        auto& sk = AddSketch(d);
        AddRect(d, sk, {-10, -10}, {10, 10});
        extrude = &AddExtrude(d, sk.id, "20");
        auto f = std::make_unique<doc::ShellFeature>();
        f->id = d.Ids().Next();
        f->name = d.UniqueFeatureName("Оболочка");
        f->thickness.expression = thickness;
        f->outward = outward;
        if (openTop) f->faces.push_back(doc::FaceReference{{0, 0, 1}, {0, 0, 20}});
        shell = f.get();
        d.Insert(std::move(f), d.FeatureCount());
    }
};

} // namespace

TEST_CASE("shell with the top removed leaves a box open at the top") {
    Cube c("2", true);
    kernel::Model model;
    model.Rebuild(c.d);
    REQUIRE(c.shell->status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(8000.0 - 16.0 * 16.0 * 18.0).epsilon(1e-6));
}

TEST_CASE("shell without removed faces is closed and hollow") {
    Cube c("2", false);
    kernel::Model model;
    model.Rebuild(c.d);
    REQUIRE(c.shell->status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(8000.0 - 16.0 * 16.0 * 16.0).epsilon(1e-6));
}

TEST_CASE("outward shell keeps the inside size") {
    Cube c("2", true, true);
    kernel::Model model;
    model.Rebuild(c.d);
    REQUIRE(c.shell->status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(24.0 * 24.0 * 22.0 - 8000.0).epsilon(1e-6));
}

TEST_CASE("shell follows the top face when the cube gets taller") {
    Cube c("2", true);
    kernel::Model model;
    model.Rebuild(c.d);
    c.extrude->distance.expression = "30";
    c.d.MarkChanged(0);
    model.Rebuild(c.d);
    REQUIRE(c.shell->status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(20.0 * 20.0 * 30.0 - 16.0 * 16.0 * 28.0).epsilon(1e-6));
}

TEST_CASE("too thick a shell is an error, not a crash") {
    Cube c("15", true);
    kernel::Model model;
    model.Rebuild(c.d);
    CHECK(c.shell->status.state == FeatureState::Error);
}
