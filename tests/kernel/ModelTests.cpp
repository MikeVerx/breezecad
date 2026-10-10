#include "core/PathText.h"
#include "../Fixtures.h"
#include "document/BzcFile.h"
#include "kernel/Model.h"
#include "kernel/TextGeometry.h"
#include "sketch/Solver.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

using namespace bz;
using namespace bz::test;
using doc::BooleanOp;
using doc::FeatureState;

TEST_CASE("a rectangle extrudes into a box") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {0, 0}, {60, 40});
    auto& ex = AddExtrude(d, sk.id, "10");

    kernel::Model model;
    model.Rebuild(d);
    CHECK(sk.status.state == FeatureState::Ok);
    CHECK(ex.status.state == FeatureState::Ok);
    REQUIRE(model.Bodies().size() == 1);
    CHECK(model.Bodies()[0].name == "Тело1");
    CHECK(model.Volume() == doctest::Approx(24000.0));
    const auto box = model.BoundingBox();
    REQUIRE(box);
    CHECK(box->max.z == doctest::Approx(10.0).epsilon(1e-3));
}

TEST_CASE("automatic profile keeps inner circles as holes") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {0, 0}, {60, 40});
    AddCircle(d, sk, {30, 20}, 8);
    AddExtrude(d, sk.id, "10");

    kernel::Model model;
    model.Rebuild(d);
    const auto* regions = model.Regions(sk.id);
    REQUIRE(regions);
    CHECK(regions->size() == 2);   // the plate with a hole, and the disc
    CHECK(model.Volume() == doctest::Approx(24000.0 - kPi * 64.0 * 10.0).epsilon(1e-4));
}

TEST_CASE("manual profile picks the region under a point") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {0, 0}, {60, 40});
    AddCircle(d, sk, {30, 20}, 8);
    auto& ex = AddExtrude(d, sk.id, "5");
    ex.profiles.automatic = false;
    ex.profiles.regions = {{30, 20}};   // the disc only

    kernel::Model model;
    model.Rebuild(d);
    CHECK(model.Volume() == doctest::Approx(kPi * 64.0 * 5.0).epsilon(1e-4));
}

TEST_CASE("a crossing line splits a rectangle into two regions") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {0, 0}, {60, 40});
    auto& s = sk.sketch;
    s.AddLine(d.Ids(), s.AddPoint(d.Ids(), {20, -10}), s.AddPoint(d.Ids(), {20, 50}));
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(model.Regions(sk.id));
    CHECK(model.Regions(sk.id)->size() == 2);
}

TEST_CASE("cut through all, join and errors") {
    doc::Document d;
    auto& base = AddSketch(d);
    AddRect(d, base, {0, 0}, {60, 40});
    AddExtrude(d, base.id, "10");

    auto& top = AddSketch(d, doc::origin::kPlaneXY);
    AddCircle(d, top, {30, 20}, 5);
    auto& cut = AddExtrude(d, top.id, "1", BooleanOp::Cut);
    cut.extent = doc::ExtentType::ThroughAll;
    cut.extent = doc::ExtentType::ThroughAll;

    kernel::Model model;
    model.Rebuild(d);
    CHECK(cut.status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(24000.0 - kPi * 25.0 * 10.0).epsilon(1e-4));
    CHECK(model.Bodies().size() == 1);

    // A boss on top joins the same body.
    auto& boss = AddSketch(d);
    AddRect(d, boss, {0, 0}, {10, 10});
    auto& join = AddExtrude(d, boss.id, "20", BooleanOp::Join);
    model.Rebuild(d);
    CHECK(join.status.state == FeatureState::Ok);
    CHECK(model.Bodies().size() == 1);
    CHECK(model.Volume() == doctest::Approx(24000.0 - kPi * 250.0 + 1000.0).epsilon(1e-4));

    // Zero distance is an error on that feature only.
    join.distance.expression = "0";
    d.MarkChanged(static_cast<std::size_t>(d.IndexOf(join.id)));
    model.Rebuild(d);
    CHECK(join.status.state == FeatureState::Error);
    CHECK(cut.status.state == FeatureState::Ok);
    CHECK(model.Bodies().size() == 1);
}

TEST_CASE("symmetric, reversed and tapered extrusions") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {0, 0}, {20, 20});
    auto& ex = AddExtrude(d, sk.id, "10");
    ex.extent = doc::ExtentType::Symmetric;
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(model.BoundingBox());
    CHECK(model.BoundingBox()->min.z == doctest::Approx(-5.0).epsilon(1e-3));

    ex.extent = doc::ExtentType::Distance;
    ex.reversed = true;
    d.MarkChanged(0);
    model.Rebuild(d);
    CHECK(model.BoundingBox()->min.z == doctest::Approx(-10.0).epsilon(1e-3));

    ex.reversed = false;
    ex.taper.expression = "5";
    d.MarkChanged(0);
    model.Rebuild(d);
    CHECK(ex.status.state == FeatureState::Ok);
    CHECK(model.Volume() < 4000.0);
    CHECK(model.Volume() > 3000.0);
}

TEST_CASE("rebuild after reloading gives the same solid") {
    doc::Document d;
    auto& sk = AddSketch(d, doc::origin::kPlaneXZ);
    AddRect(d, sk, {0, 0}, {30, 20});
    AddCircle(d, sk, {15, 10}, 4);
    AddExtrude(d, sk.id, "12");
    kernel::Model a;
    a.Rebuild(d);

    Expected<doc::Document> loaded = doc::DocumentFromJson(doc::DocumentToJson(d));
    REQUIRE(loaded.HasValue());
    kernel::Model b;
    b.Rebuild(*loaded);
    CHECK(b.Volume() == doctest::Approx(a.Volume()));
    CHECK(b.BoundingBox()->min.y == doctest::Approx(-12.0).epsilon(1e-3));   // front plane extrudes toward -Y
}

TEST_CASE("export STEP, STL and 3MF") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {0, 0}, {20, 10});
    AddExtrude(d, sk.id, "5");
    kernel::Model model;
    model.Rebuild(d);

    const auto dir = std::filesystem::temp_directory_path() / "bz_export_tests";
    std::filesystem::create_directories(dir);
    const auto step = dir / PathFromUtf8("деталь.step");
    const auto stl = dir / PathFromUtf8("деталь.stl");
    const auto mf3 = dir / PathFromUtf8("деталь.3mf");
    REQUIRE(model.ExportStep(step).IsOk());
    REQUIRE(model.ExportStl(stl).IsOk());
    REQUIRE(model.Export3mf(mf3).IsOk());

    {   // closed before the folder is removed: Windows cannot delete open files
        std::ifstream in(step);
        const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        CHECK(text.starts_with("ISO-10303-21"));
        CHECK(text.find("'BreezeCAD'") != std::string::npos);   // originating system in the header
    }
    CHECK(std::filesystem::file_size(stl) == 84 + 50 * 12);   // a box: 6 faces x 2 triangles
    CHECK(std::filesystem::file_size(mf3) > 200);
    std::filesystem::remove_all(dir);

    doc::Document empty;
    kernel::Model none;
    none.Rebuild(empty);
    CHECK_FALSE(none.ExportStl(dir / "x.stl").IsOk());
}

TEST_CASE("mass properties of two separate bodies") {
    doc::Document d;
    auto& a = AddSketch(d);
    AddRect(d, a, {0, 0}, {20, 10});
    AddExtrude(d, a.id, "5");
    auto& b = AddSketch(d);
    AddRect(d, b, {40, 0}, {50, 10});
    AddExtrude(d, b.id, "5").operation = doc::BooleanOp::NewBody;
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(model.Bodies().size() == 2);

    const auto p = model.Properties();
    REQUIRE(p);
    CHECK(p->volume == doctest::Approx(1000.0 + 500.0));
    CHECK(p->area == doctest::Approx(2 * (200 + 100 + 50) + 2 * (100 + 50 + 50)));
    // Centre of volume: weighted by volume along X.
    CHECK(p->centre.x == doctest::Approx((1000.0 * 10 + 500.0 * 45) / 1500.0));
    CHECK(p->centre.z == doctest::Approx(2.5));
    CHECK(p->bounds.min.x == doctest::Approx(0.0));
    CHECK(p->bounds.max.x == doctest::Approx(50.0));   // exact, not padded by tolerance
    CHECK(p->bounds.max.z == doctest::Approx(5.0));

    kernel::Model none;
    doc::Document empty;
    none.Rebuild(empty);
    CHECK_FALSE(none.Properties());
}

TEST_CASE("a closed spline extrudes into a body") {
    doc::Document d;
    auto& sk = bz::test::AddSketch(d);
    IdGenerator& ids = d.Ids();
    std::vector<ObjectId> pts;
    for (const Vec2 p : {Vec2{0, 0}, Vec2{30, -5}, Vec2{40, 20}, Vec2{10, 30}}) pts.push_back(sk.sketch.AddPoint(ids, p));
    pts.push_back(pts.front());
    sk.sketch.AddSpline(ids, pts, true);
    bz::test::AddExtrude(d, sk.id, "10");
    kernel::Model model;
    model.Rebuild(d);
    CHECK(model.Volume() > 5000.0);   // roughly 30 x 30 x 10, smooth corners
}

TEST_CASE("sketch text extrudes as letters with their holes") {
    const std::filesystem::path fonts = std::filesystem::path(BZ_SOURCE_DIR) / "assets/fonts";
    kernel::SetTextFont(fonts / "IBMPlexSans-Medium.ttf", fonts / "IBMPlexSans-SemiBold.ttf");
    const kernel::TextFormat format{"BZ 08", 10.0, {}, false, false, 0, 0};
    const Vec2 size = kernel::TextExtent(format);
    REQUIRE(size.x > 20.0);
    doc::Document d;
    auto& sk = bz::test::AddSketch(d);
    const ObjectId text = sk.sketch.AddText(d.Ids(), {0, 0}, size, "BZ 08", 10.0);
    bz::test::AddExtrude(d, sk.id, "2");
    kernel::Model model;
    model.Rebuild(d);
    const double plain = model.Volume();
    // Letters, not their box: well below 4 glyphs x 7 x 10 x 2 mm.
    CHECK(plain > 50.0);
    CHECK(plain < 400.0);
    // Bold letters are fatter; centring in a wider box moves, not changes them.
    sk.sketch.FindText(text)->bold = true;
    d.MarkChanged(0);
    model.Rebuild(d);
    CHECK(model.Volume() > plain * 1.05);
}

TEST_CASE("an ellipse extrudes into an elliptic cylinder; its axes stay perpendicular") {
    doc::Document d;
    auto& sk = bz::test::AddSketch(d);
    IdGenerator& ids = d.Ids();
    const ObjectId c = sk.sketch.AddPoint(ids, {0, 0});
    const ObjectId m = sk.sketch.AddPoint(ids, {20, 0});
    const ObjectId n = sk.sketch.AddPoint(ids, {0.5, 10});   // a little off: the solver squares it
    sk.sketch.AddEllipse(ids, c, m, n);
    REQUIRE(sketch::Solve(sk.sketch).converged);
    const Vec2 u = sk.sketch.PositionOf(m) - sk.sketch.PositionOf(c), v = sk.sketch.PositionOf(n) - sk.sketch.PositionOf(c);
    CHECK(std::abs(u.Dot(v)) < 1e-6);
    bz::test::AddExtrude(d, sk.id, "5");
    kernel::Model model;
    model.Rebuild(d);
    CHECK(model.Volume() == doctest::Approx(kPi * u.Length() * v.Length() * 5).epsilon(1e-3));
}

TEST_CASE("interferences: overlapping bodies are found, touching ones are not") {
    doc::Document d;
    auto& a = AddSketch(d);
    AddRect(d, a, {0, 0}, {10, 10});
    AddExtrude(d, a.id, "10");
    auto& b = AddSketch(d);
    AddRect(d, b, {8, 0}, {18, 10});   // 2 mm into the first
    AddExtrude(d, b.id, "10");
    auto& c = AddSketch(d);
    AddRect(d, c, {-10, 0}, {0, 10});   // touches the first along a face
    AddExtrude(d, c.id, "10");
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(model.Bodies().size() == 3);
    const auto& found = model.Interferences();
    REQUIRE(found.size() == 1);
    CHECK(found[0].first == 0);
    CHECK(found[0].second == 1);
    CHECK(found[0].volume == doctest::Approx(200));
    CHECK(found[0].centre.x == doctest::Approx(9));
}

TEST_CASE("a forked model rebuilds on another thread and is adopted") {
    doc::Document d;
    auto& base = AddSketch(d);
    AddRect(d, base, {0, 0}, {60, 40});
    AddExtrude(d, base.id, "10");
    auto& pocket = AddSketch(d);
    AddRect(d, pocket, {10, 10}, {20, 20});
    auto& cut = AddExtrude(d, pocket.id, "4", BooleanOp::Cut);

    kernel::Model model;
    model.Rebuild(d);
    const double before = model.Volume();
    CHECK(before == doctest::Approx(24000.0 - 400.0));

    // Deeper pocket: only the cut has to be rebuilt.
    cut.distance.expression = "6";
    d.MarkChanged(static_cast<std::size_t>(d.IndexOf(cut.id)));
    const std::size_t start = model.FirstToRebuild(d);
    CHECK(start == static_cast<std::size_t>(d.IndexOf(cut.id)));

    doc::Document copy = d.Clone();
    kernel::Model fork = model.Fork(start);
    std::thread worker([&] { fork.Rebuild(copy); });
    worker.join();
    CHECK(model.Volume() == doctest::Approx(before));   // untouched meanwhile

    d.TakeRebuilt(copy);
    model.Adopt(std::move(fork), start);
    CHECK(d.DirtyFrom() == d.FeatureCount());
    CHECK(cut.distance.value == doctest::Approx(6.0));
    CHECK(cut.status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(24000.0 - 600.0));
    // Later rebuilds go on from the adopted results.
    cut.distance.expression = "5";
    d.MarkChanged(static_cast<std::size_t>(d.IndexOf(cut.id)));
    model.Rebuild(d);
    CHECK(model.Volume() == doctest::Approx(24000.0 - 500.0));
}

TEST_CASE("extrude up to a curved face") {
    // A bar lying along Y (radius 20, axis at height 40), a 10x10 post
    // from the XY plane up to its underside.
    doc::Document d;
    auto& barSketch = AddSketch(d, doc::origin::kPlaneXZ);
    AddCircle(d, barSketch, {0, 40}, 20);
    auto& bar = AddExtrude(d, barSketch.id, "60");
    bar.extent = doc::ExtentType::Symmetric;
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(bar.status.state == FeatureState::Ok);
    const auto box = model.BoundingBox();
    REQUIRE(box);
    REQUIRE(std::abs(box->min.z - 20) < 1e-3);   // the circle went where the test means it to

    auto& postSketch = AddSketch(d);
    AddRect(d, postSketch, {-5, -5}, {5, 5});
    auto& post = AddExtrude(d, postSketch.id, "1", BooleanOp::NewBody);
    post.extent = doc::ExtentType::ToPlane;
    post.toFace = {.normal = {0, 0, -1}, .point = {0, 0, 20}, .curved = true};
    model.Rebuild(d);
    REQUIRE(post.status.state == FeatureState::Ok);
    // Height under the bar: 40 - sqrt(400 - x²), the same along y.
    double area = 0;
    constexpr int n = 2000;
    for (int i = 0; i < n; ++i) {
        const double x = -5 + 10.0 * (i + 0.5) / n;
        area += (40 - std::sqrt(400 - x * x)) * 10.0 / n;
    }
    const double bars = kPi * 400 * 60;
    CHECK(model.Volume() - bars == doctest::Approx(area * 10).epsilon(1e-3));

    // A bar narrower than the post: its surface carries on past its ends.
    bar.distance.expression = "6";
    d.MarkChanged(static_cast<std::size_t>(d.IndexOf(bar.id)));
    model.Rebuild(d);
    REQUIRE(post.status.state == FeatureState::Ok);
    CHECK(model.Volume() - kPi * 400 * 6 == doctest::Approx(area * 10).epsilon(1e-3));
}
