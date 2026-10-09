#include "../Fixtures.h"
#include "document/BzcFile.h"
#include "document/EdgeFeature.h"
#include "document/HoleFeature.h"
#include "document/PatternFeature.h"
#include "document/WorkAxisFeature.h"
#include "document/WorkPlaneFeature.h"
#include "document/WorkPlaneFeature.h"
#include "kernel/Model.h"

#include <doctest.h>

using namespace bz;
using namespace bz::test;
using doc::FeatureState;

namespace {

/// A 100 x 60 x 10 plate from the origin, with one Ø6 through hole at (10, 10).
struct Plate {
    doc::Document      d;
    doc::HoleFeature*  hole = nullptr;
    Plate() {
        auto& sk = AddSketch(d);
        AddRect(d, sk, {0, 0}, {100, 60});
        AddExtrude(d, sk.id, "10");
        auto h = std::make_unique<doc::HoleFeature>();
        h->id = d.Ids().Next();
        h->name = d.UniqueFeatureName("Отверстие");
        h->face = {{0, 0, 1}, {50, 30, 10}};
        h->centres = {{10, 10}};
        h->throughAll = true;
        h->diameter.expression = "6";
        hole = h.get();
        d.Insert(std::move(h), d.FeatureCount());
    }
    template <class T>
    T& Add() {
        auto f = std::make_unique<T>();
        f->id = d.Ids().Next();
        f->name = d.UniqueFeatureName("Копия");
        auto* raw = f.get();
        d.Insert(std::move(f), d.FeatureCount());
        return *raw;
    }
};

constexpr double kPlate = 100.0 * 60 * 10;
constexpr double kHole = kPi * 9 * 10;   // Ø6, 10 deep

} // namespace

TEST_CASE("rectangular pattern of a hole in two directions") {
    Plate p;
    auto& pattern = p.Add<doc::PatternFeature>();
    pattern.sources = {p.hole->id};
    pattern.count1.expression = "4";
    pattern.spacing1.expression = "25";
    pattern.useDirection2 = true;
    pattern.count2.expression = "2";
    pattern.spacing2.expression = "40";
    kernel::Model model;
    model.Rebuild(p.d);
    REQUIRE(pattern.status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(kPlate - 8 * kHole).epsilon(1e-6));

    // The hole gets bigger: the copies follow.
    p.hole->diameter.expression = "8";
    p.d.MarkChanged(2);
    model.Rebuild(p.d);
    CHECK(model.Volume() == doctest::Approx(kPlate - 8 * kPi * 16 * 10).epsilon(1e-6));
}

TEST_CASE("circular pattern over part of a turn") {
    Plate p;
    // Around Z through the origin: a hole at (50, 5) is copied 20 and 40
    // degrees further (first and last at the ends of the angle), all apart
    // and inside the plate.
    p.hole->centres = {{50, 5}};
    auto& circular = p.Add<doc::PatternFeature>();
    circular.kind = doc::PatternKind::Circular;
    circular.sources = {p.hole->id};
    circular.circularCount.expression = "3";
    circular.angle.expression = "40";
    kernel::Model model;
    model.Rebuild(p.d);
    REQUIRE(circular.status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(kPlate - 3 * kHole).epsilon(1e-6));
}

TEST_CASE("mirror about a work plane through the middle") {
    Plate p;
    auto& middle = p.Add<doc::WorkPlaneFeature>();
    middle.base = doc::origin::kPlaneYZ;   // x = 50
    middle.offset.expression = "50";
    auto& mirror = p.Add<doc::MirrorFeature>();
    mirror.sources = {p.hole->id};
    mirror.plane = middle.id;
    kernel::Model model;
    model.Rebuild(p.d);
    REQUIRE(mirror.status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(kPlate - 2 * kHole).epsilon(1e-6));
    const auto props = model.Properties();
    REQUIRE(props);
    CHECK(props->centre.x == doctest::Approx(50.0));   // symmetric again

    mirror.plane = {};
    p.d.MarkChanged(4);
    model.Rebuild(p.d);
    CHECK(mirror.status.state == FeatureState::Error);
}

TEST_CASE("copies need copyable sources and sane counts") {
    Plate p;
    auto& fillet = p.Add<doc::FilletFeature>();
    auto& pattern = p.Add<doc::PatternFeature>();
    pattern.sources = {fillet.id};
    kernel::Model model;
    model.Rebuild(p.d);
    CHECK(pattern.status.state == FeatureState::Error);

    pattern.sources = {p.hole->id};
    pattern.count1.expression = "2.5";
    p.d.MarkChanged(3);
    model.Rebuild(p.d);
    CHECK(pattern.status.state == FeatureState::Error);

    pattern.sources.clear();
    pattern.count1.expression = "2";
    p.d.MarkChanged(3);
    model.Rebuild(p.d);
    CHECK(pattern.status.state == FeatureState::Error);
}

TEST_CASE("pattern and mirror file round trip") {
    Plate p;
    auto& pattern = p.Add<doc::PatternFeature>();
    pattern.sources = {p.hole->id};
    pattern.kind = doc::PatternKind::Circular;
    pattern.axis = doc::ModelAxis::X;
    pattern.direction1 = doc::ModelAxis::Z;
    pattern.reversed2 = true;
    pattern.circularCount.expression = "5";
    auto& mirror = p.Add<doc::MirrorFeature>();
    mirror.sources = {p.hole->id, pattern.id};
    mirror.plane = doc::origin::kPlaneXZ;

    const auto dir = std::filesystem::temp_directory_path() / "bz_pattern_tests";
    std::filesystem::create_directories(dir);
    const auto file = dir / "pattern.bzc";
    REQUIRE(doc::SaveBzc(p.d, file, "tests").IsOk());
    auto loaded = doc::LoadBzc(file);
    REQUIRE(loaded);
    const auto* lp = loaded->FindAs<doc::PatternFeature>(pattern.id);
    const auto* lm = loaded->FindAs<doc::MirrorFeature>(mirror.id);
    REQUIRE(lp);
    REQUIRE(lm);
    CHECK(lp->sources == pattern.sources);
    CHECK(lp->kind == doc::PatternKind::Circular);
    CHECK(lp->axis == doc::ModelAxis::X);
    CHECK(lp->direction1 == doc::ModelAxis::Z);
    CHECK(lp->reversed2);
    CHECK(lp->circularCount.expression == "5");
    CHECK(lm->sources == mirror.sources);
    CHECK(lm->plane == doc::origin::kPlaneXZ);
    std::filesystem::remove_all(dir);
}

TEST_CASE("pattern placements") {
    doc::Document d;
    doc::PatternFeature f;
    f.kind = doc::PatternKind::Circular;
    f.circularCount.expression = "4";
    f.angle.expression = "360";
    std::vector<Parameter*> params;
    f.CollectParameters(params);
    for (Parameter* p : params) {   // evaluate as the document would
        p->value = std::stod(p->expression) * (p->quantity == Quantity::Angle ? kPi / 180 : 1.0);
    }
    auto copies = f.Copies();
    REQUIRE(copies);
    REQUIRE(copies->size() == 3);   // a full turn does not repeat the original
    CHECK((*copies)[2].turn == doctest::Approx(3 * kPi / 2));

    f.kind = doc::PatternKind::Rectangular;
    f.reversed1 = true;
    copies = f.Copies();
    REQUIRE(copies);
    REQUIRE(copies->size() == 2);   // 3 along X, one row
    CHECK((*copies)[1].offset.x == doctest::Approx(-40.0));
}

TEST_CASE("work axes: two planes, a cylinder; a circular pattern around one") {
    Plate p;
    p.hole->centres = {{50, 10}};
    // Where planes XZ and YZ meet: the Z axis.
    auto& z = p.Add<doc::WorkAxisFeature>();
    z.method = doc::WorkAxisMethod::Planes;
    z.plane1 = doc::origin::kPlaneXZ;
    z.plane2 = doc::origin::kPlaneYZ;
    // The axis of the hole, now at (50, 10).
    auto& holeAxis = p.Add<doc::WorkAxisFeature>();
    holeAxis.cylinder = doc::CylinderTouch{{50, 10, 0}, {0, 0, 1}, 3.0, {}};
    // Three holes around the plate's middle (50, 30), on a work axis there.
    auto& wx = p.Add<doc::WorkPlaneFeature>();
    wx.base = doc::origin::kPlaneYZ;
    wx.offset.expression = "50";
    auto& wy = p.Add<doc::WorkPlaneFeature>();
    wy.base = doc::origin::kPlaneXZ;
    wy.offset.expression = "30";
    wy.reversed = true;   // the XZ plane faces -Y
    auto& middle = p.Add<doc::WorkAxisFeature>();
    middle.method = doc::WorkAxisMethod::Planes;
    middle.plane1 = wx.id;
    middle.plane2 = wy.id;
    auto& circular = p.Add<doc::PatternFeature>();
    circular.kind = doc::PatternKind::Circular;
    circular.sources = {p.hole->id};
    circular.circularCount.expression = "3";
    circular.angle.expression = "360";
    circular.workAxis = middle.id;
    kernel::Model model;
    model.Rebuild(p.d);
    REQUIRE(z.line);
    CHECK(std::abs(z.line->second.z) == doctest::Approx(1.0));
    CHECK(z.line->first.Length() == doctest::Approx(0.0));
    REQUIRE(holeAxis.line);
    CHECK(holeAxis.line->first.x == doctest::Approx(50.0));
    CHECK(holeAxis.line->first.y == doctest::Approx(10.0));
    REQUIRE(middle.line);
    CHECK(middle.line->first.x == doctest::Approx(50.0));
    CHECK(middle.line->first.y == doctest::Approx(30.0));
    REQUIRE(circular.status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(kPlate - 3 * kHole).epsilon(1e-6));
}
