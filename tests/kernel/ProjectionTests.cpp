#include "../Fixtures.h"
#include "kernel/Model.h"
#include "kernel/occt/EdgeMatch.h"
#include "kernel/occt/ModelImpl.h"

#include <BRepAdaptor_Curve.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <doctest.h>

using namespace bz;
using namespace bz::test;
using doc::FeatureState;

namespace {

/// Reference to the first edge of the last body whose curve is of `type` and
/// whose middle lies at height `z`.
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

} // namespace

TEST_CASE("a projected circle follows the model and drives what is tied to it") {
    doc::Document d;
    auto& base = AddSketch(d);
    AddCircle(d, base, {0, 0}, 10);
    AddExtrude(d, base.id, "20");
    kernel::Model model;
    model.Rebuild(d);

    // A sketch on XY with the cylinder's top rim projected, and a free line
    // whose start is constrained onto the projected circle.
    auto& sk = AddSketch(d);
    const doc::EdgeReference rim = EdgeAt(model, GeomAbs_Circle, 20);
    const auto projected = model.ProjectEdge(rim, sk.placement.frame, static_cast<std::size_t>(d.IndexOf(sk.id)));
    REQUIRE(projected);
    CHECK(projected->kind == kernel::ProjectedCurve::Kind::Circle);
    CHECK(projected->radius == doctest::Approx(10.0));

    IdGenerator& ids = d.Ids();
    sketch::Sketch& s = sk.sketch;
    const ObjectId centre = s.AddPoint(ids, projected->centre);
    s.FindPoint(centre)->projected = true;
    const ObjectId circle = s.AddCircle(ids, centre, projected->radius);
    s.FindCircle(circle)->projected = true;
    sk.projections.push_back({rim, circle});
    const ObjectId a = s.AddPoint(ids, {10, 0}), b = s.AddPoint(ids, {30, 0});
    s.AddLine(ids, a, b);
    sketch::Constraint onCircle;
    onCircle.type = sketch::ConstraintType::PointOnCurve;
    onCircle.refs = {a, circle};
    s.AddConstraint(ids, onCircle);

    d.MarkChanged(2);
    model.Rebuild(d);
    REQUIRE(sk.status.state == FeatureState::Ok);

    // A bigger cylinder: the projection grows, the line's start goes with it.
    base.sketch.circles.front().radius = 15;
    d.MarkChanged(0);
    model.Rebuild(d);
    CHECK(sk.status.state == FeatureState::Ok);
    CHECK(s.RadiusOf(circle) == doctest::Approx(15.0));
    CHECK(s.PositionOf(a).Length() == doctest::Approx(15.0).epsilon(1e-6));
}

TEST_CASE("projecting a line and an edge seen end-on") {
    doc::Document d;
    auto& base = AddSketch(d);
    AddRect(d, base, {0, 0}, {40, 20});
    AddExtrude(d, base.id, "10");
    kernel::Model model;
    model.Rebuild(d);
    const Frame xy = *doc::origin::PlaneFrame(doc::origin::kPlaneXY);

    // A top edge along X projects to a 40 mm line; a vertical edge to a point.
    auto top = EdgeAt(model, GeomAbs_Line, 10);
    const auto line = model.ProjectEdge(top, xy, 2);
    REQUIRE(line);
    CHECK(line->kind == kernel::ProjectedCurve::Kind::Line);
    CHECK((line->end - line->start).Length() == doctest::Approx(top.direction.x != 0 ? 40.0 : 20.0));

    const auto vertical = EdgeAt(model, GeomAbs_Line, 5);
    CHECK_FALSE(model.ProjectEdge(vertical, xy, 2));
}
