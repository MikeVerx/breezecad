#include "../Fixtures.h"
#include "document/LoftFeature.h"
#include "document/WorkPlaneFeature.h"
#include "kernel/Model.h"

#include <doctest.h>

using namespace bz;
using namespace bz::test;
using doc::FeatureState;

namespace {

/// A 40 x 40 square on XY and a section on a work plane `height` above.
struct TwoSections {
    doc::Document         d;
    doc::SketchFeature*   bottom = nullptr;
    doc::SketchFeature*   top = nullptr;
    doc::LoftFeature*     loft = nullptr;
    explicit TwoSections(bool circleOnTop) {
        bottom = &AddSketch(d);
        AddRect(d, *bottom, {-20, -20}, {20, 20});
        auto wp = std::make_unique<doc::WorkPlaneFeature>();
        wp->id = d.Ids().Next();
        wp->name = "Плоскость1";
        wp->base = doc::origin::kPlaneXY;
        wp->offset.expression = "30";
        const ObjectId plane = wp->id;
        d.Insert(std::move(wp), d.FeatureCount());
        top = &AddSketch(d, plane);
        if (circleOnTop) AddCircle(d, *top, {0, 0}, 10);
        else AddRect(d, *top, {-20, -20}, {20, 20});
        auto l = std::make_unique<doc::LoftFeature>();
        l->id = d.Ids().Next();
        l->name = "Лофт1";
        l->sections = {bottom->id, top->id};
        loft = l.get();
        d.Insert(std::move(l), d.FeatureCount());
    }
};

} // namespace

TEST_CASE("loft between two equal squares is a prism") {
    TwoSections s(false);
    s.loft->ruled = true;
    kernel::Model model;
    model.Rebuild(s.d);
    REQUIRE(s.loft->status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(40.0 * 40 * 30).epsilon(1e-6));
}

TEST_CASE("loft from a square to a circle") {
    TwoSections s(true);
    kernel::Model model;
    model.Rebuild(s.d);
    REQUIRE(s.loft->status.state == FeatureState::Ok);
    const double v = model.Volume();
    // Between the circle prism and the square prism of the same height.
    CHECK(v > kPi * 100 * 30);
    CHECK(v < 40.0 * 40 * 30);
    CHECK(model.BoundingBox()->max.z == doctest::Approx(30.0).epsilon(1e-4));
}

TEST_CASE("loft needs two sections with one outline each") {
    TwoSections s(true);
    s.loft->sections = {s.bottom->id};
    kernel::Model model;
    model.Rebuild(s.d);
    CHECK(s.loft->status.state == FeatureState::Error);

    s.loft->sections = {s.bottom->id, s.top->id};
    AddCircle(s.d, *s.top, {15, 15}, 2);   // a second outline on the top
    s.d.MarkChanged(0);
    model.Rebuild(s.d);
    CHECK(s.loft->status.state == FeatureState::Error);
}
