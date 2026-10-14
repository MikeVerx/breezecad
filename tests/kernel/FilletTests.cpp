#include "../Fixtures.h"
#include "document/BzcFile.h"
#include "document/EdgeFeature.h"
#include "kernel/Model.h"

#include <doctest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <string>

using namespace bz;
using namespace bz::test;
using doc::CurveKind;
using doc::EdgeReference;
using doc::FeatureState;
using doc::SurfaceKind;

namespace {

constexpr double kBoxVolume = 120.0 * 80.0 * 30.0;

/// The top front edge of a box from z = 0 to 30, front face toward -Y.
EdgeReference TopFrontEdge(double frontY = -40.0) {
    EdgeReference e;
    e.curve = CurveKind::Line;
    e.direction = {1, 0, 0};
    e.middle = {0, frontY, 30};
    e.faces = {{{SurfaceKind::Plane, {0, -1, 0}}, {SurfaceKind::Plane, {0, 0, 1}}}};
    return e;
}

struct Box {
    doc::Document       d;
    doc::SketchFeature* sketch = nullptr;
    Box() {
        sketch = &AddSketch(d);
        AddRect(d, *sketch, {-60, -40}, {60, 40});
        AddExtrude(d, sketch->id, "30");
    }
    template <class F>
    F& Add(const std::string& size) {
        auto f = std::make_unique<F>();
        f->id = d.Ids().Next();
        f->name = d.UniqueFeatureName("Операция");
        f->edges = {TopFrontEdge()};
        std::vector<Parameter*> params;
        f->CollectParameters(params);
        for (Parameter* p : params) p->expression = size;
        auto* raw = f.get();
        d.Insert(std::move(f), d.FeatureCount());
        return *raw;
    }
};

} // namespace

TEST_CASE("fillet rounds a box edge") {
    Box b;
    auto& fillet = b.Add<doc::FilletFeature>("5");
    kernel::Model model;
    model.Rebuild(b.d);
    REQUIRE(fillet.status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(kBoxVolume - (1 - kPi / 4) * 25 * 120).epsilon(1e-4));
}

TEST_CASE("the edge is found again after the box changes size") {
    Box b;
    auto& fillet = b.Add<doc::FilletFeature>("5");
    kernel::Model model;
    model.Rebuild(b.d);
    REQUIRE(fillet.status.state == FeatureState::Ok);

    // Wider and deeper box: the stored middle point is now off, the faces still match.
    for (auto& p : b.sketch->sketch.points)
        if (!p.isOrigin) p.pos = {p.pos.x * 1.5, p.pos.y * 1.25};
    b.d.MarkChanged(0);
    model.Rebuild(b.d);
    REQUIRE(fillet.status.state == FeatureState::Ok);
    const double volume = 180.0 * 100.0 * 30.0 - (1 - kPi / 4) * 25 * 180;
    CHECK(model.Volume() == doctest::Approx(volume).epsilon(1e-4));
}

TEST_CASE("chamfer bevels a box edge") {
    Box b;
    auto& chamfer = b.Add<doc::ChamferFeature>("3");
    kernel::Model model;
    model.Rebuild(b.d);
    REQUIRE(chamfer.status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(kBoxVolume - 0.5 * 9 * 120).epsilon(1e-4));
}

TEST_CASE("a fillet too big is an error and leaves the body as it was") {
    Box b;
    auto& fillet = b.Add<doc::FilletFeature>("50");
    kernel::Model model;
    model.Rebuild(b.d);
    CHECK(fillet.status.state == FeatureState::Error);
    CHECK(model.Volume() == doctest::Approx(kBoxVolume));
}

TEST_CASE("an edge that no longer exists is reported") {
    Box b;
    auto& fillet = b.Add<doc::FilletFeature>("2");
    fillet.edges[0].faces = {{{SurfaceKind::Cylinder, {0, 0, 1}}, {SurfaceKind::Torus, {}}}};
    kernel::Model model;
    model.Rebuild(b.d);
    CHECK(fillet.status.state == FeatureState::Error);
}

TEST_CASE("fillet and chamfer survive a json round trip") {
    Box b;
    b.Add<doc::FilletFeature>("4");
    b.Add<doc::ChamferFeature>("1.5");
    const std::string json = doc::DocumentToJson(b.d);
    Expected<doc::Document> loaded = doc::DocumentFromJson(json);
    REQUIRE(loaded.HasValue());
    CHECK(doc::DocumentToJson(*loaded) == json);
}

// The top front edge: its reference face is the top (normal +Z), the
// other one the front (-Y). Legs: a on the reference face, b on the other.
TEST_CASE("chamfer with two distances measures the first on the upper face") {
    Box b;
    auto& chamfer = b.Add<doc::ChamferFeature>("2");
    chamfer.mode = doc::ChamferMode::TwoDistances;
    chamfer.distance.expression = "2";
    chamfer.distance2.expression = "5";
    kernel::Model model;
    model.Rebuild(b.d);
    REQUIRE(chamfer.status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(kBoxVolume - 0.5 * 2 * 5 * 120).epsilon(1e-4));
    // The cut-off prism (legs a along the top, b down the front) moves the
    // centre of mass: z = (V·15 − v·(30 − b/3)) / (V − v).
    const auto centreZ = [](double top, double down) {
        const double v = 0.5 * top * down * 120;
        return (kBoxVolume * 15 - v * (30 - down / 3)) / (kBoxVolume - v);
    };
    auto props = model.Properties();
    REQUIRE(props);
    CHECK(props->centre.z == doctest::Approx(centreZ(2, 5)).epsilon(1e-6));

    chamfer.flipped = true;   // 2 on the front face now: same volume, other shape
    b.d.MarkChanged(2);
    model.Rebuild(b.d);
    REQUIRE(chamfer.status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(kBoxVolume - 0.5 * 2 * 5 * 120).epsilon(1e-4));
    props = model.Properties();
    REQUIRE(props);
    CHECK(props->centre.z == doctest::Approx(centreZ(5, 2)).epsilon(1e-6));
}

TEST_CASE("chamfer by distance and angle: any angle, measured from the reference face") {
    for (const double degrees : {15.0, 30.0, 60.0, 75.0}) {
        Box b;
        auto& chamfer = b.Add<doc::ChamferFeature>("3");
        chamfer.mode = doc::ChamferMode::DistanceAngle;
        chamfer.angle.expression = std::to_string(degrees);
        kernel::Model model;
        model.Rebuild(b.d);
        REQUIRE(chamfer.status.state == FeatureState::Ok);
        // 3 on the top face; the bevel at `degrees` to the top: the leg down
        // the front face is 3·tan(angle).
        const double other = 3 * std::tan(degrees * kPi / 180);
        CHECK(model.Volume() == doctest::Approx(kBoxVolume - 0.5 * 3 * other * 120).epsilon(1e-4));
    }
}

TEST_CASE("chamfer angle of 90° or more is an error") {
    Box b;
    auto& chamfer = b.Add<doc::ChamferFeature>("3");
    chamfer.mode = doc::ChamferMode::DistanceAngle;
    chamfer.angle.expression = "90";
    kernel::Model model;
    model.Rebuild(b.d);
    CHECK(chamfer.status.state == FeatureState::Error);
}

TEST_CASE("a chamfer from an old file (no mode) keeps equal distances") {
    const char* json = R"({"type":"chamfer","id":7,"name":"Фаска1","distance":{"name":"d3","expression":"2","quantity":"length","value":2},"edges":[]})";
    doc::ChamferFeature f;
    REQUIRE(f.LoadFields(nlohmann::json::parse(json)).IsOk());
    CHECK(f.mode == doc::ChamferMode::EqualDistance);
    CHECK(f.distance.expression == "2");
    CHECK_FALSE(f.flipped);
}

TEST_CASE("chamfer: one edge turned on its own") {
    Box b;
    auto& chamfer = b.Add<doc::ChamferFeature>("2");
    EdgeReference back = TopFrontEdge(40.0);
    back.faces = {{{SurfaceKind::Plane, {0, 1, 0}}, {SurfaceKind::Plane, {0, 0, 1}}}};
    chamfer.edges.push_back(back);
    chamfer.mode = doc::ChamferMode::TwoDistances;
    chamfer.distance2.expression = "5";
    const double v = 0.5 * 2 * 5 * 120;
    // Both legs measured as (along the top, down the side): prisms' centres at 30 − down/3.
    const auto centreZ = [&](double downFront, double downBack) {
        return (kBoxVolume * 15 - v * (30 - downFront / 3) - v * (30 - downBack / 3)) / (kBoxVolume - 2 * v);
    };
    kernel::Model model;
    model.Rebuild(b.d);
    REQUIRE(chamfer.status.state == FeatureState::Ok);
    auto props = model.Properties();
    REQUIRE(props);
    CHECK(props->centre.z == doctest::Approx(centreZ(5, 5)).epsilon(1e-6));

    chamfer.FlipAt(1);   // the back edge only: 5 along the top, 2 down
    CHECK(chamfer.FlippedAt(1));
    CHECK_FALSE(chamfer.FlippedAt(0));
    b.d.MarkChanged(2);
    model.Rebuild(b.d);
    REQUIRE(chamfer.status.state == FeatureState::Ok);
    props = model.Properties();
    REQUIRE(props);
    CHECK(props->centre.z == doctest::Approx(centreZ(5, 2)).epsilon(1e-6));

    chamfer.flipped = true;   // all turned: the back edge back where it began
    CHECK_FALSE(chamfer.FlippedAt(1));
    b.d.MarkChanged(2);
    model.Rebuild(b.d);
    props = model.Properties();
    REQUIRE(props);
    CHECK(props->centre.z == doctest::Approx(centreZ(2, 5)).epsilon(1e-6));

    // Kept in the file.
    const std::string json = doc::DocumentToJson(b.d);
    Expected<doc::Document> loaded = doc::DocumentFromJson(json);
    REQUIRE(loaded.HasValue());
    const auto* read = loaded->FindAs<doc::ChamferFeature>(chamfer.id);
    REQUIRE(read);
    CHECK(read->flippedEdges.size() == 1);
    CHECK_FALSE(read->FlippedAt(1));
    CHECK(read->FlippedAt(0));
}
