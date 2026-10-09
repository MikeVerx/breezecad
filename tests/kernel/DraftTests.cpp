#include "../Fixtures.h"
#include "document/DraftFeature.h"
#include "kernel/Model.h"

#include <doctest.h>

#include <cmath>

using namespace bz;
using namespace bz::test;
using doc::FeatureState;

TEST_CASE("draft tilts a side face about the neutral plane") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {0, 0}, {100, 60});
    AddExtrude(d, sk.id, "40");
    auto draft = std::make_unique<doc::DraftFeature>();
    draft->id = d.Ids().Next();
    draft->name = "Уклон1";
    draft->faces = {{{1, 0, 0}, {100, 30, 20}}};
    draft->plane = doc::origin::kPlaneXY;
    draft->angle.expression = "5";
    auto* f = draft.get();
    d.Insert(std::move(draft), d.FeatureCount());

    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(f->status.state == FeatureState::Ok);
    // A wedge of 40 x 40·tan5° x 60 comes off (or on): half of it as a prism.
    const double wedge = 0.5 * 40 * 40 * std::tan(5 * kPi / 180) * 60;
    CHECK(std::abs(model.Volume() - 100.0 * 60 * 40) == doctest::Approx(wedge).epsilon(1e-3));

    f->angle.expression = "60";   // too steep
    d.MarkChanged(2);
    model.Rebuild(d);
    CHECK(f->status.state == FeatureState::Error);
}
