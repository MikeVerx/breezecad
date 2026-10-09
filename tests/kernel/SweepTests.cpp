#include "../Fixtures.h"
#include "core/PathText.h"
#include "document/BzcFile.h"
#include "document/SweepFeature.h"
#include "kernel/Model.h"

#include <doctest.h>

#include <cstdlib>

using namespace bz;
using namespace bz::test;
using doc::FeatureState;

namespace {

/// A Ø4 circle on XZ at the origin, a path sketch on XY, and a sweep.
struct Pipe {
    doc::Document        d;
    doc::SketchFeature*  path = nullptr;
    doc::SweepFeature*   sweep = nullptr;
    Pipe() {
        auto& profile = AddSketch(d, doc::origin::kPlaneXZ);
        AddCircle(d, profile, {0, 0}, 2);
        path = &AddSketch(d);
        auto s = std::make_unique<doc::SweepFeature>();
        s->id = d.Ids().Next();
        s->name = "Сдвиг1";
        s->sketch = profile.id;
        s->path = path->id;
        sweep = s.get();
        d.Insert(std::move(s), d.FeatureCount());
    }
    void Line(Vec2 a, Vec2 b) {
        auto& sk = path->sketch;
        sk.AddLine(d.Ids(), sk.AddPoint(d.Ids(), a), sk.AddPoint(d.Ids(), b));
    }
};

} // namespace

TEST_CASE("sweep along a straight path is a cylinder") {
    Pipe p;
    p.Line({0, 0}, {0, 50});
    kernel::Model model;
    model.Rebuild(p.d);
    REQUIRE(p.sweep->status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(kPi * 4 * 50).epsilon(1e-4));
}

TEST_CASE("sweep along a bent path") {
    Pipe p;
    p.Line({0, 0}, {0, 50});
    p.Line({0, 50}, {30, 80});   // a 45° bend
    kernel::Model model;
    model.Rebuild(p.d);
    REQUIRE(p.sweep->status.state == FeatureState::Ok);
    CHECK(model.Volume() > kPi * 4 * 50);
    if (const char* out = std::getenv("BZ_SAMPLE_OUT")) REQUIRE(doc::SaveBzc(p.d, PathFromUtf8(out), "tests").IsOk());
}

TEST_CASE("sweep path must be one chain") {
    Pipe p;
    p.Line({0, 0}, {0, 50});
    p.Line({10, 0}, {10, 50});   // a second, separate line
    kernel::Model model;
    model.Rebuild(p.d);
    CHECK(p.sweep->status.state == FeatureState::Error);

    p.sweep->path = {};
    p.d.MarkChanged(2);
    model.Rebuild(p.d);
    CHECK(p.sweep->status.state == FeatureState::Error);
}
