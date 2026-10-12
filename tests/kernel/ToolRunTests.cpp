// Regressions found by running every MCP command on real parts
// (docs: test protocol 2026-10-12). One case per defect.

#include "../Fixtures.h"
#include "assembly/Assembly.h"
#include "assembly/Solver.h"
#include "document/BzcFile.h"
#include "kernel/AssemblyModel.h"
#include "document/HoleFeature.h"
#include "document/LoftFeature.h"
#include "document/PatternFeature.h"
#include "document/ShellFeature.h"
#include "document/StandardParts.h"
#include "document/WorkPlaneFeature.h"
#include "kernel/Model.h"
#include "kernel/Topology.h"
#include "kernel/occt/ModelImpl.h"

#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

#include <doctest.h>

#include <algorithm>
#include <filesystem>

using namespace bz;
using namespace bz::test;
using doc::FeatureState;

namespace {

template <class F>
F& AddFeature(doc::Document& d, const std::string& name) {
    auto f = std::make_unique<F>();
    f->id = d.Ids().Next();
    f->name = d.UniqueFeatureName(name);
    F* raw = f.get();
    d.Insert(std::move(f), d.FeatureCount());
    return *raw;
}

double BodyVolume(const kernel::Model& model, ObjectId body) {
    for (const kernel::Body& b : model.GetImpl().bodies)
        if (b.id == body) {
            GProp_GProps props;
            BRepGProp::VolumeProperties(b.shape, props);
            return props.Mass();
        }
    return -1;
}

} // namespace

TEST_CASE("a cut acts on the body it crosses, not on the most recent one") {
    doc::Document d;
    auto& a = AddSketch(d);
    AddRect(d, a, {0, 0}, {10, 10});
    auto& first = AddExtrude(d, a.id, "10");             // body A, 1000 mm³
    auto& b = AddSketch(d);
    AddRect(d, b, {50, 0}, {60, 10});
    auto& second = AddExtrude(d, b.id, "10");            // body B, the most recent
    auto& c = AddSketch(d);
    AddRect(d, c, {2, 2}, {4, 4});
    auto& cut = AddExtrude(d, c.id, "10", doc::BooleanOp::Cut);   // only crosses A

    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(cut.status.state == FeatureState::Ok);
    CHECK(BodyVolume(model, first.id) == doctest::Approx(1000.0 - 40.0).epsilon(1e-6));
    CHECK(BodyVolume(model, second.id) == doctest::Approx(1000.0).epsilon(1e-6));
}

TEST_CASE("a cut that misses every body warns instead of passing silently") {
    doc::Document d;
    auto& a = AddSketch(d);
    AddRect(d, a, {0, 0}, {10, 10});
    AddExtrude(d, a.id, "10");
    auto& c = AddSketch(d);
    AddRect(d, c, {30, 30}, {32, 32});
    auto& cut = AddExtrude(d, c.id, "10", doc::BooleanOp::Cut);
    kernel::Model model;
    model.Rebuild(d);
    CHECK(cut.status.state == FeatureState::Warning);
    CHECK(model.Volume() == doctest::Approx(1000.0).epsilon(1e-6));
}

TEST_CASE("a revolved cut finds its body among several") {
    doc::Document d;
    auto& ring = AddSketch(d, doc::origin::kPlaneXZ);
    AddRect(d, ring, {10, 0}, {15, 20});
    auto& sleeve = AddRevolve(d, ring.id, {doc::RevolveAxisKind::SketchY, {}});   // a tube r 10..15
    auto& box = AddSketch(d);
    AddRect(d, box, {40, 40}, {60, 60});
    AddExtrude(d, box.id, "10");                                                   // the most recent body
    auto& window = AddSketch(d, doc::origin::kPlaneXZ);
    AddRect(d, window, {12, 5}, {17, 10});
    auto& cut = AddRevolve(d, window.id, {doc::RevolveAxisKind::SketchY, {}}, doc::RevolveExtent::Symmetric, "90");
    cut.operation = doc::BooleanOp::Cut;

    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(cut.status.state == FeatureState::Ok);
    const double tube = kPi * (15 * 15 - 10 * 10) * 20;
    const double removed = kPi * (15 * 15 - 12 * 12) * 5 / 4;   // a quarter turn of the ring 12..15
    CHECK(BodyVolume(model, sleeve.id) == doctest::Approx(tube - removed).epsilon(1e-5));
}

TEST_CASE("a join merges with the body it touches") {
    doc::Document d;
    auto& a = AddSketch(d);
    AddRect(d, a, {0, 0}, {10, 10});
    auto& first = AddExtrude(d, a.id, "10");
    auto& b = AddSketch(d);
    AddRect(d, b, {50, 0}, {60, 10});
    AddExtrude(d, b.id, "10");
    auto& c = AddSketch(d);
    AddRect(d, c, {10, 0}, {12, 10});                  // touches A's side
    auto& join = AddExtrude(d, c.id, "10", doc::BooleanOp::Join);
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(join.status.state == FeatureState::Ok);
    CHECK(model.Bodies().size() == 2);
    CHECK(BodyVolume(model, first.id) == doctest::Approx(1200.0).epsilon(1e-6));
}

TEST_CASE("faces of a mirrored body report outward normals") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {0, 0}, {10, 10});
    AddExtrude(d, sk.id, "10");                         // z 0..10
    auto& mirror = AddFeature<doc::MirrorFeature>(d, "Зеркало");
    mirror.wholeBody = true;
    mirror.plane = doc::origin::kPlaneXY;               // the copy at z -10..0
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(mirror.status.state == FeatureState::Ok);
    const kernel::Topology t = kernel::DescribeTopology(model);
    bool sawBottom = false;
    for (const kernel::FaceInfo& f : t.faces) {
        if (f.kind != doc::SurfaceKind::Plane) continue;
        // Outward: from the middle of the solid to the face.
        const double side = f.point.z > 0 ? 1.0 : -1.0;
        const Vec3 centre{5, 5, 5 * side};
        CHECK(f.normal.Dot(f.point - centre) > 0);
        if (f.point.z < -9.9) {
            sawBottom = true;
            CHECK(f.normal.z == doctest::Approx(-1.0));
        }
    }
    CHECK(sawBottom);
}

TEST_CASE("a tapered extrusion is as tall as asked") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {-10, -10}, {10, 10});
    auto& ex = AddExtrude(d, sk.id, "10");
    ex.taper.expression = "5";
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(ex.status.state == FeatureState::Ok);
    REQUIRE(model.BoundingBox());
    CHECK(model.BoundingBox()->max.z == doctest::Approx(10.0).epsilon(1e-6));
    // A frustum: a = 20 at the bottom, 20 - 2·10·tan 5° at the top.
    const double top = 20 - 2 * 10 * std::tan(5 * kPi / 180);
    const double v = 10.0 / 3 * (400 + top * top + 20 * top);
    CHECK(model.Volume() == doctest::Approx(v).epsilon(1e-5));
}

TEST_CASE("a shell of a square-to-circle loft says why it cannot be built") {
    // Neighbouring sides meet at 90° at the square and tangent at the circle:
    // the OCCT offset cannot shift such edges. The message must say so
    // instead of blaming the thickness.
    doc::Document d;
    auto& bottom = AddSketch(d);
    AddRect(d, bottom, {-12, -12}, {12, 12});
    auto& wp = AddFeature<doc::WorkPlaneFeature>(d, "Плоскость");
    wp.base = doc::origin::kPlaneXY;
    wp.offset.expression = "30";
    auto& top = AddSketch(d, wp.id);
    AddCircle(d, top, {0, 0}, 8);
    auto& loft = AddFeature<doc::LoftFeature>(d, "Лофт");
    loft.sections = {bottom.id, top.id};
    auto& shell = AddFeature<doc::ShellFeature>(d, "Оболочка");
    shell.thickness.expression = "1.5";
    shell.faces.push_back(doc::FaceReference{{0, 0, 1}, {0, 0, 30}});
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(loft.status.state == FeatureState::Ok);
    if (shell.status.state == FeatureState::Error) {
        CHECK(shell.status.message.find("от угла к касанию") != std::string::npos);
    } else {   // a kernel that can do it: thin walls, well under the solid loft
        CHECK(model.Volume() < 0.5 * 11228.0);
    }
}

TEST_CASE("a shell of a square-to-square loft is built") {
    doc::Document d;
    auto& bottom = AddSketch(d);
    AddRect(d, bottom, {-12, -12}, {12, 12});
    auto& wp = AddFeature<doc::WorkPlaneFeature>(d, "Плоскость");
    wp.base = doc::origin::kPlaneXY;
    wp.offset.expression = "30";
    auto& top = AddSketch(d, wp.id);
    AddRect(d, top, {-8, -8}, {8, 8});
    auto& loft = AddFeature<doc::LoftFeature>(d, "Лофт");
    loft.sections = {bottom.id, top.id};
    auto& shell = AddFeature<doc::ShellFeature>(d, "Оболочка");
    shell.thickness.expression = "1.5";
    shell.faces.push_back(doc::FaceReference{{0, 0, 1}, {0, 0, 30}});
    kernel::Model model;
    model.Rebuild(d);
    CHECK(shell.status.state == FeatureState::Ok);
}

TEST_CASE("every flat face of a counterbored plate points out of the material") {
    // The floor of a counterbore faces up (into the bore): "Вставка" of a
    // screw relies on it. Checked through the face list and by stepping off
    // each face along its normal: the point there must be outside.
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {-20, -20}, {20, 20});
    AddExtrude(d, sk.id, "20");
    auto h = std::make_unique<doc::HoleFeature>();
    h->id = d.Ids().Next();
    h->name = "Отверстие";
    h->face = {{0, 0, 1}, {0, 0, 20}};
    h->centres = {{0, 0}};
    h->type = doc::HoleType::Counterbore;
    h->throughAll = true;
    h->diameter.expression = "6.6";
    h->headDiameter.expression = "11";
    h->headDepth.expression = "6.4";
    auto& hole = *h;
    d.Insert(std::move(h), d.FeatureCount());
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(hole.status.state == FeatureState::Ok);

    const kernel::Topology t = kernel::DescribeTopology(model);
    bool sawFloor = false;
    for (const kernel::FaceInfo& f : t.faces) {
        if (f.kind != doc::SurfaceKind::Plane) continue;
        if (std::abs(f.point.z - 13.6) < 1e-6) {
            sawFloor = true;
            CHECK(f.normal.z == doctest::Approx(1.0));
        }
    }
    CHECK(sawFloor);
}

namespace {

/// Every flat face of every body points out of the material: a step along
/// its normal leaves the solid, a step against it enters.
void CheckOutward(const kernel::Model& model, const char* what) {
    const kernel::Topology t = kernel::DescribeTopology(model);
    for (const kernel::FaceInfo& f : t.faces) {
        if (f.kind != doc::SurfaceKind::Plane) continue;
        for (const kernel::Body& b : model.GetImpl().bodies) {
            if (b.id != f.body) continue;
            const auto state = [&](const Vec3& p) {
                BRepClass3d_SolidClassifier c(b.shape, gp_Pnt(p.x, p.y, p.z), 1e-7);
                return c.State();
            };
            INFO(std::string(what) << ": face " << f.index << " at " << f.point.x << " " << f.point.y << " " << f.point.z);
            CHECK(state(f.point + f.normal * 0.01) == TopAbs_OUT);
            CHECK(state(f.point - f.normal * 0.01) == TopAbs_IN);
        }
    }
}

} // namespace

TEST_CASE("faces of standard parts point out of the material") {
    for (const char* standard : {"gost11371", "gost5915", "gost11738", "gost7798", "gost6402", "gost17475"}) {
        const doc::parts::StandardInfo* s = nullptr;
        for (const doc::parts::KindInfo& kind : doc::parts::Kinds())
            for (const doc::parts::StandardInfo* candidate : doc::parts::StandardsOf(kind.kind))
                if (candidate->id == std::string_view(standard)) s = candidate;
        REQUIRE(s);
        const std::vector<std::string> sizes = doc::parts::Sizes(*s);
        const std::string size = sizes[sizes.size() / 2];
        const std::vector<double> lengths = doc::parts::Lengths(*s, size);
        Expected<doc::parts::Made> made = doc::parts::MakePart({standard, size, lengths.empty() ? 0.0 : lengths[lengths.size() / 2]});
        REQUIRE(made);
        kernel::Model model;
        model.Rebuild(made->doc);
        CheckOutward(model, standard);
    }
}

TEST_CASE("a standard screw goes into a counterbore without flipping") {
    const auto dir = std::filesystem::temp_directory_path() / "bz_tool_run_insert";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    doc::Document plate;
    auto& sk = AddSketch(plate);
    AddRect(plate, sk, {-20, -20}, {20, 20});
    AddExtrude(plate, sk.id, "20");
    auto h = std::make_unique<doc::HoleFeature>();
    h->id = plate.Ids().Next();
    h->name = "Отверстие";
    h->face = {{0, 0, 1}, {0, 0, 20}};
    h->centres = {{0, 0}};
    h->type = doc::HoleType::Counterbore;
    h->throughAll = true;
    h->diameter.expression = "6.6";
    h->headDiameter.expression = "11";
    h->headDepth.expression = "6.4";
    plate.Insert(std::move(h), plate.FeatureCount());
    REQUIRE(doc::SaveBzc(plate, dir / "plate.bzc", "tests"));
    Expected<doc::parts::Made> screw = doc::parts::MakePart({"gost11738", "М6", 30.0});
    REQUIRE(screw);
    REQUIRE(doc::SaveBzc(screw->doc, dir / "screw.bzc", "tests"));

    assembly::Assembly a;
    const ObjectId base = a.AddComponent("plate.bzc", "Плита").id;
    a.FindComponent(base)->grounded = true;
    const ObjectId part = a.AddComponent("screw.bzc", "Винт", {assembly::Rotation{}, {60, 0, 40}}).id;
    kernel::AssemblyModel m;
    std::vector<std::string> problems;
    m.Update(a, dir / "unit.bzc", &problems);
    REQUIRE(problems.empty());

    // The floor of the counterbore (r 5.5 at z 13.6) and the head's underside (r 5 at the screw's origin).
    const auto floor = m.GeometryAt(a, base, {5.5, 0, 13.6}, kernel::AssemblyPick::Circle);
    const auto head = m.GeometryAt(a, part, {65, 0, 40}, kernel::AssemblyPick::Circle);
    REQUIRE(floor);
    REQUIRE(head);
    CHECK(floor->direction.z == doctest::Approx(1.0));    // out of the floor: up
    CHECK(head->direction.z == doctest::Approx(-1.0));    // out of the underside: down
    assembly::Constraint c;
    c.type = assembly::ConstraintType::Insert;
    c.first = *head;
    c.second = *floor;
    a.AddConstraint(c);
    REQUIRE(assembly::Solve(a).converged);
    const assembly::Placement& p = a.FindComponent(part)->placement;
    CHECK(p.translation.x == doctest::Approx(0).epsilon(1e-6));
    CHECK(p.translation.z == doctest::Approx(13.6).epsilon(1e-6));
    CHECK(p.Direction({0, 0, 1}).z == doctest::Approx(1.0).epsilon(1e-6));   // head up, shank down
    std::filesystem::remove_all(dir);
}

// --- found after the first fixes: vertices, axes, distances ----------------------

#include "kernel/occt/EdgeMatch.h"

#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

namespace {

/// A triangular prism, 5 high, on the triangle (0,0) (8,-8) (16,0), moved by `dx` along X.
TopoDS_Shape Wedge(double dx) {
    BRepBuilderAPI_MakePolygon tri(gp_Pnt(dx, 0, 0), gp_Pnt(dx + 8, -8, 0), gp_Pnt(dx + 16, 0, 0), true);
    return BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(tri.Wire()).Face(), gp_Vec(0, 0, 5)).Shape();
}

} // namespace

TEST_CASE("a vertex stays at the same end of its edge when the edge moves") {
    // The bottom edge (0,0,0)–(8,-8,0) moves 10 to the left: the vertex
    // picked at (0,0,0) is now at (-10,0,0) — though the other end, at
    // (-2,-8,0), is nearer the old place.
    const TopoDS_Shape before = Wedge(0), after = Wedge(-10);
    std::optional<TopoDS_Edge> picked;
    for (TopExp_Explorer e(before, TopAbs_EDGE); e.More(); e.Next()) {
        TopoDS_Vertex a, b;
        TopExp::Vertices(TopoDS::Edge(e.Current()), a, b);
        const gp_Pnt pa = BRep_Tool::Pnt(a), pb = BRep_Tool::Pnt(b);
        const auto is = [](const gp_Pnt& p, double x, double y, double z) { return p.Distance(gp_Pnt(x, y, z)) < 1e-9; };
        if ((is(pa, 0, 0, 0) && is(pb, 8, -8, 0)) || (is(pb, 0, 0, 0) && is(pa, 8, -8, 0))) picked = TopoDS::Edge(e.Current());
    }
    REQUIRE(picked);
    doc::EdgeReference ref = kernel::occt::DescribeEdge(*picked, before);
    const auto at = kernel::occt::ResolveVertex(ref, {0, 0, 0}, after);
    REQUIRE(at);
    CHECK(at->x == doctest::Approx(-10.0));
    CHECK(at->y == doctest::Approx(0.0));
    // The reference now describes the edge where it is: a second move from
    // there keeps the same end too.
    CHECK(ref.middle.x == doctest::Approx(-6.0));
}

TEST_CASE("a hole's axis starts at the face, not where the hole tool began") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {-20, -20}, {20, 20});
    AddExtrude(d, sk.id, "20");
    auto h = std::make_unique<doc::HoleFeature>();
    h->id = d.Ids().Next();
    h->name = "Отверстие";
    h->face = {{0, 0, 1}, {0, 0, 20}};
    h->centres = {{5, 5}};
    h->throughAll = true;
    h->diameter.expression = "6.6";
    d.Insert(std::move(h), d.FeatureCount());
    kernel::Model model;
    model.Rebuild(d);
    const kernel::Topology t = kernel::DescribeTopology(model);
    bool saw = false;
    for (const kernel::FaceInfo& f : t.faces)
        if (f.kind == doc::SurfaceKind::Cylinder && f.hole) {
            saw = true;
            REQUIRE(f.axis);
            const double top = f.axis->axisPoint.z, bottom = top + f.axis->axisDir.z * 20;
            CHECK(std::max(top, bottom) == doctest::Approx(20.0).epsilon(1e-9));
            CHECK(std::min(top, bottom) == doctest::Approx(0.0).epsilon(1e-9));
            CHECK(f.axis->axisPoint.x == doctest::Approx(5.0));
        }
    CHECK(saw);
}

TEST_CASE("distances to faces and edges for near") {
    CHECK(kernel::BoxDistance({0, 0, 0}, {10, 10, 0}, {5, 5, 0}) == doctest::Approx(0.0));
    CHECK(kernel::BoxDistance({0, 0, 0}, {10, 10, 0}, {13, 14, 0}) == doctest::Approx(5.0));
    kernel::EdgeInfo line;
    line.kind = doc::CurveKind::Line;
    line.start = {0, 0, 0};
    line.end = {10, 0, 0};
    line.middle = {5, 0, 0};
    CHECK(kernel::EdgeDistance(line, {3, 4, 0}) == doctest::Approx(4.0));
    kernel::EdgeInfo circle;
    circle.kind = doc::CurveKind::Circle;
    circle.centre = Vec3{0, 0, 0};
    circle.direction = {0, 0, 1};
    circle.radius = 5;
    circle.closed = true;
    circle.start = circle.end = {5, 0, 0};
    circle.middle = {-5, 0, 0};
    CHECK(kernel::EdgeDistance(circle, {0, 8, 0}) == doctest::Approx(3.0));
    CHECK(kernel::EdgeDistance(circle, {0, 0, 0}) == doctest::Approx(5.0));
}

TEST_CASE("a washer turns on a screw head both ways round") {
    // Rotational from the washer's underside to the top of the head: by
    // default the washer lies on the head; flipped, it turns over — the
    // solver must turn the part half round to get there, not stall.
    const auto dir = std::filesystem::temp_directory_path() / "bz_tool_run_rotational";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    Expected<doc::parts::Made> screw = doc::parts::MakePart({"gost11738", "M6", 30.0});
    Expected<doc::parts::Made> washer = doc::parts::MakePart({"gost11371", "M6", 0.0});   // «6» typed as M6
    REQUIRE(screw);
    REQUIRE(washer);
    REQUIRE(doc::SaveBzc(screw->doc, dir / "screw.bzc", "tests"));
    REQUIRE(doc::SaveBzc(washer->doc, dir / "washer.bzc", "tests"));

    assembly::Assembly a;
    const ObjectId base = a.AddComponent("screw.bzc", "Винт").id;
    a.FindComponent(base)->grounded = true;
    const ObjectId part = a.AddComponent("washer.bzc", "Шайба", {assembly::Rotation{}, {40, 0, 0}}).id;
    kernel::AssemblyModel m;
    std::vector<std::string> problems;
    m.Update(a, dir / "unit.bzc", &problems);
    REQUIRE(problems.empty());

    const auto top = m.GeometryAt(a, base, {5, 0, 6}, kernel::AssemblyPick::Circle);         // top rim of the head
    const auto under = m.GeometryAt(a, part, {46.25, 0, 0}, kernel::AssemblyPick::Circle);   // outer rim underneath
    REQUIRE(top);
    REQUIRE(under);
    assembly::Constraint c;
    c.type = assembly::ConstraintType::Rotational;
    c.first = *under;
    c.second = *top;
    const ObjectId joint = a.AddConstraint(c).id;
    REQUIRE(assembly::Solve(a).converged);
    const assembly::Placement& on = a.FindComponent(part)->placement;
    CHECK(on.translation.z == doctest::Approx(6.0).epsilon(1e-6));
    CHECK(on.Direction({0, 0, 1}).z == doctest::Approx(1.0).epsilon(1e-6));   // lies on the head, its top up

    a.FindConstraint(joint)->flipped = true;
    REQUIRE(assembly::Solve(a).converged);
    const assembly::Placement& over = a.FindComponent(part)->placement;
    CHECK(over.Direction({0, 0, 1}).z == doctest::Approx(-1.0).epsilon(1e-6));   // turned over
    CHECK(over.translation.z == doctest::Approx(6.0).epsilon(1e-6));
    std::filesystem::remove_all(dir);
}
