#include "../Fixtures.h"
#include "kernel/Model.h"

#include <doctest.h>

#include <vector>

using namespace bz;
using namespace bz::test;
using doc::BooleanOp;

namespace {

/// A 40 x 40 box on XY, `distance` high, made as `extent` says; then
/// which side of XY holds material, seen from a 10 x 10 profile in the
/// middle, for an operation coming after it.
int SideAfterBox(const std::string& distance, bool reversed, doc::ExtentType extent = doc::ExtentType::Distance,
                 Frame frame = Frame{}) {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {-20, -20}, {20, 20});
    auto& box = AddExtrude(d, sk.id, distance);
    box.reversed = reversed;
    box.extent = extent;
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(model.Bodies().size() == 1);
    const std::vector<Vec2> samples{{0, 0}, {3, 3}};
    return model.MaterialSide(frame, samples, d.FeatureCount());
}

} // namespace

// The «Вырез» turns into the body by itself (ExtrudeAutoDirection); which
// way that is the model tells from the body before the cut.
TEST_CASE("выдавливание: с какой стороны плоскости эскиза материал") {
    SUBCASE("тело над плоскостью: +1") { CHECK(SideAfterBox("10", false) == 1); }
    SUBCASE("тело под плоскостью (эскиз на верхней грани): -1") { CHECK(SideAfterBox("10", true) == -1); }
    SUBCASE("плоскость посередине тела: 0, не решаем") {
        CHECK(SideAfterBox("10", false, doc::ExtentType::Symmetric) == 0);
    }
    SUBCASE("эскиз на плоскости в стороне от тела: тело найдётся и вдали") {
        Frame above;
        above.origin = {0, 0, 25};   // the box is 0..10: below this plane, 15 mm away
        CHECK(SideAfterBox("10", false, doc::ExtentType::Distance, above) == -1);
    }
    SUBCASE("профиль мимо тела: 0") {
        Frame aside;
        aside.origin = {100, 0, 0};
        CHECK(SideAfterBox("10", false, doc::ExtentType::Distance, aside) == 0);
    }
    SUBCASE("нет тела до операции: 0") {
        doc::Document d;
        kernel::Model model;
        model.Rebuild(d);
        const std::vector<Vec2> samples{{0, 0}};
        CHECK(model.MaterialSide(Frame{}, samples, 0) == 0);
    }
}
