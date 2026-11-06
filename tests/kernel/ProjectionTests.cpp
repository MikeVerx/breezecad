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
    sk.projections.push_back({rim, circle, {}});
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

TEST_CASE("проекция: отрезок, пришедший задом наперёд, не тащит общую точку с дугой") {
    // An arc (centre 0,0; from (10,0) to (0,10)) and a line from the arc's
    // end (0,10) to (-20,10), all projected and sharing their ends. After a
    // rebuild the same edge of the line may come the other way round.
    sketch::Sketch s;
    IdGenerator ids;
    const ObjectId c = s.AddPoint(ids, {0, 0}), a0 = s.AddPoint(ids, {10, 0}), a1 = s.AddPoint(ids, {0, 10});
    const ObjectId l1 = s.AddPoint(ids, {-20, 10});
    const ObjectId arc = s.AddArc(ids, c, a0, a1);
    const ObjectId line = s.AddLine(ids, a1, l1);

    kernel::ProjectedCurve pa;
    pa.kind = kernel::ProjectedCurve::Kind::Arc;
    pa.centre = {0, 0};
    pa.start = {12, 0};   // the model grew: radius 12
    pa.end = {0, 12};
    pa.radius = 12;
    kernel::ProjectedCurve pl;
    pl.kind = kernel::ProjectedCurve::Kind::Line;
    pl.start = {-20, 12};   // reversed: from the far end to the arc
    pl.end = {0, 12};

    const std::vector<bool> placed = kernel::PlaceProjectedCurves(s, {{arc, pa}, {line, pl}});
    REQUIRE(placed == std::vector<bool>{true, true});
    CHECK((s.PositionOf(a1) - Vec2{0, 12}).Length() < 1e-12);    // the shared point is where both want it
    CHECK((s.PositionOf(l1) - Vec2{-20, 12}).Length() < 1e-12);
    CHECK((s.PositionOf(a0) - Vec2{12, 0}).Length() < 1e-12);
    CHECK(kernel::CurveMatches(s, line, pl, 1e-9));   // a line matches either way round

    // A chain of lines only (a rectangle), one of them reversed: they agree with each other.
    sketch::Sketch r;
    const ObjectId p0 = r.AddPoint(ids, {0, 0}), p1 = r.AddPoint(ids, {10, 0}), p2 = r.AddPoint(ids, {10, 5}), p3 = r.AddPoint(ids, {0, 5});
    const ObjectId e0 = r.AddLine(ids, p0, p1), e1 = r.AddLine(ids, p1, p2), e2 = r.AddLine(ids, p2, p3), e3 = r.AddLine(ids, p3, p0);
    const auto L = [](Vec2 from, Vec2 to) {
        kernel::ProjectedCurve pc;
        pc.start = from;
        pc.end = to;
        return pc;
    };
    // The rectangle moved by (1, 1); the second side came reversed.
    kernel::PlaceProjectedCurves(r, {{e0, L({1, 1}, {11, 1})}, {e1, L({11, 6}, {11, 1})}, {e2, L({11, 6}, {1, 6})}, {e3, L({1, 6}, {1, 1})}});
    CHECK((r.PositionOf(p0) - Vec2{1, 1}).Length() < 1e-12);
    CHECK((r.PositionOf(p1) - Vec2{11, 1}).Length() < 1e-12);
    CHECK((r.PositionOf(p2) - Vec2{11, 6}).Length() < 1e-12);
    CHECK((r.PositionOf(p3) - Vec2{1, 6}).Length() < 1e-12);
}
