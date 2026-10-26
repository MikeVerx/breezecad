#include "../Fixtures.h"
#include "document/BzcFile.h"
#include "document/SheetMetalFeature.h"
#include "kernel/Model.h"
#include "sheetmetal/Dxf.h"

#include <doctest.h>

using namespace bz;
using namespace bz::test;
using doc::FeatureState;

namespace {

/// A 100 x 50 sketch on XY made a sheet, with flanges added by the tests.
struct Sheet {
    doc::Document          d;
    doc::SheetMetalFeature* sheet = nullptr;

    explicit Sheet(const std::string& thickness = "2") {
        auto& sk = AddSketch(d);
        AddRect(d, sk, {0, 0}, {100, 50});
        auto f = std::make_unique<doc::SheetMetalFeature>();
        f->id = d.Ids().Next();
        f->name = d.UniqueFeatureName("Лист");
        f->sketch = sk.id;
        f->thickness.name = "s";
        f->thickness.expression = thickness;
        sheet = f.get();
        d.Insert(std::move(f), d.FeatureCount());
    }
    doc::FlangeFeature& OnFlange(const doc::FlangeFeature& under, const std::string& height, bool down = false) {
        auto& f = Flange({}, height, down);
        f.edgeSet = false;
        f.onFlange = under.id;
        return f;
    }
    doc::FlangeFeature& Flange(Vec3 edge, const std::string& height = "20", bool down = false, const std::string& angle = "90") {
        auto f = std::make_unique<doc::FlangeFeature>();
        f->id = d.Ids().Next();
        f->name = d.UniqueFeatureName("Отбортовка");
        f->sheet = sheet->id;
        f->edge = edge;
        f->edgeSet = true;
        f->height.expression = height;
        f->angle.expression = angle;
        f->down = down;
        auto& ref = *f;
        d.Insert(std::move(f), d.FeatureCount());
        return ref;
    }
};

constexpr double kBendVolume = kPi / 2 / 2 * (4.0 * 4.0 - 2.0 * 2.0) * 100;   // quarter tube R 2..4, 100 long

} // namespace

TEST_CASE("a sheet is a plate of its thickness; the rules follow the thickness") {
    Sheet s;
    kernel::Model model;
    model.Rebuild(s.d);
    REQUIRE(s.sheet->status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(100 * 50 * 2));
    CHECK(s.sheet->radius.value == doctest::Approx(2));
    CHECK(s.sheet->radius.expression == "2");
    CHECK(s.sheet->kFactor.value == doctest::Approx(0.325));
    const auto info = model.Sheet(s.d, s.sheet->id);
    REQUIRE(info);
    CHECK(info->part.base.Area() == doctest::Approx(5000));
}

TEST_CASE("U channel: two flanges up, exact volume and size") {
    Sheet s;
    s.Flange({50, 0, 0});
    auto& second = s.Flange({50, 50, 2});   // the top edge works as well as the bottom one
    kernel::Model model;
    model.Rebuild(s.d);
    REQUIRE(second.status.state == FeatureState::Ok);
    // Base 100 x 42 x 2, two quarter tubes, two strips 16 x 2 x 100.
    CHECK(model.Volume() == doctest::Approx(100 * 42 * 2 + 2 * (kBendVolume + 16 * 2 * 100)).epsilon(1e-6));
    const auto box = model.Properties()->bounds;
    CHECK(box.min.y == doctest::Approx(0).epsilon(1e-6));
    CHECK(box.max.y == doctest::Approx(50));
    CHECK(box.min.z == doctest::Approx(0).epsilon(1e-6));
    CHECK(box.max.z == doctest::Approx(20));

    const auto info = model.Sheet(s.d, s.sheet->id);
    REQUIRE(info);
    const sheet::FlatPattern flat = sheet::Flatten(info->part, info->layout);
    const Box2 fb = flat.Bounds();
    CHECK(fb.max.y - fb.min.y == doctest::Approx(42 + 2 * (kPi / 2 * (2 + 0.325 * 2) + 16)));
    CHECK(sheet::FlatPatternDxf(flat).find("AC1009") != std::string::npos);
}

TEST_CASE("a new thickness brings the bend radius and K-factor with it") {
    Sheet s;
    s.Flange({50, 0, 0});
    kernel::Model model;
    model.Rebuild(s.d);
    s.sheet->thickness.expression = "3";
    s.d.MarkChanged(static_cast<std::size_t>(s.d.IndexOf(s.sheet->id)));
    model.Rebuild(s.d);
    REQUIRE(s.d.Features().back()->status.state == FeatureState::Ok);
    CHECK(s.sheet->radius.value == doctest::Approx(3));
    const auto info = model.Sheet(s.d, s.sheet->id);
    REQUIRE(info);
    CHECK(info->layout.flanges[0].setback == doctest::Approx(6));   // R + s
    CHECK(model.Properties()->bounds.max.z == doctest::Approx(20));  // outer height kept

    // A typed radius stays when the thickness changes again.
    s.sheet->radiusAuto = false;
    s.sheet->radius.expression = "5";
    s.sheet->thickness.expression = "1";
    s.d.MarkChanged(static_cast<std::size_t>(s.d.IndexOf(s.sheet->id)));
    model.Rebuild(s.d);
    CHECK(s.sheet->radius.value == doctest::Approx(5));
    CHECK(s.sheet->kFactor.value == doctest::Approx(0.5));   // R / s = 5
}

TEST_CASE("a flange down and one at 45°") {
    Sheet s;
    s.Flange({100, 25, 0}, "20", true);
    auto& f45 = s.Flange({50, 50, 0}, "15", false, "45");
    kernel::Model model;
    model.Rebuild(s.d);
    REQUIRE(f45.status.state == FeatureState::Ok);
    const auto box = model.Properties()->bounds;
    CHECK(box.min.z == doctest::Approx(2 - 20));
    CHECK(box.max.x == doctest::Approx(100));
    CHECK(box.max.z > 2);
}

TEST_CASE("a flange after an ordinary operation, or too low, says why") {
    SUBCASE("too low") {
        Sheet s;
        auto& f = s.Flange({50, 0, 0}, "3");
        kernel::Model model;
        model.Rebuild(s.d);
        CHECK(f.status.state == FeatureState::Error);
        CHECK(f.status.message.find("меньше зоны сгиба") != std::string::npos);
        CHECK(model.Volume() == doctest::Approx(10000));   // the plate stays
    }
    SUBCASE("after an extrusion") {
        Sheet s;
        auto& sk = AddSketch(s.d);
        AddCircle(s.d, sk, {50, 25}, 5);
        AddExtrude(s.d, sk.id, "10");
        auto& f = s.Flange({50, 0, 0});
        kernel::Model model;
        model.Rebuild(s.d);
        CHECK(f.status.state == FeatureState::Error);
        CHECK(f.status.message.find("перенесите её выше") != std::string::npos);
    }
}

TEST_CASE("the flange follows its edge when the sketch grows, and the file keeps it") {
    Sheet s;
    auto& f = s.Flange({50, 0, 0});
    kernel::Model model;
    model.Rebuild(s.d);
    REQUIRE(f.status.state == FeatureState::Ok);
    CHECK(f.edge.x == doctest::Approx(50));
    const std::string text = doc::DocumentToJson(s.d);
    CHECK(text.find("sheetFlange") != std::string::npos);
    CHECK(text.find("sheetMetal") != std::string::npos);
    auto loaded = doc::DocumentFromJson(text);
    REQUIRE(loaded.HasValue());
    kernel::Model again;
    again.Rebuild(*loaded);
    CHECK(loaded->Features().back()->status.state == FeatureState::Ok);
    CHECK(again.Volume() == doctest::Approx(model.Volume()));
}

TEST_CASE("a flange on a flange: C profile body") {
    Sheet s;
    auto& wall = s.Flange({50, 0, 0}, "25");
    auto& hem = s.OnFlange(wall, "10");
    kernel::Model model;
    model.Rebuild(s.d);
    REQUIRE(hem.status.state == FeatureState::Ok);
    // Base 100 x 46 x 2, two quarter tubes, strips 17 and 6 long.
    CHECK(model.Volume() == doctest::Approx(100 * 46 * 2 + 2 * kBendVolume + (17 + 6) * 2 * 100).epsilon(1e-6));
    const auto box = model.Properties()->bounds;
    CHECK(box.max.z == doctest::Approx(25));
    CHECK(box.min.y == doctest::Approx(0).epsilon(1e-6));
    CHECK(hem.edge.z == doctest::Approx(21));   // shown at the middle of the wall's far edge

    // The wall gets taller: the hem follows.
    wall.height.expression = "40";
    s.d.MarkChanged(static_cast<std::size_t>(s.d.IndexOf(wall.id)));
    model.Rebuild(s.d);
    REQUIRE(hem.status.state == FeatureState::Ok);
    CHECK(model.Properties()->bounds.max.z == doctest::Approx(40));

    // Without the wall the hem has nothing to stand on.
    wall.suppressed = true;
    s.d.MarkChanged(static_cast<std::size_t>(s.d.IndexOf(wall.id)));
    model.Rebuild(s.d);
    CHECK(hem.status.state == FeatureState::Error);

    // The file keeps it.
    wall.suppressed = false;
    s.d.MarkChanged(0);
    const std::string text = doc::DocumentToJson(s.d);
    CHECK(text.find("onFlange") != std::string::npos);
    auto loaded = doc::DocumentFromJson(text);
    REQUIRE(loaded.HasValue());
    kernel::Model again;
    again.Rebuild(*loaded);
    CHECK(loaded->Features().back()->status.state == FeatureState::Ok);
}

TEST_CASE("a flange on part of an edge, a corner tab and cuts: the body") {
    SUBCASE("offsets with reliefs") {
        Sheet s;
        auto& f = s.Flange({50, 0, 0});
        f.widthMode = doc::FlangeWidth::Offsets;
        f.offset1.expression = "20";
        f.offset2.expression = "30";
        kernel::Model model;
        model.Rebuild(s.d);
        REQUIRE(f.status.state == FeatureState::Ok);
        const double bend = kPi / 4 * (4.0 * 4.0 - 2.0 * 2.0) * 50;
        CHECK(model.Volume() == doctest::Approx((5000 - 50 * 4 - 2 * 2 * 5) * 2 + bend + 16 * 2 * 50).epsilon(1e-6));
        const auto box = model.Properties()->bounds;
        CHECK(box.min.y == doctest::Approx(0).epsilon(1e-6));   // the plate keeps its corners
        CHECK(box.max.z == doctest::Approx(20));
    }
    SUBCASE("a tab on the wall's side") {
        Sheet s;
        auto& wall = s.Flange({50, 0, 0}, "25");
        auto& tab = s.OnFlange(wall, "15");
        tab.onEdge = sheet::FlangeEdge::Start;
        kernel::Model model;
        model.Rebuild(s.d);
        REQUIRE(tab.status.state == FeatureState::Ok);
        CHECK(model.Properties()->bounds.min.x == doctest::Approx(-4));
        CHECK(model.Properties()->bounds.max.z == doctest::Approx(25));
    }
    SUBCASE("a hole in the wall from a sketch on its outer face, and one in the plate") {
        Sheet s;
        s.Flange({50, 0, 0}, "25");
        kernel::Model model;
        model.Rebuild(s.d);
        const double before = model.Volume();
        auto& onWall = AddSketch(s.d, doc::origin::kPlaneXZ);   // y = 0: the wall's outer face
        AddCircle(s.d, onWall, {30, 14}, 3);
        auto cut = std::make_unique<doc::SheetCutFeature>();
        cut->id = s.d.Ids().Next();
        cut->name = "Вырез1";
        cut->sketch = onWall.id;
        cut->sheet = s.sheet->id;
        auto& wallCut = *cut;
        s.d.Insert(std::move(cut), s.d.FeatureCount());
        auto& onPlate = AddSketch(s.d);
        AddCircle(s.d, onPlate, {50, 25}, 5);
        auto cut2 = std::make_unique<doc::SheetCutFeature>();
        cut2->id = s.d.Ids().Next();
        cut2->name = "Вырез2";
        cut2->sketch = onPlate.id;
        cut2->sheet = s.sheet->id;
        auto& plateCut = *cut2;
        s.d.Insert(std::move(cut2), s.d.FeatureCount());
        model.Rebuild(s.d);
        REQUIRE(wallCut.status.state == FeatureState::Ok);
        REQUIRE(plateCut.status.state == FeatureState::Ok);
        CHECK(model.Volume() == doctest::Approx(before - kPi * 9 * 2 - kPi * 25 * 2).epsilon(1e-6));
        const auto info = model.Sheet(s.d, s.sheet->id);
        REQUIRE(info);
        const sheet::FlatPattern flat = sheet::Flatten(info->part, info->layout);
        REQUIRE(flat.outline.holes.size() == 2);
        CHECK(flat.outline.holes[1][0].center.x == doctest::Approx(30));
        CHECK(flat.outline.holes[1][0].center.y == doctest::Approx(4 - kPi / 2 * (2 + 0.65) - 10));
        // The file keeps them.
        auto loaded = doc::DocumentFromJson(doc::DocumentToJson(s.d));
        REQUIRE(loaded.HasValue());
        kernel::Model again;
        again.Rebuild(*loaded);
        CHECK(again.Volume() == doctest::Approx(model.Volume()));
    }
}

TEST_CASE("a flange that goes into another one is reported, the part is still built") {
    Sheet s;
    auto& wall = s.Flange({50, 0, 0}, "25");
    kernel::Model model;
    model.Rebuild(s.d);
    REQUIRE(wall.status.state == FeatureState::Ok);
    // A hem 60 mm long, bent down from the wall's top: it lies over the plate... and through nothing.
    auto& longHem = s.OnFlange(wall, "60");
    model.Rebuild(s.d);
    CHECK(longHem.status.state == FeatureState::Ok);   // above the plate, 25 mm up: no clash
    // A wall on the far side, then a hem from the first wall long enough to reach it.
    auto& back = s.Flange({50, 50, 0}, "40");
    longHem.height.expression = "60";
    s.d.MarkChanged(0);
    model.Rebuild(s.d);
    CHECK(back.status.state == FeatureState::Warning);
    CHECK(back.status.message.find("врезается") != std::string::npos);
    CHECK(model.Volume() > 0);
    // A tab on the wall's side next to a flange on the plate's side edge: they share the corner.
    Sheet t;
    auto& w = t.Flange({50, 0, 0}, "25");
    t.Flange({0, 25, 0}, "25");   // the left side, up
    auto& tab = t.OnFlange(w, "15");
    tab.onEdge = sheet::FlangeEdge::Start;
    kernel::Model m2;
    m2.Rebuild(t.d);
    CHECK(tab.status.state == FeatureState::Warning);
}
