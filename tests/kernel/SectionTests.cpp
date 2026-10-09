#include "../Fixtures.h"
#include "document/Origin.h"
#include "document/SectionFeature.h"
#include "kernel/Model.h"

#include <doctest.h>

using namespace bz;
using namespace bz::test;
using doc::FeatureState;

TEST_CASE("section keeps one side of the plane and switches off by suppression") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {-50, -30}, {50, 30});
    AddExtrude(d, sk.id, "40");
    auto section = std::make_unique<doc::SectionFeature>();
    section->id = d.Ids().Next();
    section->name = "Сечение1";
    section->plane = doc::origin::kPlaneYZ;   // x = 0, normal +X
    auto* f = section.get();
    d.Insert(std::move(section), d.FeatureCount());

    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(f->status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(50.0 * 60 * 40));   // the +X half is cut away

    f->flipped = true;
    d.MarkChanged(2);
    model.Rebuild(d);
    CHECK(model.Volume() == doctest::Approx(50.0 * 60 * 40));

    f->suppressed = true;
    d.MarkChanged(2);
    model.Rebuild(d);
    CHECK(model.Volume() == doctest::Approx(100.0 * 60 * 40));
}
