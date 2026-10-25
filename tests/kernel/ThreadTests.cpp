#include "../Fixtures.h"
#include "document/BzcFile.h"
#include "document/ThreadFeature.h"
#include "kernel/Model.h"

#include <doctest.h>

using namespace bz;
using namespace bz::test;
using doc::FeatureState;

namespace {

doc::ThreadFeature& AddThread(doc::Document& d, double radius, const std::string& pitch) {
    auto t = std::make_unique<doc::ThreadFeature>();
    t->id = d.Ids().Next();
    t->name = d.UniqueFeatureName("Резьба");
    t->face = doc::CylinderTouch{{0, 0, 0}, {0, 0, 1}, radius, {1, 0, 0}};
    t->pitch.expression = pitch;
    auto* raw = t.get();
    d.Insert(std::move(t), d.FeatureCount());
    return *raw;
}

} // namespace

TEST_CASE("an external thread cuts a groove along the shaft") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddCircle(d, sk, {0, 0}, 4);
    AddExtrude(d, sk.id, "20");
    auto& thread = AddThread(d, 4, "1.25");
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(thread.status.state == FeatureState::Ok);
    const double shaft = kPi * 16 * 20;
    // ISO 68-1 groove: a trapezoid 7/8 P wide at the surface, 0.6134 P deep,
    // flanks at 60°: 0.3195 P² in section, its centre of area 0.237 P below
    // the surface; 16 turns along the 20 mm. A groove of the right width puts
    // the pitch diameter where it belongs (the old V groove was too narrow:
    // about 125 mm³ here, a bolt too fat for a nut).
    const double area = 0.3195 * 1.25 * 1.25;
    const double expected = area * 2 * kPi * (4 - 0.237 * 1.25) * 16;
    const double cut = shaft - model.Volume();
    CHECK(cut == doctest::Approx(expected).epsilon(0.08));

    thread.fullLength = false;
    thread.length.expression = "10";
    d.MarkChanged(2);
    model.Rebuild(d);
    REQUIRE(thread.status.state == FeatureState::Ok);
    CHECK(shaft - model.Volume() == doctest::Approx(cut / 2).epsilon(0.15));
}

TEST_CASE("an internal thread cuts into the hole wall") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {-10, -10}, {10, 10});
    AddCircle(d, sk, {0, 0}, 3.4);
    AddExtrude(d, sk.id, "12");
    auto& thread = AddThread(d, 3.4, "1.25");
    thread.leftHanded = true;
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(thread.status.state == FeatureState::Ok);
    const double plate = (400 - kPi * 3.4 * 3.4) * 12;
    CHECK(plate - model.Volume() > 30);
    CHECK(plate - model.Volume() < 150);
    CHECK(doc::CoarsePitch(8) == 1.25);
    CHECK(doc::CoarsePitch(6.1) == 1.0);
    REQUIRE(thread.site);
    CHECK_FALSE(thread.site->external);
    CHECK(thread.site->nominal == 8);
    CHECK(doc::StandardSize(7.97) == 8);
    CHECK(doc::StandardSize(7) == 7);
}

TEST_CASE("a thread without a cylinder reports it") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {-10, -10}, {10, 10});
    AddExtrude(d, sk.id, "12");
    auto& thread = AddThread(d, 4, "1.25");
    kernel::Model model;
    model.Rebuild(d);
    CHECK(thread.status.state == FeatureState::Error);
}

TEST_CASE("a thread survives saving and loading") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddCircle(d, sk, {0, 0}, 4);
    AddExtrude(d, sk.id, "20");
    auto& thread = AddThread(d, 4, "1.25");
    thread.fullLength = false;
    thread.fromOtherEnd = true;
    thread.leftHanded = true;
    thread.length.expression = "7";
    const auto loaded = doc::DocumentFromJson(doc::DocumentToJson(d));
    REQUIRE(loaded);
    const auto* t = dynamic_cast<const doc::ThreadFeature*>(loaded->Features()[2].get());
    REQUIRE(t);
    REQUIRE(t->face);
    CHECK(t->face->radius == 4);
    CHECK_FALSE(t->fullLength);
    CHECK(t->fromOtherEnd);
    CHECK(t->leftHanded);
    CHECK(t->length.expression == "7");
}

TEST_CASE("a thread stops short of a shoulder and runs out of a free end") {
    const auto cutOf = [](bool onPlate) {
        doc::Document d;
        if (onPlate) {
            auto& plate = AddSketch(d);
            AddRect(d, plate, {-10, -10}, {10, 10});
            AddExtrude(d, plate.id, "5");
        }
        auto& sk = AddSketch(d);
        AddCircle(d, sk, {0, 0}, 4);
        AddExtrude(d, sk.id, onPlate ? "25" : "20", onPlate ? doc::BooleanOp::Join : doc::BooleanOp::NewBody);
        kernel::Model before;
        before.Rebuild(d);
        auto& thread = AddThread(d, 4, "1.25");
        kernel::Model model;
        model.Rebuild(d);
        REQUIRE(thread.status.state == FeatureState::Ok);
        return before.Volume() - model.Volume();
    };
    CHECK(cutOf(true) < cutOf(false) - 1);
}

TEST_CASE("a chosen standard size wins over the shaft, a cosmetic thread cuts nothing") {
    // An 8.6 mm shaft with M8 chosen: cut down to 8 mm, then threaded, so
    // it ends up the same as an M8 thread on an 8 mm shaft.
    const auto threaded = [](double radius, double size, bool cosmetic) {
        doc::Document d;
        auto& sk = AddSketch(d);
        AddCircle(d, sk, {0, 0}, radius);
        AddExtrude(d, sk.id, "20");
        auto& thread = AddThread(d, radius, "1.25");
        thread.size = size;
        thread.cosmetic = cosmetic;
        kernel::Model model;
        model.Rebuild(d);
        REQUIRE(thread.status.state == FeatureState::Ok);
        return model.Volume();
    };
    const double m8 = threaded(4, 0, false);
    CHECK(threaded(4.3, 8, false) == doctest::Approx(m8).epsilon(0.01));
    CHECK(threaded(4, 0, true) == doctest::Approx(kPi * 16 * 20).epsilon(1e-6));

    doc::Document d;
    auto& sk = AddSketch(d);
    AddCircle(d, sk, {0, 0}, 4);
    AddExtrude(d, sk.id, "20");
    auto& thread = AddThread(d, 4, "1.25");
    thread.size = 8;
    thread.cosmetic = true;
    const auto loaded = doc::DocumentFromJson(doc::DocumentToJson(d));
    REQUIRE(loaded);
    const auto* t = dynamic_cast<const doc::ThreadFeature*>(loaded->Features()[2].get());
    REQUIRE(t);
    CHECK(t->size == 8);
    CHECK(t->cosmetic);
    CHECK(doc::StandardSizes().size() == 22);
}

TEST_CASE("a size bigger than the shaft or smaller than the hole comes out whole, not cut short") {
    // A 6 mm shaft with M8 chosen: material grows to 8 mm first, then the thread.
    {
        doc::Document d;
        auto& sk = AddSketch(d);
        AddCircle(d, sk, {0, 0}, 3);
        AddExtrude(d, sk.id, "20");
        auto& thread = AddThread(d, 3, "1.25");
        thread.size = 8;
        kernel::Model model;
        model.Rebuild(d);
        REQUIRE(thread.status.state == FeatureState::Ok);
        doc::Document ref;
        auto& rs = AddSketch(ref);
        AddCircle(ref, rs, {0, 0}, 4);
        AddExtrude(ref, rs.id, "20");
        AddThread(ref, 4, "1.25");
        kernel::Model m8;
        m8.Rebuild(ref);
        CHECK(model.Volume() == doctest::Approx(m8.Volume()).epsilon(0.01));
    }
    // A 10 mm hole with M6 chosen: the wall grows in to the minor diameter.
    {
        doc::Document d;
        auto& sk = AddSketch(d);
        AddRect(d, sk, {-10, -10}, {10, 10});
        AddCircle(d, sk, {0, 0}, 5);
        AddExtrude(d, sk.id, "12");
        auto& thread = AddThread(d, 5, "1");
        thread.size = 6;
        kernel::Model model;
        model.Rebuild(d);
        REQUIRE(thread.status.state == FeatureState::Ok);
        const double minor = 6 - 1.0825;
        const double plate = 400 * 12 - kPi * minor * minor / 4 * 12;
        // Less the groove: 0.2368 P² in section, about 9 mm³ a turn round 6 mm.
        CHECK(plate - model.Volume() > 20);
        CHECK(plate - model.Volume() < 70);
    }
}
