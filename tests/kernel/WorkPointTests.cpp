#include "../Fixtures.h"
#include "document/WorkAxisFeature.h"
#include "document/WorkPlaneFeature.h"
#include "document/WorkPointFeature.h"
#include "kernel/Model.h"
#include "kernel/occt/EdgeMatch.h"
#include "kernel/occt/ModelImpl.h"

#include <BRepAdaptor_Curve.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <doctest.h>

#include <cmath>

using namespace bz;
using namespace bz::test;
using doc::FeatureState;
using doc::WorkPointMethod;

namespace {

template <class F>
F& Add(doc::Document& d, const char* name) {
    auto f = std::make_unique<F>();
    f->id = d.Ids().Next();
    f->name = d.UniqueFeatureName(name);
    auto* raw = f.get();
    d.Insert(std::move(f), d.FeatureCount());
    return *raw;
}

doc::WorkPointFeature& AddPoint(doc::Document& d, WorkPointMethod method) {
    auto& p = Add<doc::WorkPointFeature>(d, "Точка");
    p.method = method;
    return p;
}

/// Reference to the first edge of the last body of `type` whose middle is at height `z`.
doc::EdgeReference EdgeAt(const kernel::Model& model, GeomAbs_CurveType type, double z) {
    const TopoDS_Shape& body = model.GetImpl().bodies.back().shape;
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(body, TopAbs_EDGE, edges);
    for (int i = 1; i <= edges.Extent(); ++i) {
        const BRepAdaptor_Curve c(TopoDS::Edge(edges(i)));
        if (c.GetType() != type) continue;
        if (std::abs(c.Value((c.FirstParameter() + c.LastParameter()) / 2).Z() - z) < 1e-6)
            return kernel::occt::DescribeEdge(TopoDS::Edge(edges(i)), body);
    }
    FAIL("no such edge");
    return {};
}

/// A box 120 x 80 x 30 standing on XY, centred on the origin.
doc::Document BoxDocument() {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {-60, -40}, {60, 40});
    AddExtrude(d, sk.id, "30");
    return d;
}

void CheckAt(const doc::WorkPointFeature& p, Vec3 expected) {
    REQUIRE(p.status.state == FeatureState::Ok);
    REQUIRE(p.position);
    CHECK(p.position->x == doctest::Approx(expected.x));
    CHECK(p.position->y == doctest::Approx(expected.y));
    CHECK(p.position->z == doctest::Approx(expected.z));
}

} // namespace

TEST_CASE("work point at the centre of a round edge follows the part") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddCircle(d, sk, {5, 3}, 10);
    auto& ex = AddExtrude(d, sk.id, "20");
    kernel::Model model;
    model.Rebuild(d);
    auto& p = AddPoint(d, WorkPointMethod::Center);
    p.edge = EdgeAt(model, GeomAbs_Circle, 20);
    model.Rebuild(d);
    CheckAt(p, {5, 3, 20});

    ex.distance.expression = "35";
    d.MarkChanged(0);
    model.Rebuild(d);
    CheckAt(p, {5, 3, 35});
}

TEST_CASE("work point in the middle of an edge and at a vertex") {
    doc::Document d = BoxDocument();   // 120 x 80 x 30
    kernel::Model model;
    model.Rebuild(d);
    doc::EdgeReference front;
    front.curve = doc::CurveKind::Line;
    front.direction = {1, 0, 0};
    front.middle = {0, -40, 30};
    front.faces = {{{doc::SurfaceKind::Plane, {0, -1, 0}}, {doc::SurfaceKind::Plane, {0, 0, 1}}}};
    auto& mid = AddPoint(d, WorkPointMethod::Midpoint);
    mid.edge = front;
    auto& corner = AddPoint(d, WorkPointMethod::Vertex);
    corner.edge = front;
    corner.vertex = {60, -40, 30};
    model.Rebuild(d);
    CheckAt(mid, {0, -40, 30});
    CheckAt(corner, {60, -40, 30});

    for (const auto& f : d.Features())
        if (auto* ex = dynamic_cast<doc::ExtrudeFeature*>(f.get())) ex->distance.expression = "45";
    d.MarkChanged(0);
    model.Rebuild(d);
    CheckAt(corner, {60, -40, 45});
}

TEST_CASE("work point where an edge crosses a plane") {
    doc::Document d = BoxDocument();
    kernel::Model model;
    model.Rebuild(d);
    auto& plane = Add<doc::WorkPlaneFeature>(d, "Плоскость");
    plane.base = doc::origin::kPlaneXY;
    plane.offset.expression = "10";
    auto& p = AddPoint(d, WorkPointMethod::Intersection);
    p.edge = EdgeAt(model, GeomAbs_Line, 15);   // a vertical edge
    p.plane = plane.id;
    model.Rebuild(d);
    REQUIRE(p.position);
    CHECK(std::abs(p.position->x) == doctest::Approx(60));
    CHECK(std::abs(p.position->y) == doctest::Approx(40));
    CHECK(p.position->z == doctest::Approx(10));

    // Parallel to the plane: an error, not a point far away.
    p.plane = doc::origin::kPlaneXY;
    p.edge = EdgeAt(model, GeomAbs_Line, 30);   // a top edge, flat
    d.MarkChanged(0);
    model.Rebuild(d);
    CHECK(p.status.state == FeatureState::Error);
}

TEST_CASE("work points by coordinates make planes and axes") {
    doc::Document d;
    auto& a = AddPoint(d, WorkPointMethod::Coordinates);
    auto& b = AddPoint(d, WorkPointMethod::Coordinates);
    auto& c = AddPoint(d, WorkPointMethod::Coordinates);
    b.x.expression = "10";
    c.y.expression = "10";
    for (auto* p : {&a, &b, &c}) p->z.expression = "3*2";
    auto& plane = Add<doc::WorkPlaneFeature>(d, "Плоскость");
    plane.method = doc::WorkPlaneMethod::ThreePoints;
    plane.points = {{}, {}, {}};
    plane.pointFeatures = {a.id, b.id, c.id};
    auto& axis = Add<doc::WorkAxisFeature>(d, "Ось");
    axis.method = doc::WorkAxisMethod::Points;
    axis.points = {{}, {}};
    axis.pointFeatures = {a.id, c.id};
    kernel::Model model;
    model.Rebuild(d);
    CheckAt(b, {10, 0, 6});
    REQUIRE(plane.status.state == FeatureState::Ok);
    CHECK(plane.frame->origin.z == doctest::Approx(6));
    CHECK(std::abs(plane.frame->normal.z) == doctest::Approx(1));
    REQUIRE(axis.status.state == FeatureState::Ok);
    CHECK(std::abs(axis.line->second.y) == doctest::Approx(1));
    const auto deps = plane.Dependencies();
    CHECK(std::ranges::find(deps, a.id) != deps.end());
}

TEST_CASE("work point errors") {
    doc::Document d = BoxDocument();
    kernel::Model model;
    model.Rebuild(d);
    auto& p = AddPoint(d, WorkPointMethod::Center);
    p.edge = EdgeAt(model, GeomAbs_Line, 15);   // straight: no centre
    auto& none = AddPoint(d, WorkPointMethod::Vertex);
    model.Rebuild(d);
    CHECK(p.status.state == FeatureState::Error);
    CHECK(none.status.state == FeatureState::Error);
    CHECK_FALSE(none.position);
}
