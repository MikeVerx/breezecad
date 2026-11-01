// Broken projections: a projected edge lost after a change above the sketch,
// or a projection the sketch's constraints no longer agree with — drawn red,
// a warning on the sketch — and "Восстановить эскиз как до изменения", which
// brings the sketch back and unlinks its projections from the model.
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
using doctest::Approx;

namespace {

doc::EdgeReference RimAt(const kernel::Model& model, double z) {
    const TopoDS_Shape& body = model.GetImpl().bodies.back().shape;
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(body, TopAbs_EDGE, edges);
    for (int i = 1; i <= edges.Extent(); ++i) {
        const BRepAdaptor_Curve c(TopoDS::Edge(edges(i)));
        if (c.GetType() == GeomAbs_Circle && std::abs(c.Value((c.FirstParameter() + c.LastParameter()) / 2).Z() - z) < 1e-6)
            return kernel::occt::DescribeEdge(TopoDS::Edge(edges(i)), body);
    }
    FAIL("no rim");
    return {};
}

/// A cylinder r 10, h 20, and a second sketch with its top rim projected and
/// a line from the rim outwards (its start held on the projected circle).
struct Cylinder {
    doc::Document d;
    kernel::Model model;
    doc::SketchFeature* base = nullptr;
    doc::SketchFeature* sk = nullptr;
    ObjectId circle, centre, start, end;

    Cylinder() {
        base = &AddSketch(d);
        AddCircle(d, *base, {0, 0}, 10);
        AddExtrude(d, base->id, "20");
        model.Rebuild(d);
        sk = &AddSketch(d);
        const doc::EdgeReference rim = RimAt(model, 20);
        const auto pc = model.ProjectEdge(rim, sk->placement.frame, static_cast<std::size_t>(d.IndexOf(sk->id)));
        REQUIRE(pc);
        IdGenerator& ids = d.Ids();
        sketch::Sketch& s = sk->sketch;
        centre = s.AddPoint(ids, pc->centre);
        s.FindPoint(centre)->projected = true;
        circle = s.AddCircle(ids, centre, pc->radius);
        s.FindCircle(circle)->projected = true;
        sk->projections.push_back({rim, circle});
        start = s.AddPoint(ids, {10, 0});
        end = s.AddPoint(ids, {30, 0});
        s.AddLine(ids, start, end);
        sketch::Constraint on;
        on.type = sketch::ConstraintType::PointOnCurve;
        on.refs = {start, circle, {}};
        s.AddConstraint(ids, on);
        d.MarkChanged(2);
        model.Rebuild(d);
        REQUIRE(sk->status.state == FeatureState::Ok);
        REQUIRE_FALSE(sk->HasBrokenProjections());
    }

    void Radius(double r) {
        base->sketch.circles.front().radius = r;
        d.MarkChanged(0);
        model.Rebuild(d);
    }
    /// The cylinder becomes a box: its rim is gone.
    void Square() {
        base->sketch.circles.clear();
        AddRect(d, *base, {-10, -10}, {10, 10});
        d.MarkChanged(0);
        model.Rebuild(d);
    }
};

} // namespace

TEST_CASE("сломанная проекция: ребро пропало выше по дереву — кривая сломана, эскиз помнит, каким был до этого") {
    Cylinder c;
    c.Radius(15);   // followed: all well
    CHECK_FALSE(c.sk->HasBrokenProjections());
    CHECK_FALSE(c.sk->beforeBreak);
    const Vec2 startAt = c.sk->sketch.PositionOf(c.start);
    CHECK(startAt.Length() == Approx(15.0).epsilon(1e-6));

    c.Square();
    CHECK(c.sk->lostProjections.count(c.circle) == 1);
    CHECK(c.sk->BrokenProjections().count(c.circle) == 1);
    CHECK(c.sk->status.state == FeatureState::Warning);
    REQUIRE(c.sk->beforeBreak);
    CHECK(c.sk->beforeBreak->RadiusOf(c.circle) == Approx(15.0));

    SUBCASE("восстановить как до изменения: проекции отвязаны, геометрия на месте и держится") {
        REQUIRE(c.sk->RestoreBeforeBreak(c.d.Ids()));
        CHECK(c.sk->projections.empty());
        CHECK_FALSE(c.sk->HasBrokenProjections());
        CHECK_FALSE(c.sk->beforeBreak);
        CHECK_FALSE(c.sk->sketch.FindCircle(c.circle)->projected);
        CHECK_FALSE(c.sk->sketch.FindPoint(c.centre)->projected);
        bool fixed = false;
        for (const sketch::Constraint& k : c.sk->sketch.constraints)
            fixed = fixed || (k.type == sketch::ConstraintType::Fix && k.refs[0] == c.centre);
        CHECK(fixed);
        c.d.MarkChanged(2);
        c.model.Rebuild(c.d);
        CHECK(c.sk->status.state == FeatureState::Ok);
        CHECK(c.sk->sketch.RadiusOf(c.circle) == Approx(15.0));
        CHECK((c.sk->sketch.PositionOf(c.start) - startAt).Length() < 1e-6);
        CHECK(c.sk->sketch.PositionOf(c.centre).Length() < 1e-9);
    }
    SUBCASE("ребро вернулось — проекция снова исправна, снимок забыт") {
        c.base->sketch = sketch::Sketch{};
        c.base->sketch.EnsureOrigin(c.d.Ids());
        AddCircle(c.d, *c.base, {0, 0}, 12);
        c.d.MarkChanged(0);
        c.model.Rebuild(c.d);
        // Found again: whole again, the snapshot forgotten.
        CHECK(c.sk->lostProjections.empty());
        CHECK_FALSE(c.sk->HasBrokenProjections());
        CHECK_FALSE(c.sk->beforeBreak);
        CHECK(c.sk->status.state == FeatureState::Ok);
        CHECK(c.sk->sketch.RadiusOf(c.circle) == Approx(12.0));
    }
}

TEST_CASE("сломанная проекция: размер на проекции не сходится после изменения выше по дереву") {
    Cylinder c;
    // The projected circle dimensioned as it is now: R 10.
    sketch::Constraint r;
    r.type = sketch::ConstraintType::Radius;
    r.refs = {c.circle, {}, {}};
    r.parameter = {"d90", "10", Quantity::Length, 10.0, {}};
    c.sk->sketch.AddConstraint(c.d.Ids(), r);
    c.d.MarkChanged(2);
    c.model.Rebuild(c.d);
    REQUIRE_FALSE(c.sk->HasBrokenProjections());

    // The cylinder grows: the projection follows, the dimension cannot.
    c.Radius(15);
    CHECK_FALSE(c.sk->analysis.failing.empty());
    CHECK(c.sk->lostProjections.empty());
    CHECK(c.sk->BrokenProjections().count(c.circle) == 1);
    CHECK(c.sk->status.state != FeatureState::Ok);
    REQUIRE(c.sk->beforeBreak);
    CHECK(c.sk->beforeBreak->RadiusOf(c.circle) == Approx(10.0));

    // Back as it was before (R 10), unlinked: the dimension holds again.
    REQUIRE(c.sk->RestoreBeforeBreak(c.d.Ids()));
    c.d.MarkChanged(2);
    c.model.Rebuild(c.d);
    CHECK(c.sk->status.state == FeatureState::Ok);
    CHECK(c.sk->sketch.RadiusOf(c.circle) == Approx(10.0));
    CHECK_FALSE(c.sk->HasBrokenProjections());
}

TEST_CASE("сломанная проекция: эскиз без проекций восстанавливать нечего") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {0, 0}, {10, 10});
    CHECK_FALSE(sk.RestoreBeforeBreak(d.Ids()));
    CHECK(sk.sketch.lines.size() == 4);
}
