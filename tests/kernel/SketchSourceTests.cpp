// Projections that follow the profile their edge was made from (doc::SketchSource):
// a fillet or chamfer on the projected edge, added above the sketch, does not
// break the projection — the line may get shorter, it stays the same line.
#include "../Fixtures.h"
#include "document/EdgeFeature.h"
#include "kernel/Model.h"
#include "kernel/occt/EdgeMatch.h"
#include "kernel/occt/ModelImpl.h"
#include "kernel/occt/SketchSource.h"

#include <BRepAdaptor_Curve.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <doctest.h>
#include <nlohmann/json.hpp>

using namespace bz;
using namespace bz::test;
using doc::FeatureState;
using doctest::Approx;

namespace {

/// The straight edge of the last body with its middle at `middle`.
std::optional<TopoDS_Edge> LineEdgeAt(const kernel::Model& model, Vec3 middle) {
    const TopoDS_Shape& body = model.GetImpl().bodies.back().shape;
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(body, TopAbs_EDGE, edges);
    for (int i = 1; i <= edges.Extent(); ++i) {
        const BRepAdaptor_Curve c(TopoDS::Edge(edges(i)));
        if (c.GetType() != GeomAbs_Line) continue;
        const gp_Pnt m = c.Value((c.FirstParameter() + c.LastParameter()) / 2);
        if ((Vec3{m.X(), m.Y(), m.Z()} - middle).Length() < 1e-6) return TopoDS::Edge(edges(i));
    }
    return std::nullopt;
}

/// A box 40×20×10 from a rectangle on XY, and a second sketch on XY with the
/// box's top back edge (y 20, z 10) projected into it.
struct Box {
    doc::Document d;
    kernel::Model model;
    doc::SketchFeature* base = nullptr;
    doc::SketchFeature* sk = nullptr;
    ObjectId line;

    Box() {
        base = &AddSketch(d);
        AddRect(d, *base, {0, 0}, {40, 20});
        AddExtrude(d, base->id, "10");
        model.Rebuild(d);
        sk = &AddSketch(d);
        const std::optional<TopoDS_Edge> top = LineEdgeAt(model, {20, 20, 10});
        REQUIRE(top);
        const doc::EdgeReference ref = kernel::occt::DescribeEdge(*top, model.GetImpl().bodies.back().shape);
        const auto pc = model.ProjectEdge(ref, sk->placement.frame, static_cast<std::size_t>(d.IndexOf(sk->id)));
        REQUIRE(pc);
        IdGenerator& ids = d.Ids();
        sketch::Sketch& s = sk->sketch;
        const ObjectId a = s.AddPoint(ids, pc->start), b = s.AddPoint(ids, pc->end);
        s.FindPoint(a)->projected = s.FindPoint(b)->projected = true;
        line = s.AddLine(ids, a, b);
        s.FindLine(line)->projected = true;
        sk->projections.push_back({ref, line, {}});
        Rebuild(2);
        REQUIRE(sk->status.state == FeatureState::Ok);
    }

    void Rebuild(std::size_t from) {
        d.MarkChanged(from);
        model.Rebuild(d);
    }
    /// A fillet or chamfer of `size` on the edges with middles at `where`,
    /// put above the projecting sketch.
    template <class F>
    F& EdgeOp(std::vector<Vec3> where, const std::string& size) {
        auto f = std::make_unique<F>();
        f->id = d.Ids().Next();
        f->name = d.UniqueFeatureName("Операция");
        const TopoDS_Shape& body = model.GetImpl().bodiesAfter[1].back().shape;
        for (Vec3 m : where) {
            TopTools_IndexedMapOfShape edges;
            TopExp::MapShapes(body, TopAbs_EDGE, edges);
            bool found = false;
            for (int i = 1; i <= edges.Extent() && !found; ++i) {
                const BRepAdaptor_Curve c(TopoDS::Edge(edges(i)));
                const gp_Pnt p = c.Value((c.FirstParameter() + c.LastParameter()) / 2);
                if ((Vec3{p.X(), p.Y(), p.Z()} - m).Length() < 1e-6) {
                    f->edges.push_back(kernel::occt::DescribeEdge(TopoDS::Edge(edges(i)), body));
                    found = true;
                }
            }
            REQUIRE(found);
        }
        std::vector<Parameter*> params;
        f->CollectParameters(params);
        params.front()->expression = size;
        auto* raw = f.get();
        d.Insert(std::move(f), 2);   // between the extrusion and the projecting sketch
        Rebuild(2);
        REQUIRE(raw->status.state == FeatureState::Ok);
        return *raw;
    }

    Vec2 Start() const { return sk->sketch.PositionOf(sk->sketch.FindLine(line)->start); }
    Vec2 End() const { return sk->sketch.PositionOf(sk->sketch.FindLine(line)->end); }
    /// The projected line lies on y = `y` from x `x0` to `x1` (either way round).
    void CheckLine(double y, double x0, double x1) const {
        const Vec2 a = Start(), b = End();
        CHECK(a.y == Approx(y));
        CHECK(b.y == Approx(y));
        CHECK(std::min(a.x, b.x) == Approx(x0));
        CHECK(std::max(a.x, b.x) == Approx(x1));
    }
};

} // namespace

TEST_CASE("проекция узнаёт кривую эскиза, из которой выросло ребро") {
    Box b;
    const doc::SketchSource& source = b.sk->projections.front().source;
    REQUIRE(source.IsSet());
    CHECK(source.sketch == b.base->id);
    const sketch::Line* l = b.base->sketch.FindLine(source.curve);
    REQUIRE(l);
    CHECK(b.base->sketch.PositionOf(l->start).y == Approx(20.0));
    CHECK(b.base->sketch.PositionOf(l->end).y == Approx(20.0));
    CHECK(source.offset == Approx(10.0));
    CHECK(source.from == Approx(0.0));
    CHECK(source.to == Approx(1.0));
    b.CheckLine(20, 0, 40);
}

TEST_CASE("скругление самого спроецированного ребра выше по дереву: линия остаётся линией") {
    Box b;
    b.EdgeOp<doc::FilletFeature>({{20, 20, 10}}, "3");
    CHECK(b.sk->lostProjections.empty());
    CHECK_FALSE(b.sk->HasBrokenProjections());
    CHECK(b.sk->status.state == FeatureState::Ok);
    b.CheckLine(20, 0, 40);   // the rim of the side face under the fillet: the same line
    CHECK(b.sk->projections.front().source.offset == Approx(7.0));
}

TEST_CASE("фаска на спроецированном ребре: проекция не теряется") {
    Box b;
    b.EdgeOp<doc::ChamferFeature>({{20, 20, 10}}, "2");
    CHECK(b.sk->lostProjections.empty());
    CHECK(b.sk->status.state == FeatureState::Ok);
    b.CheckLine(20, 0, 40);
}

TEST_CASE("скругления вертикальных рёбер: ребро стало короче, но это та же линия") {
    Box b;
    b.EdgeOp<doc::FilletFeature>({{0, 20, 5}, {40, 20, 5}}, "3");
    CHECK(b.sk->lostProjections.empty());
    CHECK(b.sk->status.state == FeatureState::Ok);
    b.CheckLine(20, 3, 37);
    const doc::SketchSource& s = b.sk->projections.front().source;
    CHECK(s.from == Approx(3.0 / 40));
    CHECK(s.to == Approx(37.0 / 40));
}

TEST_CASE("исходный эскиз изменился: проекция идёт за своей кривой") {
    Box b;
    b.EdgeOp<doc::FilletFeature>({{20, 20, 10}}, "3");
    // The box grows deeper: the back line moves from y 20 to y 30.
    for (sketch::Point& p : b.base->sketch.points)
        if (std::abs(p.pos.y - 20) < 1e-9) p.pos.y = 30;
    b.Rebuild(0);
    CHECK(b.sk->lostProjections.empty());
    CHECK(b.sk->status.state == FeatureState::Ok);
    b.CheckLine(30, 0, 40);
}

TEST_CASE("кривая-источник удалена: остаётся следовать за ребром, без него — проекция сломана") {
    Box b;
    const ObjectId curve = b.sk->projections.front().source.curve;
    // The rectangle becomes a circle: neither the line nor the edge is there.
    b.base->sketch = sketch::Sketch{};
    b.base->sketch.EnsureOrigin(b.d.Ids());
    AddCircle(b.d, *b.base, {20, 10}, 5);
    b.Rebuild(0);
    CHECK_FALSE(b.base->sketch.FindLine(curve));
    CHECK(b.sk->lostProjections.count(b.line) == 1);
    CHECK_FALSE(b.sk->projections.front().source.IsSet());
}

TEST_CASE("кривая-источник как ребро модели: отрезок, дуга и окружность") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {0, 0}, {40, 20});
    AddCircle(d, sk, {20, 10}, 5);
    const ObjectId line = sk.sketch.lines.front().id;   // (0,0)–(40,0)
    const ObjectId circle = sk.sketch.circles.front().id;

    auto edge = kernel::occt::SourceCurveEdge(d, {sk.id, line, 7.0, 0.25, 0.75});
    REQUIRE(edge);
    BRepAdaptor_Curve c(*edge);
    gp_Pnt p0 = c.Value(c.FirstParameter()), p1 = c.Value(c.LastParameter());
    CHECK(std::min(p0.X(), p1.X()) == Approx(10.0));
    CHECK(std::max(p0.X(), p1.X()) == Approx(30.0));
    CHECK(p0.Z() == Approx(7.0));

    edge = kernel::occt::SourceCurveEdge(d, {sk.id, circle, 2.0, 0.0, 1.0});
    REQUIRE(edge);
    BRepAdaptor_Curve o(*edge);
    CHECK(o.GetType() == GeomAbs_Circle);
    CHECK(o.Circle().Radius() == Approx(5.0));
    CHECK(o.Circle().Location().Z() == Approx(2.0));

    // An edge lies on the curve it came from; the range is read back.
    doc::SketchSource s{sk.id, line};
    edge = kernel::occt::SourceCurveEdge(d, {sk.id, line, 3.0, 0.1, 0.6});
    REQUIRE(edge);
    REQUIRE(kernel::occt::LiesOnSource(*edge, d, s));
    CHECK(s.offset == Approx(3.0));
    CHECK(s.from == Approx(0.1));
    CHECK(s.to == Approx(0.6));
    s = {sk.id, circle};
    CHECK_FALSE(kernel::occt::LiesOnSource(*edge, d, s));

    CHECK_FALSE(kernel::occt::SourceCurveEdge(d, {sk.id, ObjectId{}, 0.0, 0.0, 1.0}));
}

TEST_CASE("ссылка на кривую-источник сохраняется в файле") {
    Box b;
    b.EdgeOp<doc::FilletFeature>({{0, 20, 5}, {40, 20, 5}}, "3");
    const doc::SketchSource before = b.sk->projections.front().source;
    nlohmann::json j;
    b.sk->SaveFields(j);
    doc::SketchFeature loaded;
    REQUIRE(loaded.LoadFields(j).IsOk());
    REQUIRE(loaded.projections.size() == 1);
    const doc::SketchSource& after = loaded.projections.front().source;
    CHECK(after.sketch == before.sketch);
    CHECK(after.curve == before.curve);
    CHECK(after.offset == Approx(before.offset));
    CHECK(after.from == Approx(before.from));
    CHECK(after.to == Approx(before.to));

    // Older files have no source: it is unset, not an error.
    j["projections"][0].erase("source");
    doc::SketchFeature old;
    REQUIRE(old.LoadFields(j).IsOk());
    CHECK_FALSE(old.projections.front().source.IsSet());
}
