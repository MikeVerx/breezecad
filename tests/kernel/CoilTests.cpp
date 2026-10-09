#include "../Fixtures.h"
#include "document/CoilFeature.h"
#include "kernel/Model.h"

#include <doctest.h>

#include <cmath>

using namespace bz;
using namespace bz::test;
using doc::FeatureState;

TEST_CASE("coil winds a round wire along a helix") {
    doc::Document d;
    auto coil = std::make_unique<doc::CoilFeature>();
    coil->id = d.Ids().Next();
    coil->name = "Пружина1";
    auto* f = coil.get();
    d.Insert(std::move(coil), d.FeatureCount());

    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(f->status.state == FeatureState::Ok);
    // Wire area times the helix length: 6 turns of radius 10, pitch 5, wire 2.
    const double helix = 6 * std::hypot(2 * kPi * 10, 5.0);
    CHECK(model.Volume() == doctest::Approx(kPi * 1.0 * helix).epsilon(1e-2));

    f->wire.expression = "6";   // thicker than the pitch: turns would overlap
    d.MarkChanged(0);
    model.Rebuild(d);
    CHECK(f->status.state == FeatureState::Error);
}

TEST_CASE("coil cut from a body leaves a groove") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {-30, -30}, {30, 30});
    AddExtrude(d, sk.id, "20");
    auto coil = std::make_unique<doc::CoilFeature>();
    coil->id = d.Ids().Next();
    coil->name = "Пружина1";
    coil->operation = doc::BooleanOp::Cut;
    coil->turns.expression = "2";
    coil->start.expression = "5";   // the whole wire inside the 20 mm block
    auto* f = coil.get();
    d.Insert(std::move(coil), d.FeatureCount());

    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(f->status.state == FeatureState::Ok);
    const double helix = 2 * std::hypot(2 * kPi * 10, 5.0);
    CHECK(60.0 * 60 * 20 - model.Volume() == doctest::Approx(kPi * helix).epsilon(1e-2));
}
