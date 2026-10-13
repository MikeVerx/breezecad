#include "kernel/occt/Measure.h"

#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <doctest.h>

using namespace bz;
using namespace bz::kernel;

namespace {

/// The sub-shape of `type` (a face by default) nearest the point `target`.
TopoDS_Shape Nearest(const TopoDS_Shape& shape, gp_Pnt target, TopAbs_ShapeEnum type = TopAbs_FACE) {
    const TopoDS_Shape probe = BRepBuilderAPI_MakeVertex(target).Shape();
    TopoDS_Shape best;
    double bestDistance = 1e300;
    for (TopExp_Explorer e(shape, type); e.More(); e.Next()) {
        const double d = occt::Measure(e.Current(), probe).distance;
        if (d < bestDistance) {
            bestDistance = d;
            best = e.Current();
        }
    }
    return best;
}

} // namespace

TEST_CASE("measure faces and edges of a box") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(100, 60, 40).Shape();
    const TopoDS_Shape bottom = Nearest(box, {50, 30, -1});
    const TopoDS_Shape top = Nearest(box, {50, 30, 41});
    const TopoDS_Shape side = Nearest(box, {-1, 30, 20});

    const EntityInfo info = occt::Describe(top, {50, 30, 40});
    CHECK(info.kind == EntityKind::Face);
    CHECK(info.flat);
    CHECK(*info.area == doctest::Approx(6000.0));

    const Measurement parallel = occt::Measure(top, bottom);
    CHECK(parallel.distance == doctest::Approx(40.0));
    REQUIRE(parallel.angle);
    CHECK(*parallel.angle == doctest::Approx(0.0));

    const Measurement square = occt::Measure(top, side);
    CHECK(square.distance == doctest::Approx(0.0));
    REQUIRE(square.angle);
    CHECK(*square.angle == doctest::Approx(kPi / 2));

    const TopoDS_Shape edge = Nearest(box, {50, -1, -1}, TopAbs_EDGE);   // along X at y = z = 0
    const EntityInfo e = occt::Describe(edge, {50, 0, 0});
    CHECK(e.kind == EntityKind::Edge);
    CHECK(e.straight);
    CHECK(*e.length == doctest::Approx(100.0));

    // An edge and a face: the angle to the face's plane, not to its normal.
    const Measurement along = occt::Measure(edge, top);   // the edge lies parallel to the top
    REQUIRE(along.angle);
    CHECK(*along.angle == doctest::Approx(0.0));
    const Measurement across = occt::Measure(side, edge);   // the edge goes through the side (x = 0) square to it
    REQUIRE(across.angle);
    CHECK(*across.angle == doctest::Approx(kPi / 2));
}

TEST_CASE("measure a cylinder") {
    const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(5, 20).Shape();
    const TopoDS_Shape wall = Nearest(cylinder, {6, 0, 10});
    const EntityInfo info = occt::Describe(wall, {5, 0, 10});
    CHECK_FALSE(info.flat);
    REQUIRE(info.radius);
    CHECK(*info.radius == doctest::Approx(5.0));

    const TopoDS_Shape rim = Nearest(cylinder, {6, 0, 21}, TopAbs_EDGE);
    const EntityInfo r = occt::Describe(rim, {5, 0, 20});
    REQUIRE(r.radius);
    CHECK(*r.radius == doctest::Approx(5.0));
    CHECK(*r.length == doctest::Approx(2 * kPi * 5));
}
