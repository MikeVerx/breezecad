// Extrusion up to a curved face in the situations people actually make:
// a cylinder made by an extrusion, a boss joined to it, a profile longer
// than the face, the face below the sketch, the sketch plane through it.

#include "../Fixtures.h"
#include "document/WorkPlaneFeature.h"
#include "kernel/Model.h"

#include <doctest.h>

#include <cmath>
#include <string>

using namespace bz;
using namespace bz::test;
using doc::BooleanOp;
using doc::FeatureState;

namespace {

/// A cylinder of radius `r` lying along Y with its axis at height `z`,
/// `length` long, made by extruding a circle on XZ symmetrically.
doc::ExtrudeFeature& AddLyingCylinder(doc::Document& d, double z, double r, double length) {
    auto& sk = AddSketch(d, doc::origin::kPlaneXZ);
    AddCircle(d, sk, {0, z}, r);
    auto& ex = AddExtrude(d, sk.id, std::to_string(length));
    ex.extent = doc::ExtentType::Symmetric;
    return ex;
}

/// A rectangle `lo`..`hi` on `plane` extruded up to the curved face picked
/// at `point` with the outward normal `normal`.
doc::ExtrudeFeature& AddUpTo(doc::Document& d, ObjectId plane, Vec2 lo, Vec2 hi, Vec3 point, Vec3 normal, BooleanOp op) {
    auto& sk = AddSketch(d, plane);
    AddRect(d, sk, lo, hi);
    auto& ex = AddExtrude(d, sk.id, "1", op);
    ex.extent = doc::ExtentType::ToPlane;
    ex.toFace = {.normal = normal.Normalized(), .point = point, .curved = true};
    return ex;
}

/// Area under the circle y = c - sqrt(r^2 - x^2) for x in [a, b], times `depth`:
/// the volume of a post from y = 0 up to a lying cylinder.
double Under(double c, double r, double a, double b, double depth) {
    double area = 0;
    constexpr int n = 4000;
    for (int i = 0; i < n; ++i) {
        const double x = a + (b - a) * (i + 0.5) / n;
        area += (c - std::sqrt(r * r - x * x)) * (b - a) / n;
    }
    return area * depth;
}

doc::WorkPlaneFeature& AddOffsetPlane(doc::Document& d, ObjectId base, const std::string& offset) {
    auto wp = std::make_unique<doc::WorkPlaneFeature>();
    wp->id = d.Ids().Next();
    wp->name = d.UniqueFeatureName("Плоскость");
    wp->base = base;
    wp->offset.expression = offset;
    auto* raw = wp.get();
    d.Insert(std::move(wp), d.FeatureCount());
    return *raw;
}

} // namespace

TEST_CASE("up to a cylinder made by an extrusion, as a new body and joined") {
    const double cylinder = kPi * 400 * 60;
    for (const BooleanOp op : {BooleanOp::NewBody, BooleanOp::Join}) {
        CAPTURE(static_cast<int>(op));
        doc::Document d;
        auto& cyl = AddLyingCylinder(d, 40, 20, 60);
        const Vec3 normal{0, 0, -1};
        auto& post = AddUpTo(d, doc::origin::kPlaneXY, {-5, -5}, {5, 5}, {0, 0, 20}, normal, op);
        kernel::Model model;
        model.Rebuild(d);
        REQUIRE(cyl.status.state == FeatureState::Ok);
        REQUIRE_MESSAGE(post.status.state == FeatureState::Ok, post.status.message);
        CHECK(model.Volume() - cylinder == doctest::Approx(Under(40, 20, -5, 5, 10)).epsilon(1e-3));
    }
}

TEST_CASE("up to a cylinder picked off the middle, on its side") {
    doc::Document d;
    AddLyingCylinder(d, 40, 20, 60);
    const double x = 12, z = 40 - std::sqrt(400 - x * x);
    auto& post = AddUpTo(d, doc::origin::kPlaneXY, {-5, -5}, {5, 5}, {x, 3, z}, Vec3{x, 0, z - 40}, BooleanOp::NewBody);
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE_MESSAGE(post.status.state == FeatureState::Ok, post.status.message);
    CHECK(model.Volume() - kPi * 400 * 60 == doctest::Approx(Under(40, 20, -5, 5, 10)).epsilon(1e-3));
}

TEST_CASE("a profile longer than the cylinder: its surface carries on past the ends") {
    doc::Document d;
    AddLyingCylinder(d, 40, 20, 20);   // y from -10 to 10, the post from -15 to 15
    auto& post = AddUpTo(d, doc::origin::kPlaneXY, {-5, -15}, {5, 15}, {0, 0, 20}, {0, 0, -1}, BooleanOp::NewBody);
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE_MESSAGE(post.status.state == FeatureState::Ok, post.status.message);
    CHECK(model.Volume() - kPi * 400 * 20 == doctest::Approx(Under(40, 20, -5, 5, 30)).epsilon(1e-3));
}

TEST_CASE("up to a cylinder below the sketch") {
    doc::Document d;
    AddLyingCylinder(d, -40, 20, 60);
    auto& post = AddUpTo(d, doc::origin::kPlaneXY, {-5, -5}, {5, 5}, {0, 0, -20}, {0, 0, 1}, BooleanOp::NewBody);
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE_MESSAGE(post.status.state == FeatureState::Ok, post.status.message);
    CHECK(model.Volume() - kPi * 400 * 60 == doctest::Approx(Under(40, 20, -5, 5, 10)).epsilon(1e-3));
}

TEST_CASE("from a plane above a lying cylinder down to its top") {
    doc::Document d;
    AddLyingCylinder(d, 0, 20, 60);
    auto& wp = AddOffsetPlane(d, doc::origin::kPlaneXY, "50");
    auto& post = AddUpTo(d, wp.id, {-5, -5}, {5, 5}, {0, 0, 20}, {0, 0, 1}, BooleanOp::Join);
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE_MESSAGE(post.status.state == FeatureState::Ok, post.status.message);
    // 50 - sqrt(r^2 - x^2) high, the cylinder's own volume under it not counted twice.
    CHECK(model.Volume() - kPi * 400 * 60 == doctest::Approx(Under(50, 20, -5, 5, 10)).epsilon(1e-3));
}

TEST_CASE("the sketch plane through a cylinder's axis: the side picked decides the way") {
    // A standing cylinder r 20 (axis Z); a sketch on XZ, through its axis,
    // just outside it; up to the far (+Y) or near (-Y) side.
    for (const double side : {1.0, -1.0}) {
        CAPTURE(side);
        doc::Document d;
        auto& base = AddSketch(d);
        AddCircle(d, base, {0, 0}, 20);
        AddExtrude(d, base.id, "40");
        auto& sk = AddSketch(d, doc::origin::kPlaneXZ);
        AddRect(d, sk, {30, 10}, {40, 20});   // off the cylinder: x 30..40
        auto& ex = AddExtrude(d, sk.id, "1", BooleanOp::NewBody);
        ex.extent = doc::ExtentType::ToPlane;
        ex.toFace = {.normal = {0, side, 0}, .point = {0, 20 * side, 15}, .curved = true};
        kernel::Model model;
        model.Rebuild(d);
        // The profile is beside the cylinder: the face does not cover it.
        CHECK(ex.status.state == FeatureState::Error);
    }
    for (const double side : {1.0, -1.0}) {
        CAPTURE(side);
        doc::Document d;
        auto& base = AddSketch(d);
        AddCircle(d, base, {0, 0}, 20);
        AddExtrude(d, base.id, "40");
        auto& wp = AddOffsetPlane(d, doc::origin::kPlaneXZ, "50");   // y = -50
        auto& sk = AddSketch(d, wp.id);
        AddRect(d, sk, {-5, 10}, {5, 20});
        auto& ex = AddExtrude(d, sk.id, "1", BooleanOp::Join);
        ex.extent = doc::ExtentType::ToPlane;
        ex.toFace = {.normal = {0, -1, 0}, .point = {0, -20, 15}, .curved = true};
        kernel::Model model;
        model.Rebuild(d);
        REQUIRE_MESSAGE(ex.status.state == FeatureState::Ok, ex.status.message);
        CHECK(model.Volume() - kPi * 400 * 40 == doctest::Approx(Under(50, 20, -5, 5, 10)).epsilon(1e-3));
    }
}

TEST_CASE("up to the inside of a half pipe (a concave face)") {
    // A trough: a box with a lying cylinder cut out of its top. Its inner
    // face's centre of mass is above the face itself.
    doc::Document d;
    auto& box = AddSketch(d);
    AddRect(d, box, {-30, -30}, {30, 30});
    AddExtrude(d, box.id, "40");
    auto& cut = AddSketch(d, doc::origin::kPlaneXZ);
    AddCircle(d, cut, {0, 40}, 20);
    auto& cutEx = AddExtrude(d, cut.id, "60", BooleanOp::Cut);
    cutEx.extent = doc::ExtentType::Symmetric;
    auto& wp = AddOffsetPlane(d, doc::origin::kPlaneXY, "60");
    auto& sk = AddSketch(d, wp.id);
    AddRect(d, sk, {-5, -5}, {5, 5});
    auto& ex = AddExtrude(d, sk.id, "1", BooleanOp::Join);
    ex.extent = doc::ExtentType::ToPlane;
    ex.toFace = {.normal = {0, 0, 1}, .point = {0, 0, 20}, .curved = true};
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(cutEx.status.state == FeatureState::Ok);
    REQUIRE_MESSAGE(ex.status.state == FeatureState::Ok, ex.status.message);
    const double trough = 60 * 60 * 40 - kPi * 400 * 60 / 2;
    // From z = 60 down to the trough bottom z = 40 - sqrt(400 - x^2): 20 + sqrt(400 - x^2) high.
    const double post = 10 * 10 * 20 + (10 * 10 * 40 - Under(40, 20, -5, 5, 10));
    CHECK(model.Volume() - trough == doctest::Approx(post).epsilon(1e-3));
}

TEST_CASE("up to a curved face with a draft") {
    doc::Document d;
    AddLyingCylinder(d, 40, 20, 60);
    auto& post = AddUpTo(d, doc::origin::kPlaneXY, {-5, -5}, {5, 5}, {0, 0, 20}, {0, 0, -1}, BooleanOp::NewBody);
    post.taper.expression = "3";
    kernel::Model model;
    model.Rebuild(d);
    CHECK_MESSAGE(post.status.state == FeatureState::Ok, post.status.message);
}

TEST_CASE("up to the inside of a funnel: a cone, carried on to its apex and no further") {
    // The author's part (22.10.2026): a hub filled down to the funnel under
    // it. The cone face does not cover the hub, so its surface is carried
    // on — past the apex it was a second cone turned the other way, and a
    // long inverted cone hung under the right one (a "hat" 450 mm deep).
    const auto funnel = [](doc::Document& d) {
        auto& sk = AddSketch(d, doc::origin::kPlaneXZ);
        // Radius along the sketch's x, height along its y: the inside a cone
        // at 45° from r 15 (z 0) down to r 9 (z -6) — its apex at z -15 —, then a bore.
        const std::vector<Vec2> points{{15, 0}, {30, 0}, {30, -30}, {9, -30}, {9, -6}};
        std::vector<ObjectId> ids;
        for (const Vec2 p : points) ids.push_back(sk.sketch.AddPoint(d.Ids(), p));
        for (std::size_t i = 0; i < ids.size(); ++i) sk.sketch.AddLine(d.Ids(), ids[i], ids[(i + 1) % ids.size()]);
        return &AddRevolve(d, sk.id, {doc::RevolveAxisKind::SketchY, {}});
    };
    doc::Document alone;
    funnel(alone);
    kernel::Model bare;
    bare.Rebuild(alone);

    doc::Document d;
    const doc::RevolveFeature* rev = funnel(d);
    auto& hub = AddSketch(d);
    AddCircle(d, hub, {0, 0}, 14);
    auto& ex = AddExtrude(d, hub.id, "1", BooleanOp::NewBody);
    ex.extent = doc::ExtentType::ToPlane;
    ex.toFace = {.normal = Vec3{-1, 0, 1}.Normalized(), .point = {12, 0, -3}, .curved = true};
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(rev->status.state == FeatureState::Ok);
    REQUIRE_MESSAGE(ex.status.state == FeatureState::Ok, ex.status.message);
    // From z = 0 down to the cone z = r - 15: the integral of 2πr(15 - r) over r 0..14.
    const double cone = 2 * kPi * (15.0 * 14 * 14 / 2 - 14.0 * 14 * 14 / 3);
    CHECK(model.Volume() - bare.Volume() == doctest::Approx(cone).epsilon(1e-3));
    const auto box = model.BoundingBox();
    REQUIRE(box);
    CHECK(box->min.z == doctest::Approx(-30).epsilon(1e-6));   // the funnel's bottom, nothing below it
}
