#include "../Fixtures.h"
#include "document/BzcFile.h"
#include "document/HoleFeature.h"
#include "kernel/Model.h"

#include <doctest.h>

using namespace bz;
using namespace bz::test;
using doc::FeatureState;
using doc::HoleType;

namespace {

constexpr double kBox = 120.0 * 80.0 * 30.0;

struct Plate {
    doc::Document         d;
    doc::ExtrudeFeature*  extrude = nullptr;
    Plate() {
        auto& sk = AddSketch(d);
        AddRect(d, sk, {-60, -40}, {60, 40});
        extrude = &AddExtrude(d, sk.id, "30");
    }
    /// A hole of `type` at the centre of the top face.
    doc::HoleFeature& AddHole(HoleType type, bool through, const std::string& diameter = "10") {
        auto h = std::make_unique<doc::HoleFeature>();
        h->id = d.Ids().Next();
        h->name = d.UniqueFeatureName("Отверстие");
        h->face = {{0, 0, 1}, {10, 5, 30}};
        h->centres = {{0, 0}};
        h->type = type;
        h->throughAll = through;
        h->diameter.expression = diameter;
        h->headDiameter.expression = "16";
        h->headDepth.expression = "5";
        h->sinkDiameter.expression = "16";
        auto* raw = h.get();
        d.Insert(std::move(h), d.FeatureCount());
        return *raw;
    }
};

double Cut(const kernel::Model& m) { return kBox - m.Volume(); }

} // namespace

TEST_CASE("blind and through holes") {
    Plate p;
    auto& hole = p.AddHole(HoleType::Simple, false);
    kernel::Model model;
    model.Rebuild(p.d);
    REQUIRE(hole.status.state == FeatureState::Ok);
    CHECK(Cut(model) == doctest::Approx(kPi * 25 * 10).epsilon(1e-4));

    hole.throughAll = true;
    p.d.MarkChanged(2);
    model.Rebuild(p.d);
    REQUIRE(hole.status.state == FeatureState::Ok);
    CHECK(Cut(model) == doctest::Approx(kPi * 25 * 30).epsilon(1e-4));
}

TEST_CASE("counterbore and countersink") {
    Plate p;
    auto& bore = p.AddHole(HoleType::Counterbore, true);
    kernel::Model model;
    model.Rebuild(p.d);
    REQUIRE(bore.status.state == FeatureState::Ok);
    CHECK(Cut(model) == doctest::Approx(kPi * (64 * 5 + 25 * 25)).epsilon(1e-4));

    bore.type = HoleType::Countersink;
    p.d.MarkChanged(2);
    model.Rebuild(p.d);
    REQUIRE(bore.status.state == FeatureState::Ok);
    // 90° cone from R8 to R5 is 3 mm deep: frustum + the cylinder below it.
    CHECK(Cut(model) == doctest::Approx(kPi * (1.0 * (64 + 40 + 25) + 27 * 25)).epsilon(1e-4));
}

TEST_CASE("the hole follows its face when the part gets thicker") {
    Plate p;
    auto& hole = p.AddHole(HoleType::Simple, false);
    p.extrude->distance.expression = "40";
    p.d.MarkChanged(1);
    kernel::Model model;
    model.Rebuild(p.d);
    REQUIRE(hole.status.state == FeatureState::Ok);
    CHECK(120.0 * 80.0 * 40.0 - model.Volume() == doctest::Approx(kPi * 25 * 10).epsilon(1e-4));
    const auto box = model.BoundingBox();
    REQUIRE(box);
    CHECK(box->max.z == doctest::Approx(40.0).epsilon(1e-3));
}

TEST_CASE("bad hole sizes are errors") {
    Plate p;
    auto& hole = p.AddHole(HoleType::Counterbore, false, "20");   // head 16 is narrower than the hole
    kernel::Model model;
    model.Rebuild(p.d);
    CHECK(hole.status.state == FeatureState::Error);
    CHECK(model.Volume() == doctest::Approx(kBox));
}

TEST_CASE("hole survives a json round trip") {
    Plate p;
    p.AddHole(HoleType::Countersink, true).centres.push_back({20, -10});
    const std::string json = doc::DocumentToJson(p.d);
    Expected<doc::Document> loaded = doc::DocumentFromJson(json);
    REQUIRE(loaded.HasValue());
    CHECK(doc::DocumentToJson(*loaded) == json);
}

TEST_CASE("a hole on a side face goes into the body") {
    Plate p;
    auto& hole = p.AddHole(HoleType::Simple, false);
    hole.face = {{1, 0, 0}, {60, -6.75, 10.8}};   // as picked in the viewport
    hole.centres = {{15, 10}};   // model y = 15, z = 10
    kernel::Model model;
    model.Rebuild(p.d);
    REQUIRE(hole.status.state == FeatureState::Ok);
    CHECK(Cut(model) == doctest::Approx(kPi * 25 * 10).epsilon(1e-4));
}
