#include "kernel/occt/Project.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <doctest.h>

#include <algorithm>
#include <array>

using namespace bz;
using kernel::ProjectedCurve;

namespace {

/// A tube around Z: radii 5..10, height 0..20.
TopoDS_Shape Tube() {
    BRepBuilderAPI_MakePolygon profile(gp_Pnt(5, 0, 0), gp_Pnt(10, 0, 0), gp_Pnt(10, 0, 20), gp_Pnt(5, 0, 20), true);
    const TopoDS_Face face = BRepBuilderAPI_MakeFace(profile.Wire(), true).Face();
    return BRepPrimAPI_MakeRevol(face, gp_Ax1(gp::Origin(), gp::DZ()), 2 * kPi, true).Shape();
}

/// The first face of `body` with a surface of the given type and radius.
TopoDS_Face FaceOf(const TopoDS_Shape& body, GeomAbs_SurfaceType type, double radius = -1) {
    for (TopExp_Explorer e(body, TopAbs_FACE); e.More(); e.Next()) {
        const BRepAdaptor_Surface s(TopoDS::Face(e.Current()));
        if (s.GetType() != type) continue;
        if (radius > 0 && type == GeomAbs_Cylinder && std::abs(s.Cylinder().Radius() - radius) > 1e-9) continue;
        return TopoDS::Face(e.Current());
    }
    FAIL("no such face");
    return {};
}

Frame SidePlane(double y) { return Frame{{0, y, 0}, {1, 0, 0}, {0, 1, 0}}; }   // parallel to Z

double TotalLength(const std::vector<ProjectedCurve>& curves) {
    double sum = 0;
    for (const ProjectedCurve& c : curves)
        if (c.kind == ProjectedCurve::Kind::Line) sum += (c.end - c.start).Length();
    return sum;
}

} // namespace

TEST_CASE("a body of revolution seen from the side: axis and both halves of the profile") {
    const TopoDS_Shape tube = Tube();
    const auto r = kernel::occt::ProjectRevolved(tube, FaceOf(tube, GeomAbs_Cylinder, 10), SidePlane(0));
    REQUIRE(r);
    REQUIRE(r->axis);
    CHECK(r->axis->kind == ProjectedCurve::Kind::Line);
    // The axis runs along sketch Y (= -Z) over the height of the body and a bit more.
    CHECK(std::abs(r->axis->start.x) < 1e-9);
    CHECK(std::abs(r->axis->end.y - r->axis->start.y) > 20);
    // Two 5 × 20 rectangles: 2 · (5 + 20 + 5 + 20) of outline.
    CHECK(TotalLength(r->outline) == doctest::Approx(100.0));
    CHECK(r->section.empty());   // the plane through the axis is the outline itself
    CHECK_FALSE(r->centre);
}

TEST_CASE("a side plane off the axis also gets the cut through the body") {
    const TopoDS_Shape tube = Tube();
    const auto r = kernel::occt::ProjectRevolved(tube, FaceOf(tube, GeomAbs_Cylinder, 5), SidePlane(7));
    REQUIRE(r);
    CHECK(TotalLength(r->outline) == doctest::Approx(100.0));
    // y = 7 misses the bore (r 5) and cuts the outer wall: two lines along the
    // height at x = ±√(100 − 49), joined by the cuts of the end rings.
    REQUIRE_FALSE(r->section.empty());
    const double x = std::sqrt(51.0);
    CHECK(TotalLength(r->section) == doctest::Approx(2 * 20 + 2 * 2 * x));
    CHECK(kernel::occt::ProjectRevolved(tube, FaceOf(tube, GeomAbs_Cylinder, 5), SidePlane(20))->section.empty());
}

TEST_CASE("a body of revolution seen along its axis: centre and outline circles") {
    const TopoDS_Shape tube = Tube();
    const Frame top{{0, 0, 30}, {1, 0, 0}, {0, 0, 1}};
    const auto r = kernel::occt::ProjectRevolved(tube, FaceOf(tube, GeomAbs_Cylinder, 10), top);
    REQUIRE(r);
    REQUIRE(r->centre);
    CHECK(r->centre->Length() < 1e-9);
    CHECK_FALSE(r->axis);
    REQUIRE(r->outline.size() == 2);
    CHECK(r->outline[0].radius == doctest::Approx(5.0));
    CHECK(r->outline[1].radius == doctest::Approx(10.0));
    CHECK(r->section.empty());   // z = 30 is above the tube
}

TEST_CASE("a torus seen along its axis: the inner and outer circles, not just its edges") {
    const TopoDS_Shape torus = BRepPrimAPI_MakeTorus(gp_Ax2(gp::Origin(), gp::DZ()), 10, 3).Shape();
    const Frame top{{0, 0, 0}, {1, 0, 0}, {0, 0, 1}};
    const auto r = kernel::occt::ProjectRevolved(torus, FaceOf(torus, GeomAbs_Torus), top);
    REQUIRE(r);
    std::vector<double> radii;
    for (const ProjectedCurve& c : r->outline) radii.push_back(c.radius);
    REQUIRE(radii.size() == 2);
    CHECK(radii[0] == doctest::Approx(7.0).epsilon(1e-3));
    CHECK(radii[1] == doctest::Approx(13.0).epsilon(1e-3));
    CHECK(r->section.empty());   // the cut at z = 0 repeats the outline circles
}

TEST_CASE("a flat face is not a body of revolution") {
    const TopoDS_Shape tube = Tube();
    CHECK_FALSE(kernel::occt::ProjectRevolved(tube, FaceOf(tube, GeomAbs_Plane), SidePlane(0)));
}

// --- circles seen from the side (Б3) ----------------------------------------

namespace {

/// The circle edge of `body` whose centre is at height `z`.
TopoDS_Edge CircleAt(const TopoDS_Shape& body, double z) {
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(body, TopAbs_EDGE, edges);
    for (int i = 1; i <= edges.Extent(); ++i) {
        const BRepAdaptor_Curve c(TopoDS::Edge(edges(i)));
        if (c.GetType() == GeomAbs_Circle && std::abs(c.Circle().Location().Z() - z) < 1e-9) return TopoDS::Edge(edges(i));
    }
    FAIL("no such circle");
    return {};
}

/// Bounds of the line curves: {xmin, ymin, xmax, ymax}.
std::array<double, 4> Extent(const std::vector<ProjectedCurve>& curves) {
    std::array<double, 4> b{1e300, 1e300, -1e300, -1e300};
    for (const ProjectedCurve& c : curves)
        for (const Vec2 p : {c.start, c.end}) {
            b[0] = std::min(b[0], p.x);
            b[1] = std::min(b[1], p.y);
            b[2] = std::max(b[2], p.x);
            b[3] = std::max(b[3], p.y);
        }
    return b;
}

} // namespace

TEST_CASE("a cylinder's rim seen from the side projects to its diameter") {
    const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(3, 0, 0), gp::DZ()), 10, 25).Shape();
    const Frame side = SidePlane(-40);   // sketch x = X, y = -Z
    const auto rim = kernel::occt::ProjectEdge(CircleAt(cylinder, 25), side);
    REQUIRE(rim);
    CHECK(rim->kind == ProjectedCurve::Kind::Line);
    CHECK((rim->end - rim->start).Length() == doctest::Approx(20.0));
    CHECK(std::min(rim->start.x, rim->end.x) == doctest::Approx(-7.0));
    CHECK(std::abs(rim->start.y) == doctest::Approx(25.0));
}

TEST_CASE("an arc seen edge-on covers only the part it spans") {
    // A half cylinder (x >= 0): its top rim is a half circle from y = -10 to 10 through x = 10.
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(gp_Pnt(0, -10, 0), 10, 20, 25).Shape();
    const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(gp_Ax2(gp::Origin(), gp::DZ()), 10, 25).Shape();
    const TopoDS_Shape halfCylinder = BRepAlgoAPI_Common(cylinder, box).Shape();
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(halfCylinder, TopAbs_EDGE, edges);
    bool found = false;
    double ylo = 1e300, yhi = -1e300;   // OCCT splits the rim at the seam: the arcs together
    for (int i = 1; i <= edges.Extent(); ++i) {
        const BRepAdaptor_Curve c(TopoDS::Edge(edges(i)));
        if (c.GetType() != GeomAbs_Circle) continue;
        found = true;
        // Seen along Y (plane XZ): the half circle covers x 0..10.
        const auto onXZ = kernel::occt::ProjectEdge(TopoDS::Edge(edges(i)), SidePlane(-40));
        REQUIRE(onXZ);
        CHECK(std::min(onXZ->start.x, onXZ->end.x) == doctest::Approx(0.0).epsilon(1e-9));
        CHECK(std::max(onXZ->start.x, onXZ->end.x) == doctest::Approx(10.0));
        // Seen along X (plane YZ): together y -10..10.
        const auto onYZ = kernel::occt::ProjectEdge(TopoDS::Edge(edges(i)), Frame{{-40, 0, 0}, {0, 1, 0}, {1, 0, 0}});
        REQUIRE(onYZ);
        ylo = std::min({ylo, onYZ->start.x, onYZ->end.x});
        yhi = std::max({yhi, onYZ->start.x, onYZ->end.x});
    }
    CHECK(found);
    CHECK(ylo == doctest::Approx(-10.0));
    CHECK(yhi == doctest::Approx(10.0));
}

TEST_CASE("a whole circle at a slant projects to an ellipse") {
    const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(gp_Ax2(gp::Origin(), gp::DZ()), 10, 25).Shape();
    const double a = kPi / 6;   // the sketch plane turned 30° from XY about X
    const Frame slanted{{0, 0, 0}, {1, 0, 0}, {0, -std::sin(a), std::cos(a)}};
    const auto e = kernel::occt::ProjectEdge(CircleAt(cylinder, 25), slanted);
    REQUIRE(e);
    CHECK(e->kind == ProjectedCurve::Kind::Ellipse);
    const double major = (e->start - e->centre).Length(), minor = (e->end - e->centre).Length();
    CHECK(std::max(major, minor) == doctest::Approx(10.0));
    CHECK(std::min(major, minor) == doctest::Approx(10.0 * std::cos(a)));
}

TEST_CASE("a cylinder boss on a plate seen from the side is a closed rectangle") {
    // The boss has no flat bottom of its own (it stands on the plate's face):
    // its bottom rim closes the outline.
    const TopoDS_Shape plate = BRepPrimAPI_MakeBox(gp_Pnt(-30, -30, -5), 60, 60, 5).Shape();
    const TopoDS_Shape boss = BRepPrimAPI_MakeCylinder(gp_Ax2(gp::Origin(), gp::DZ()), 10, 25).Shape();
    const TopoDS_Shape part = BRepAlgoAPI_Fuse(plate, boss).Shape();
    const auto r = kernel::occt::ProjectRevolved(part, FaceOf(part, GeomAbs_Cylinder, 10), SidePlane(-40));
    REQUIRE(r);
    CHECK_FALSE(r->outlineSkipped);
    // Two sides of 25 and the top and bottom of 20.
    CHECK(TotalLength(r->outline) == doctest::Approx(2 * 25 + 2 * 20));
    const auto b = Extent(r->outline);
    CHECK(b[0] == doctest::Approx(-10.0));
    CHECK(b[2] == doctest::Approx(10.0));
    CHECK(b[3] - b[1] == doctest::Approx(25.0));
}

TEST_CASE("a cylinder on its own seen from the side: no line twice") {
    const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(gp_Ax2(gp::Origin(), gp::DZ()), 10, 25).Shape();
    const auto r = kernel::occt::ProjectRevolved(cylinder, FaceOf(cylinder, GeomAbs_Cylinder, 10), SidePlane(-40));
    REQUIRE(r);
    CHECK(TotalLength(r->outline) == doctest::Approx(2 * 25 + 2 * 20));
}
