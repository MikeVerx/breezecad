#include "../Fixtures.h"
#include "document/BzcFile.h"
#include "document/EdgeFeature.h"
#include "kernel/Model.h"

#include <doctest.h>

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
