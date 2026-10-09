#include "../Fixtures.h"
#include "document/CoilFeature.h"
#include "kernel/Model.h"

#include <doctest.h>

#include <cmath>

using namespace bz;
using namespace bz::test;
using doc::FeatureState;

namespace {

/// A sketch with an осевая along the sketch Y axis and a round profile beside it.
doc::CoilFeature& AddCoil(doc::Document& d, double profileX, double profileRadius, const std::string& pitch,
                          const std::string& turns) {
    auto& sk = AddSketch(d, doc::origin::kPlaneXZ);
    sketch::Sketch& s = sk.sketch;
    const ObjectId axis = s.AddLine(d.Ids(), s.AddPoint(d.Ids(), {0, -5}), s.AddPoint(d.Ids(), {0, 50}));
    s.FindLine(axis)->centerline = true;
    AddCircle(d, sk, {profileX, 0}, profileRadius);
    auto coil = std::make_unique<doc::CoilFeature>();
    coil->id = d.Ids().Next();
    coil->name = "Пружина1";
    coil->sketch = sk.id;
    coil->axisLine = axis;
    coil->pitch.expression = pitch;
    coil->turns.expression = turns;
    auto* f = coil.get();
    d.Insert(std::move(coil), d.FeatureCount());
    return *f;
}

} // namespace

TEST_CASE("coil winds a sketch profile around its осевая") {
    doc::Document d;
    auto& f = AddCoil(d, 10, 1, "5", "6");
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(f.status.state == FeatureState::Ok);
    // Section area times the helix length through the profile centre.
    const double helix = 6 * std::hypot(2 * kPi * 10, 5.0);
    CHECK(model.Volume() == doctest::Approx(kPi * 1.0 * helix).epsilon(1e-2));

    f.pitch.expression = "1.5";   // below the profile height: turns would overlap
    d.MarkChanged(1);
    model.Rebuild(d);
    CHECK(f.status.state == FeatureState::Error);

    f.pitch.expression = "5";
    f.axisLine = {};
    d.MarkChanged(1);
    model.Rebuild(d);
    CHECK(f.status.state == FeatureState::Error);   // no axis chosen
}

TEST_CASE("coil profile must not touch the axis") {
    doc::Document d;
    auto& f = AddCoil(d, 0.5, 1, "5", "2");
    kernel::Model model;
    model.Rebuild(d);
    CHECK(f.status.state == FeatureState::Error);
}
