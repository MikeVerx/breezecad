#include "../Fixtures.h"
#include "document/BzcFile.h"
#include "document/WorkPlaneFeature.h"
#include "kernel/Model.h"

#include <doctest.h>

using namespace bz;
using namespace bz::test;
using doc::FeatureState;

namespace {

doc::WorkPlaneFeature& AddWorkPlane(doc::Document& d, const std::string& offset) {
    auto wp = std::make_unique<doc::WorkPlaneFeature>();
    wp->id = d.Ids().Next();
    wp->name = d.UniqueFeatureName("Плоскость");
    wp->base = doc::origin::kPlaneXY;
    wp->offset.expression = offset;
    auto* raw = wp.get();
    d.Insert(std::move(wp), d.FeatureCount());
    return *raw;
}

} // namespace

TEST_CASE("a sketch on a work plane follows its offset") {
    doc::Document d;
    auto& wp = AddWorkPlane(d, "25");
    auto& sk = AddSketch(d, wp.id);
    AddRect(d, sk, {0, 0}, {20, 10});
    AddExtrude(d, sk.id, "5");
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(wp.status.state == FeatureState::Ok);
    REQUIRE(wp.frame);
    CHECK(wp.frame->origin.z == doctest::Approx(25.0));
    auto box = model.BoundingBox();
    REQUIRE(box);
    CHECK(box->min.z == doctest::Approx(25.0).epsilon(1e-4));

    wp.offset.expression = "40";
    d.MarkChanged(0);
    model.Rebuild(d);
    box = model.BoundingBox();
    CHECK(box->min.z == doctest::Approx(40.0).epsilon(1e-4));
    CHECK(model.Volume() == doctest::Approx(1000.0));

    wp.reversed = true;
    d.MarkChanged(0);
    model.Rebuild(d);
    box = model.BoundingBox();
    CHECK(box->min.z == doctest::Approx(-40.0).epsilon(1e-4));
}

TEST_CASE("work plane on a work plane and on a face") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {0, 0}, {20, 10});
    auto& ex = AddExtrude(d, sk.id, "30");
    auto& onFace = AddWorkPlane(d, "5");
    onFace.face = {{0, 0, 1}, {10, 5, 30}};   // top of the box
    auto& onPlane = AddWorkPlane(d, "10");
    onPlane.base = onFace.id;
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(onFace.status.state == FeatureState::Ok);
    REQUIRE(onPlane.status.state == FeatureState::Ok);
    CHECK(onFace.frame->origin.z == doctest::Approx(35.0));
    CHECK(onPlane.frame->origin.z == doctest::Approx(45.0));
    CHECK(onPlane.Dependencies() == std::vector<ObjectId>{onFace.id});

    ex.distance.expression = "50";   // the face moves up, both planes follow
    d.MarkChanged(1);
    model.Rebuild(d);
    CHECK(onFace.frame->origin.z == doctest::Approx(55.0));
    CHECK(onPlane.frame->origin.z == doctest::Approx(65.0));
}

TEST_CASE("work plane errors and file round trip") {
    doc::Document d;
    auto& wp = AddWorkPlane(d, "7");
    wp.base = {};
    kernel::Model model;
    model.Rebuild(d);
    CHECK(wp.status.state == FeatureState::Error);

    wp.base = doc::origin::kPlaneYZ;
    wp.reversed = true;
    const auto dir = std::filesystem::temp_directory_path() / "bz_workplane_tests";
    std::filesystem::create_directories(dir);
    const auto file = dir / "wp.bzc";
    REQUIRE(doc::SaveBzc(d, file, "tests").IsOk());
    auto loaded = doc::LoadBzc(file);
    REQUIRE(loaded);
    const auto* w = loaded->FindAs<doc::WorkPlaneFeature>(wp.id);
    REQUIRE(w);
    CHECK(w->base == doc::origin::kPlaneYZ);
    CHECK(w->reversed);
    CHECK_FALSE(w->face.IsSet());
    CHECK(w->offset.expression == "7");
    std::filesystem::remove_all(dir);
}

TEST_CASE("a sketch on a face moves with the face") {
    doc::Document d;
    auto& base = AddSketch(d);
    AddRect(d, base, {0, 0}, {40, 40});
    auto& plate = AddExtrude(d, base.id, "10");
    // A boss sketched on the top face (as picked at (20, 20, 10)).
    auto& top = AddSketch(d);
    top.placement = {plate.id, {{0, 0, 10}, {1, 0, 0}, {0, 0, 1}}, true, {{0, 0, 1}, {20, 20, 10}}};
    AddRect(d, top, {10, 10}, {20, 20});
    AddExtrude(d, top.id, "5", doc::BooleanOp::Join);
    kernel::Model model;
    model.Rebuild(d);
    CHECK(model.BoundingBox()->max.z == doctest::Approx(15.0).epsilon(1e-4));

    plate.distance.expression = "25";
    d.MarkChanged(1);
    model.Rebuild(d);
    CHECK(top.status.state == FeatureState::Ok);
    CHECK(top.placement.frame.origin.z == doctest::Approx(25.0));
    CHECK(model.BoundingBox()->max.z == doctest::Approx(30.0).epsilon(1e-4));
    CHECK(model.Volume() == doctest::Approx(40.0 * 40 * 25 + 10.0 * 10 * 5));
}
