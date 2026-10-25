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

namespace {

/// A plate with a round boss on it: Ø10 at (20, 10), from z = 10 up to 30.
struct Boss {
    doc::Document       d;
    doc::SketchFeature* bossSketch = nullptr;
    Boss() {
        auto& plate = AddSketch(d);
        AddRect(d, plate, {-60, -40}, {60, 40});
        AddExtrude(d, plate.id, "10");
        bossSketch = &AddSketch(d);
        AddCircle(d, *bossSketch, {20, 10}, 5);
        AddExtrude(d, bossSketch->id, "30", doc::BooleanOp::Join);
    }
    /// A Ø4 hole into the boss top, its centre typed off the axis on purpose.
    doc::HoleFeature& AddHole() {
        auto h = std::make_unique<doc::HoleFeature>();
        h->id = d.Ids().Next();
        h->name = d.UniqueFeatureName("Отверстие");
        h->face = {{0, 0, 1}, {20, 10, 30}};
        h->centres = {{0, 0}};
        h->diameter.expression = "4";
        h->depth.expression = "8";
        auto* raw = h.get();
        d.Insert(std::move(h), d.FeatureCount());
        return *raw;
    }
    void MoveBoss(Vec2 to) {
        for (auto& p : bossSketch->sketch.points)
            if (!p.isOrigin) p.pos = to;
        d.MarkChanged(static_cast<std::size_t>(d.IndexOf(bossSketch->id)));
    }
};

/// The top rim of the boss as the user would click it.
doc::EdgeReference TopRim(Vec2 at) {
    doc::EdgeReference e;
    e.curve = doc::CurveKind::Circle;
    e.direction = {0, 0, 1};
    e.radius = 5;
    e.middle = {at.x - 5, at.y, 30};
    e.faces = {{{doc::SurfaceKind::Plane, {0, 0, 1}}, {doc::SurfaceKind::Cylinder, {0, 0, 1}}}};
    return e;
}

} // namespace

TEST_CASE("a hole coaxial with a round edge sits on its axis and follows it") {
    Boss b;
    auto& hole = b.AddHole();
    kernel::Model model;
    model.Rebuild(b.d);
    REQUIRE(hole.status.state == FeatureState::Ok);
    // The rim as the kernel describes it (as a click would give it).
    hole.concentric = {doc::HoleAxis{0, TopRim({20, 10}), std::nullopt}};
    b.d.MarkChanged(static_cast<std::size_t>(b.d.IndexOf(hole.id)));
    model.Rebuild(b.d);
    REQUIRE(hole.status.state == FeatureState::Ok);
    CHECK(hole.centres[0].x == doctest::Approx(20.0));
    CHECK(hole.centres[0].y == doctest::Approx(10.0));

    b.MoveBoss({-15, 5});
    model.Rebuild(b.d);
    REQUIRE(hole.status.state == FeatureState::Ok);
    CHECK(hole.centres[0].x == doctest::Approx(-15.0));
    CHECK(hole.centres[0].y == doctest::Approx(5.0));
    // Drilled where the boss is: the volume lost is the Ø4×8 hole.
    const double boss = 120.0 * 80.0 * 10.0 + kPi * 25.0 * 20.0;
    CHECK(model.Volume() == doctest::Approx(boss - kPi * 4.0 * 8.0).epsilon(1e-4));
}

TEST_CASE("a hole coaxial with a cylindrical face follows it; a lost reference is an error") {
    Boss b;
    auto& hole = b.AddHole();
    hole.concentric = {doc::HoleAxis{0, std::nullopt, doc::CylinderTouch{{20, 10, 0}, {0, 0, 1}, 5, {}}}};
    kernel::Model model;
    model.Rebuild(b.d);
    REQUIRE(hole.status.state == FeatureState::Ok);
    CHECK(hole.centres[0].x == doctest::Approx(20.0));
    CHECK(hole.centres[0].y == doctest::Approx(10.0));

    // A cylinder is found again near where it was (as for a work axis), so
    // a small move; a round edge is found by its shape (the test above).
    b.MoveBoss({22, 12});
    model.Rebuild(b.d);
    INFO(hole.status.message);
    REQUIRE(hole.status.state == FeatureState::Ok);
    CHECK(hole.centres[0].x == doctest::Approx(22.0));
    CHECK(hole.centres[0].y == doctest::Approx(12.0));

    hole.concentric[0].cylinder = doc::CylinderTouch{{0, 0, 0}, {1, 0, 0}, 50, {}};   // nothing like it on the part
    b.d.MarkChanged(static_cast<std::size_t>(b.d.IndexOf(hole.id)));
    model.Rebuild(b.d);
    CHECK(hole.status.state == FeatureState::Error);
}

TEST_CASE("coaxial centres: saved, read back, and dropped with their centre") {
    Boss b;
    auto& hole = b.AddHole();
    hole.centres = {{1, 1}, {2, 2}, {3, 3}};
    hole.concentric = {doc::HoleAxis{1, TopRim({20, 10}), std::nullopt},
                       doc::HoleAxis{2, std::nullopt, doc::CylinderTouch{{20, 10, 0}, {0, 0, 1}, 5, {}}}};
    const std::string json = doc::DocumentToJson(b.d);
    Expected<doc::Document> loaded = doc::DocumentFromJson(json);
    REQUIRE(loaded.HasValue());
    const auto* back = loaded->FindAs<doc::HoleFeature>(hole.id);
    REQUIRE(back);
    REQUIRE(back->concentric.size() == 2);
    CHECK(back->concentric[0].edge == hole.concentric[0].edge);
    CHECK(back->concentric[1].cylinder->radius == doctest::Approx(5.0));

    hole.RemoveCentre(0);   // the axes now hold centres 0 and 1
    REQUIRE(hole.concentric.size() == 2);
    CHECK(hole.concentric[0].centre == 0);
    CHECK(hole.concentric[1].centre == 1);
    hole.RemoveCentre(0);   // with its axis
    REQUIRE(hole.concentric.size() == 1);
    CHECK(hole.concentric[0].centre == 0);
    CHECK(hole.AxisOf(0) == &hole.concentric[0]);
}

TEST_CASE("a tapped hole: drilled for the tap, the thread cut in the wall") {
    Plate p;
    auto& hole = p.AddHole(HoleType::Simple, true);
    hole.threaded = true;
    hole.threadSize = 8;
    hole.threadPitch.expression = "1.25";
    kernel::Model model;
    model.Rebuild(p.d);
    REQUIRE(hole.status.state == FeatureState::Ok);
    CHECK(hole.DrillDiameter() == doctest::Approx(8 - 1.0825 * 1.25).epsilon(1e-4));
    const double drilled = kPi * hole.DrillDiameter() * hole.DrillDiameter() / 4 * 30;
    // The groove: 0.2368 P² in section round about 7.2 mm, 24 turns and the run-outs.
    const double groove = Cut(model) - drilled;
    CHECK(groove > 0.2368 * 1.25 * 1.25 * kPi * 7.0 * 22);
    CHECK(groove < 0.2368 * 1.25 * 1.25 * kPi * 7.6 * 27);

    // Threaded only part of the way: less groove, same drill.
    hole.threadFull = false;
    hole.threadDepth.expression = "10";
    p.d.MarkChanged(2);
    model.Rebuild(p.d);
    REQUIRE(hole.status.state == FeatureState::Ok);
    const double part = Cut(model) - drilled;
    CHECK(part < groove * 0.6);
    CHECK(part > groove * 0.2);

    // Saved and loaded with its thread; an old file without one is a plain hole.
    const auto loaded = doc::DocumentFromJson(doc::DocumentToJson(p.d));
    REQUIRE(loaded);
    const auto* h = dynamic_cast<const doc::HoleFeature*>(loaded->Features()[2].get());
    REQUIRE(h);
    CHECK(h->threaded);
    CHECK(h->threadSize == 8);
    CHECK_FALSE(h->threadFull);
    CHECK(h->threadDepth.expression == "10");
}
