#include "kernel/occt/Project.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <doctest.h>

#include <algorithm>

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
