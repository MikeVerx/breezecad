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
    // Groove: a 60° triangle 0.6134P deep, about 0.22 P² in section, 16 turns.
    const double cut = shaft - model.Volume();
    CHECK(cut > 80);
    CHECK(cut < 180);

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
