#include "sheetmetal/Dxf.h"
#include "sheetmetal/SheetPart.h"
#include "sheetmetal/SheetRules.h"

#include <doctest.h>

#include <cmath>
#include <string>

using namespace bz;
using namespace bz::sheet;

namespace {

constexpr double kRight = kPi / 2.0;

Loop Rect(double x0, double y0, double x1, double y1) {
    return {Segment::Line({x0, y0}, {x1, y0}), Segment::Line({x1, y0}, {x1, y1}), Segment::Line({x1, y1}, {x0, y1}),
            Segment::Line({x0, y1}, {x0, y0})};
}

Part Plate(double thickness = 2.0) {
    Part p;
    p.rules = Rules::Auto(thickness);
    p.base.outer = Rect(0, 0, 100, 50);
    return p;
}

std::size_t Count(const std::string& text, const std::string& what) {
    std::size_t n = 0;
    for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) ++n;
    return n;
}

} // namespace

TEST_CASE("bend rules follow the thickness: R = s, K by DIN 6935") {
    // k = 0.65 + 0.5 lg(r/s); K = k / 2.
    CHECK(KFactorDin6935(2, 2) == doctest::Approx(0.325));
    CHECK(KFactorDin6935(4, 2) == doctest::Approx((0.65 + 0.5 * std::log10(2.0)) / 2));
    CHECK(KFactorDin6935(10, 2) == doctest::Approx(0.5));    // r/s >= 5: neutral layer in the middle
    CHECK(KFactorDin6935(30, 2) == doctest::Approx(0.5));
    CHECK(KFactorDin6935(0.01, 2) == doctest::Approx(0.0));  // the formula goes below zero: held at 0
    const Rules r = Rules::Auto(3);
    CHECK(r.thickness == 3);
    CHECK(r.radius == 3);
    CHECK(r.kFactor == doctest::Approx(0.325));
}

TEST_CASE("bend allowance and setback") {
    // BA = angle * (R + K s): 90°, R 2, K 0.44, s 2 -> pi/2 * 2.88.
    CHECK(BendAllowance(kRight, 2, 0.44, 2) == doctest::Approx(4.5238934));
    CHECK(OuterSetback(kRight, 2, 2) == doctest::Approx(4.0));
    CHECK(OuterSetback(kPi / 3, 2, 2) == doctest::Approx(4.0 * std::tan(kPi / 6)));
}

TEST_CASE("arc lengths and areas of loops") {
    const Loop disc{Segment::Circle({5, 5}, 10)};
    CHECK(SignedArea(disc) == doctest::Approx(kPi * 100));
    CHECK(SignedArea(Reversed(disc)) == doctest::Approx(-kPi * 100));
    // A 20 x 10 slot: two lines and two half circles of radius 5.
    const Loop slot{Segment::Line({0, 0}, {20, 0}), Segment::Arc({20, 0}, {20, 10}, {20, 5}, true),
                    Segment::Line({20, 10}, {0, 10}), Segment::Arc({0, 10}, {0, 0}, {0, 5}, true)};
    CHECK(SignedArea(slot) == doctest::Approx(200 + kPi * 25));
    CHECK(slot[1].Length() == doctest::Approx(kPi * 5));
    CHECK(slot[1].Middle().x == doctest::Approx(25));
    Region r{Reversed(Rect(0, 0, 10, 10)), {Rect(2, 2, 4, 4)}};
    r.Orient();
    CHECK(SignedArea(r.outer) > 0);
    CHECK(SignedArea(r.holes[0]) < 0);
    CHECK(r.Area() == doctest::Approx(96));
}

TEST_CASE("U channel: flat width = outer sizes minus setbacks plus allowances") {
    // 100 x 50 base, s = 2, R = 2, K = 0.325, two 20 mm flanges up on the long sides.
    Part p = Plate();
    p.flanges.push_back({{50, 0}, 20, kRight, false});
    p.flanges.push_back({{50, 50}, 20, kRight, false});
    const auto layout = Solve(p);
    REQUIRE(layout.HasValue());
    REQUIRE(layout->flanges.size() == 2);
    const BentFlange& f = layout->flanges[0];
    CHECK(f.setback == doctest::Approx(4));
    CHECK(f.straight == doctest::Approx(16));
    CHECK(f.allowance == doctest::Approx(kRight * (2 + 0.325 * 2)));
    CHECK(f.out.y == doctest::Approx(-1));   // the edge y = 0 faces -y
    // The base lost the setback on both long sides.
    CHECK(layout->base.Bounds().min.y == doctest::Approx(4));
    CHECK(layout->base.Bounds().max.y == doctest::Approx(46));

    const FlatPattern flat = Flatten(p, *layout);
    const Box2 box = flat.Bounds();
    const double width = 50 - 2 * 4 + 2 * (f.allowance + 16);   // 82.325
    CHECK(box.max.x - box.min.x == doctest::Approx(100));
    CHECK(box.max.y - box.min.y == doctest::Approx(width));
    CHECK(width == doctest::Approx(82.32522).epsilon(1e-6));
    CHECK(flat.Area() == doctest::Approx(100 * width));
    REQUIRE(flat.bends.size() == 2);
    CHECK(flat.bends[0].a.y == doctest::Approx(4 - f.allowance / 2));   // centre of the bend zone
    CHECK(BendNote(flat.bends[0]) == "ВВЕРХ 90° R2");
}

TEST_CASE("the reference follows the edge when the sketch grows") {
    Part p = Plate();
    p.flanges.push_back({{50, 0}, 20, kRight, false});
    p.base.outer = Rect(0, 0, 160, 50);   // the sketch got longer: the old point is no longer the middle
    const auto layout = Solve(p);
    REQUIRE(layout.HasValue());
    CHECK(layout->flanges[0].edgeMiddle.x == doctest::Approx(80));
    CHECK(layout->flanges[0].edgeMiddle.y == doctest::Approx(0));
}

TEST_CASE("two flanges at a corner leave a relief in the blank") {
    Part p = Plate();
    p.flanges.push_back({{50, 0}, 20, kRight, false});   // along x
    p.flanges.push_back({{100, 25}, 20, kRight, true});  // along y, bent down
    const auto layout = Solve(p);
    REQUIRE(layout.HasValue());
    const double ba = layout->flanges[0].allowance, len = ba + 16;
    // Trimmed base 96 x 46, strips 96 and 46 long.
    const FlatPattern flat = Flatten(p, *layout);
    CHECK(flat.outline.outer.size() == 8);
    CHECK(flat.Area() == doctest::Approx(96 * 46 + 96 * len + 46 * len));
    CHECK(BendNote(flat.bends[1]) == "ВНИЗ 90° R2");
}

TEST_CASE("an angle other than 90° and a typed K-factor") {
    Part p = Plate(1.5);
    p.rules.radius = 3;
    p.rules.kFactor = 0.44;
    p.flanges.push_back({{50, 50}, 30, kPi / 4, false});   // 45°
    const auto layout = Solve(p);
    REQUIRE(layout.HasValue());
    const BentFlange& f = layout->flanges[0];
    CHECK(f.setback == doctest::Approx(4.5 * std::tan(kPi / 8)));
    CHECK(f.allowance == doctest::Approx(kPi / 4 * (3 + 0.44 * 1.5)));
    CHECK(f.straight == doctest::Approx(30 - f.setback));
}

TEST_CASE("flanges that cannot be made say why") {
    SUBCASE("too low for the bend") {
        Part p = Plate();
        p.flanges.push_back({{50, 0}, 3, kRight, false});
        std::size_t failed = 9;
        const auto layout = Solve(p, &failed);
        REQUIRE_FALSE(layout.HasValue());
        CHECK(failed == 0);
        CHECK(layout.GetError().message.find("меньше зоны сгиба") != std::string::npos);
    }
    SUBCASE("two on one edge") {
        Part p = Plate();
        p.flanges.push_back({{50, 0}, 20, kRight, false});
        p.flanges.push_back({{40, 0}, 20, kRight, false});
        std::size_t failed = 9;
        REQUIRE_FALSE(Solve(p, &failed).HasValue());
        CHECK(failed == 1);
    }
    SUBCASE("a hole in the bend") {
        Part p = Plate();
        p.base.holes.push_back({Segment::Circle({50, 3}, 2)});
        p.flanges.push_back({{50, 0}, 20, kRight, false});
        const auto layout = Solve(p);
        REQUIRE_FALSE(layout.HasValue());
        CHECK(layout.GetError().message.find("отверстие") != std::string::npos);
    }
    SUBCASE("a hole clear of the bend stays") {
        Part p = Plate();
        p.base.holes.push_back({Segment::Circle({50, 25}, 5)});
        p.flanges.push_back({{50, 0}, 20, kRight, false});
        const auto layout = Solve(p);
        REQUIRE(layout.HasValue());
        CHECK(Flatten(p, *layout).outline.holes.size() == 1);
    }
    SUBCASE("an arc next to the edge") {
        Part p = Plate();
        p.base.outer = {Segment::Line({0, 0}, {20, 0}), Segment::Arc({20, 0}, {20, 10}, {20, 5}, true),
                        Segment::Line({20, 10}, {0, 10}), Segment::Arc({0, 10}, {0, 0}, {0, 5}, true)};
        p.flanges.push_back({{10, 0}, 20, kRight, false});
        const auto layout = Solve(p);
        REQUIRE_FALSE(layout.HasValue());
        CHECK(layout.GetError().message.find("дуга") != std::string::npos);
    }
    SUBCASE("a side too short") {
        Part p = Plate();
        p.base.outer = Rect(0, 0, 100, 6);
        p.flanges.push_back({{50, 0}, 20, kRight, false});
        p.flanges.push_back({{50, 6}, 20, kRight, false});   // 4 + 4 > 6
        REQUIRE_FALSE(Solve(p).HasValue());
    }
}

TEST_CASE("the flat pattern as DXF R12") {
    Part p = Plate();
    p.base.holes.push_back({Segment::Circle({50, 25}, 5)});
    p.flanges.push_back({{50, 0}, 20, kRight, false});
    const auto layout = Solve(p);
    REQUIRE(layout.HasValue());
    const std::string dxf = FlatPatternDxf(Flatten(p, *layout));
    CHECK(dxf.find("AC1009") != std::string::npos);
    CHECK(Count(dxf, "\nLINE\n") == 6 + 1);   // outline 4 - 1 + 3, one bend line
    CHECK(Count(dxf, "\nCIRCLE\n") == 1);
    CHECK(Count(dxf, "\nTEXT\n") == 1);
    CHECK(dxf.find("\nBEND\n") != std::string::npos);
    CHECK(dxf.find("90%%d R2") != std::string::npos);   // the degree sign as AutoCAD writes it
    CHECK(dxf.substr(dxf.size() - 8) == "  0\nEOF\n");
}

TEST_CASE("a flange on a flange: C profile, flat length and where the hem goes") {
    // 100 x 50, s 2, R 2, K 0.325: a 25 mm flange up on y = 0, and on its far edge a 10 mm flange towards the plate.
    Part p = Plate();
    p.flanges.push_back({{50, 0}, 25, kRight, false});
    Flange hem{{}, 10, kRight, false};
    hem.parent = 0;
    p.flanges.push_back(hem);
    const auto layout = Solve(p);
    REQUIRE(layout.HasValue());
    const BentFlange& wall = layout->flanges[0];
    const BentFlange& top = layout->flanges[1];
    CHECK(wall.child == 1);
    CHECK(top.parent == 0);
    CHECK(wall.straight == doctest::Approx(25 - 4 - 4));   // both bends come out of it
    CHECK(top.straight == doctest::Approx(6));
    // The wall stands at y = 0 going up; the hem starts at its top and points into the plate (+y).
    CHECK(wall.stripStart.y == doctest::Approx(0).epsilon(1e-9));
    CHECK(wall.stripStart.z == doctest::Approx(4));
    CHECK(wall.stripOut.z == doctest::Approx(1));
    CHECK(wall.stripNormal.y == doctest::Approx(1));
    CHECK(top.start.z == doctest::Approx(21));
    CHECK(top.stripOut.y == doctest::Approx(1));
    CHECK(top.stripStart.z == doctest::Approx(25));   // its outer side: the outer height of the wall is kept
    CHECK(top.stripNormal.z == doctest::Approx(-1));
    const FlatPattern flat = Flatten(p, *layout);
    const double ba = kRight * (2 + 0.325 * 2);
    const Box2 box = flat.Bounds();
    CHECK(box.max.y - box.min.y == doctest::Approx(46 + ba + 17 + ba + 6));
    REQUIRE(flat.bends.size() == 2);
    CHECK(flat.bends[1].a.y == doctest::Approx(4 - (ba + 17 + ba / 2)));
}

TEST_CASE("a flange on a flange that does not fit says why") {
    Part p = Plate();
    p.flanges.push_back({{50, 0}, 10, kRight, false});   // straight 6
    Flange hem{{}, 10, kRight, false};
    hem.parent = 0;
    p.flanges.push_back(hem);   // takes 4 more: 2 left, fine
    REQUIRE(Solve(p).HasValue());
    p.flanges[0].height = 8;   // 8 - 4 - 4 = 0
    std::size_t failed = 9;
    const auto layout = Solve(p, &failed);
    REQUIRE_FALSE(layout.HasValue());
    CHECK(failed == 1);
    CHECK(layout.GetError().message.find("слишком низкая") != std::string::npos);
    Flange second{{}, 10, kRight, false};
    second.parent = 0;
    p.flanges[0].height = 30;
    p.flanges.push_back(second);
    REQUIRE_FALSE(Solve(p, &failed).HasValue());
    CHECK(failed == 2);   // one flange per far edge
}

TEST_CASE("a flange narrower than its edge: offsets, reliefs, the corners stay") {
    Part p = Plate();   // 100 x 50, s 2: relief 2 wide, 1 past the bend zone
    Flange f{{50, 0}, 20, kRight, false};
    f.offsetStart = 20, f.offsetEnd = 30;
    p.flanges.push_back(f);
    const auto layout = Solve(p);
    REQUIRE(layout.HasValue());
    const BentFlange& b = layout->flanges[0];
    CHECK(b.u1 == doctest::Approx(20));
    CHECK(b.u2 == doctest::Approx(70));
    CHECK(b.width == doctest::Approx(50));
    CHECK(b.a.x == doctest::Approx(20));
    CHECK(b.a.y == doctest::Approx(4));
    // The plate keeps its corners; it loses the bend zone under the flange and two reliefs 2 x 5.
    CHECK(layout->base.Area() == doctest::Approx(5000 - 50 * 4 - 2 * (2 * 5)));
    const Box2 box = layout->base.Bounds();
    CHECK(box.min.y == doctest::Approx(0).epsilon(1e-9));
    const FlatPattern flat = Flatten(p, *layout);
    CHECK(flat.Area() == doctest::Approx(layout->base.Area() + 50 * (b.allowance + 16)));
    CHECK(flat.bends.size() == 1);

    p.flanges[0] = {{50, 0}, 20, kRight, false};
    p.flanges[0].centered = true, p.flanges[0].width = 40;
    const auto centred = Solve(p);
    REQUIRE(centred.HasValue());
    CHECK(centred->flanges[0].u1 == doctest::Approx(30));
    CHECK(centred->flanges[0].u2 == doctest::Approx(70));

    p.flanges[0] = {{50, 0}, 20, kRight, false};
    p.flanges[0].offsetStart = 1;   // less than the relief
    const auto tight = Solve(p);
    REQUIRE_FALSE(tight.HasValue());
    CHECK(tight.GetError().message.find("рельеф") != std::string::npos);
}

TEST_CASE("a flange on the side edge of a flange: a corner tab") {
    Part p = Plate();
    p.flanges.push_back({{50, 0}, 25, kRight, false});   // the wall: up on y = 0, straight 21
    Flange tab{{}, 15, kRight, false};
    tab.parent = 0;
    tab.onEdge = FlangeEdge::Start;   // its side at x = 0, bent towards the plate
    p.flanges.push_back(tab);
    const auto layout = Solve(p);
    REQUIRE(layout.HasValue());
    const BentFlange& wall = layout->flanges[0];
    const BentFlange& t = layout->flanges[1];
    CHECK(wall.sideStart == 1);
    CHECK(t.width == doctest::Approx(21));   // as long as the wall's flat part
    CHECK(t.stripOut.y == doctest::Approx(1));   // runs along the plate's side
    CHECK(t.stripStart.x == doctest::Approx(-4));   // bends from the wall's side itself
    const FlatPattern flat = Flatten(p, *layout);
    REQUIRE(flat.bends.size() == 2);
    const double ba = kRight * (2 + 0.325 * 2);
    CHECK(flat.Area() == doctest::Approx(100 * 46 + 100 * (ba + 21) + 21 * (ba + 11)));
    CHECK(flat.Bounds().min.x == doctest::Approx(-(ba + 11)));
    // Nothing goes on a side flange yet.
    Flange more{{}, 15, kRight, false};
    more.parent = 1;
    p.flanges.push_back(more);
    REQUIRE_FALSE(Solve(p).HasValue());
}

TEST_CASE("cuts in the plate and in a wall land in the flat pattern") {
    Part p = Plate();
    p.flanges.push_back({{50, 0}, 25, kRight, false});
    p.cuts.push_back({-1, {Segment::Circle({50, 25}, 5)}});
    p.cuts.push_back({0, {Segment::Circle({30, 10}, 3)}});   // in the wall: 30 along it, 10 up its flat part
    const auto layout = Solve(p);
    REQUIRE(layout.HasValue());
    CHECK(layout->base.holes.size() == 1);
    REQUIRE(layout->stripCuts.size() == 1);
    const FlatPattern flat = Flatten(p, *layout);
    REQUIRE(flat.outline.holes.size() == 2);
    const double ba = kRight * (2 + 0.325 * 2);
    const Segment& h = flat.outline.holes[1][0];
    CHECK(h.center.x == doctest::Approx(30));
    CHECK(h.center.y == doctest::Approx(4 - ba - 10));
    CHECK(SignedArea(flat.outline.holes[1]) < 0);   // a hole
    CHECK(flat.Area() == doctest::Approx(100 * 46 + 100 * (ba + 21) - kPi * 25 - kPi * 9));

    p.cuts[1] = {0, {Segment::Circle({30, 1}, 3)}};   // into the bend
    std::size_t failed = 0;
    const auto bad = Solve(p, &failed);
    REQUIRE_FALSE(bad.HasValue());
    CHECK(failed == p.flanges.size() + 1);
    p.cuts[1] = {-1, {Segment::Circle({50, 1}, 3)}};   // over the plate's edge
    REQUIRE_FALSE(Solve(p).HasValue());
}
