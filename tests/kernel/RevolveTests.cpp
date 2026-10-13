#include "../Fixtures.h"
#include "document/BzcFile.h"
#include "kernel/Model.h"
#include "kernel/occt/Extrude.h"

#include <doctest.h>

using namespace bz;
using namespace bz::test;
using doc::FeatureState;
using doc::RevolveAxisKind;
using doc::RevolveExtent;

namespace {

// A 10 x 30 rectangle 10..20 mm from the sketch Y axis: turned around it,
// a tube with radii 10 and 20, 30 mm long.
constexpr double kTubeVolume = kPi * (20.0 * 20.0 - 10.0 * 10.0) * 30.0;

struct Tube {
    doc::Document       d;
    doc::SketchFeature* sketch = nullptr;
    Tube() {
        sketch = &AddSketch(d);
        AddRect(d, *sketch, {10, 0}, {20, 30});
    }
};

} // namespace

TEST_CASE("full revolution around the sketch Y axis makes a tube") {
    Tube t;
    auto& rev = AddRevolve(t.d, t.sketch->id, {RevolveAxisKind::SketchY, {}});
    kernel::Model model;
    model.Rebuild(t.d);
    REQUIRE(rev.status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(kTubeVolume).epsilon(1e-4));
    const auto box = model.BoundingBox();
    REQUIRE(box);
    CHECK(box->min.x == doctest::Approx(-20.0).epsilon(1e-3));
    CHECK(box->max.y == doctest::Approx(30.0).epsilon(1e-3));
}

TEST_CASE("angle and symmetric revolutions") {
    Tube t;
    auto& rev = AddRevolve(t.d, t.sketch->id, {RevolveAxisKind::SketchY, {}}, RevolveExtent::Angle, "90");
    kernel::Model model;
    model.Rebuild(t.d);
    REQUIRE(rev.status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(kTubeVolume / 4).epsilon(1e-4));

    rev.extent = RevolveExtent::Symmetric;
    t.d.MarkChanged(1);
    model.Rebuild(t.d);
    REQUIRE(rev.status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(kTubeVolume / 4).epsilon(1e-4));
    const auto box = model.BoundingBox();
    REQUIRE(box);
    CHECK(box->max.z == doctest::Approx(-box->min.z).epsilon(1e-3));   // half on each side of the sketch plane
}

TEST_CASE("axis along a sketch line") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {10, 0}, {20, 30});
    // Vertical construction line at x = 30: radii become 10 and 20 again.
    const ObjectId a = sk.sketch.AddPoint(d.Ids(), {30, 0});
    const ObjectId b = sk.sketch.AddPoint(d.Ids(), {30, 30});
    const ObjectId line = sk.sketch.AddLine(d.Ids(), a, b, true);
    auto& rev = AddRevolve(d, sk.id, {RevolveAxisKind::SketchLine, line});
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(rev.status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(kTubeVolume).epsilon(1e-4));
}

TEST_CASE("an axis through the profile is an error, not a broken solid") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {-5, 0}, {5, 30});
    auto& rev = AddRevolve(d, sk.id, {RevolveAxisKind::SketchY, {}});
    kernel::Model model;
    model.Rebuild(d);
    CHECK(rev.status.state == FeatureState::Error);
    CHECK(model.Bodies().empty());
}

TEST_CASE("revolve cuts the previous body") {
    doc::Document d;
    auto& base = AddSketch(d);
    AddRect(d, base, {-30, -30}, {30, 30});
    AddExtrude(d, base.id, "30");
    auto& groove = AddSketch(d, doc::origin::kPlaneXZ);
    AddRect(d, groove, {25, 10}, {35, 20});   // a ring groove around the Z axis
    auto& rev = AddRevolve(d, groove.id, {RevolveAxisKind::SketchY, {}});
    rev.operation = doc::BooleanOp::Cut;
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(rev.status.state == FeatureState::Ok);
    CHECK(model.Bodies().size() == 1);
    CHECK(model.Volume() < 60.0 * 60.0 * 30.0);
}

TEST_CASE("revolve survives a json round trip") {
    Tube t;
    auto& rev = AddRevolve(t.d, t.sketch->id, {RevolveAxisKind::SketchX, {}}, RevolveExtent::Symmetric, "120");
    rev.reversed = true;
    rev.operation = doc::BooleanOp::NewBody;
    const std::string json = doc::DocumentToJson(t.d);
    Expected<doc::Document> loaded = doc::DocumentFromJson(json);
    REQUIRE(loaded.HasValue());
    CHECK(doc::DocumentToJson(*loaded) == json);
    const auto* back = loaded->FindAs<doc::RevolveFeature>(rev.id);
    REQUIRE(back);
    CHECK(back->axis.kind == RevolveAxisKind::SketchX);
    CHECK(back->extent == RevolveExtent::Symmetric);
    CHECK(back->reversed);
}

namespace {

/// A closed polygon through `points`; returns the line ids in order.
std::vector<ObjectId> AddPolygon(doc::Document& d, doc::SketchFeature& sk, const std::vector<Vec2>& points) {
    std::vector<ObjectId> ids, lines;
    for (const Vec2 p : points) ids.push_back(sk.sketch.AddPoint(d.Ids(), p));
    for (std::size_t i = 0; i < ids.size(); ++i) lines.push_back(sk.sketch.AddLine(d.Ids(), ids[i], ids[(i + 1) % ids.size()]));
    return lines;
}

} // namespace

TEST_CASE("a profile touching the axis at a corner is an error, not a pinched solid") {
    doc::Document d;
    auto& sk = AddSketch(d);
    // A diamond whose left corner sits on the Y axis.
    AddPolygon(d, sk, {{0, 10}, {10, 0}, {20, 10}, {10, 20}});
    auto& rev = AddRevolve(d, sk.id, {RevolveAxisKind::SketchY, {}});
    kernel::Model model;
    model.Rebuild(d);
    CHECK(rev.status.state == FeatureState::Error);
    CHECK(rev.status.message.find("касается оси") != std::string::npos);
}

TEST_CASE("a side of the closed profile made the осевая turns the rest around it") {
    // The tester's case: a closed contour, one of its own sides switched to
    // an осевая and used as the axis. The side lies on the axis: allowed.
    doc::Document d;
    auto& sk = AddSketch(d);
    const std::vector<ObjectId> lines = AddPolygon(d, sk, {{0, 0}, {10, 0}, {10, 30}, {0, 30}});
    sketch::Line* side = sk.sketch.FindLine(lines.back());   // x = 0, from (0,30) to (0,0)
    REQUIRE(side);
    side->centerline = true;
    auto& rev = AddRevolve(d, sk.id, {RevolveAxisKind::SketchLine, side->id});
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(rev.status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(kPi * 10.0 * 10.0 * 30.0).epsilon(1e-4));
}

TEST_CASE("a triangle with a side on the axis makes a cone") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddPolygon(d, sk, {{0, 0}, {10, 0}, {0, 30}});
    auto& rev = AddRevolve(d, sk.id, {RevolveAxisKind::SketchY, {}});
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(rev.status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(kPi * 10.0 * 10.0 * 30.0 / 3).epsilon(1e-4));
}

TEST_CASE("serial booleans: the scope nests and ends") {
    using kernel::occt::BooleansSerial;
    CHECK_FALSE(BooleansSerial());
    {
        const kernel::occt::SerialBooleans outer;
        {
            const kernel::occt::SerialBooleans inner;
            CHECK(BooleansSerial());
        }
        CHECK(BooleansSerial());
    }
    CHECK_FALSE(BooleansSerial());
}
