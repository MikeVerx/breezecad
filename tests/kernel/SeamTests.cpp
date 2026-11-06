// Швы на телах: соседние грани в одной плоскости сливаются в одну —
// сторона контура из двух отрезков на одной прямой и лишний отрезок внутри
// контура не оставляют на теле линии. Скругление и фаска по такой грани
// проходят, ссылки на грани и рёбра после слияния находятся.

#include "../Fixtures.h"
#include "document/EdgeFeature.h"
#include "kernel/Model.h"
#include "kernel/occt/EdgeMatch.h"
#include "kernel/occt/ModelImpl.h"
#include "kernel/occt/FaceMatch.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <doctest.h>

#include <chrono>

using namespace bz;
using namespace bz::test;
using doc::CurveKind;
using doc::EdgeReference;
using doc::FeatureState;
using doc::SurfaceKind;

namespace {

std::size_t Count(const kernel::Model& model, TopAbs_ShapeEnum type) {
    std::size_t n = 0;
    for (const kernel::Body& b : model.GetImpl().bodies) {
        TopTools_IndexedMapOfShape map;
        TopExp::MapShapes(b.shape, type, map);
        n += static_cast<std::size_t>(map.Extent());
    }
    return n;
}

/// Прямоугольник 120×80, передняя сторона (y = −40) — из двух отрезков
/// с точкой посередине.
void AddSplitRect(doc::Document& d, doc::SketchFeature& sk) {
    auto& s = sk.sketch;
    IdGenerator& ids = d.Ids();
    const ObjectId p0 = s.AddPoint(ids, {-60, -40}), pm = s.AddPoint(ids, {0, -40});
    const ObjectId p1 = s.AddPoint(ids, {60, -40}), p2 = s.AddPoint(ids, {60, 40});
    const ObjectId p3 = s.AddPoint(ids, {-60, 40});
    s.AddLine(ids, p0, pm);
    s.AddLine(ids, pm, p1);
    s.AddLine(ids, p1, p2);
    s.AddLine(ids, p2, p3);
    s.AddLine(ids, p3, p0);
}

/// Верхнее переднее ребро бруска z = 0…30 (передняя грань к −Y).
EdgeReference TopFrontEdge() {
    EdgeReference e;
    e.curve = CurveKind::Line;
    e.direction = {1, 0, 0};
    e.middle = {0, -40, 30};
    e.faces = {{{SurfaceKind::Plane, {0, -1, 0}}, {SurfaceKind::Plane, {0, 0, 1}}}};
    return e;
}

template <class F>
F& AddEdgeOp(doc::Document& d, const std::string& size) {
    auto f = std::make_unique<F>();
    f->id = d.Ids().Next();
    f->name = d.UniqueFeatureName("Операция");
    f->edges = {TopFrontEdge()};
    std::vector<Parameter*> params;
    f->CollectParameters(params);
    params.front()->expression = size;   // радиус скругления, расстояние фаски
    auto* raw = f.get();
    d.Insert(std::move(f), d.FeatureCount());
    return *raw;
}

constexpr double kBox = 120.0 * 80.0 * 30.0;

} // namespace

TEST_CASE("шов: сторона из двух отрезков — у бруска шесть граней") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddSplitRect(d, sk);
    auto& ex = AddExtrude(d, sk.id, "30");
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(ex.status.state == FeatureState::Ok);
    CHECK(Count(model, TopAbs_FACE) == 6);
    CHECK(Count(model, TopAbs_EDGE) == 12);
    CHECK(Count(model, TopAbs_VERTEX) == 8);
}

TEST_CASE("шов: скругление и фаска ребра на слитой грани проходят") {
    SUBCASE("скругление") {
        doc::Document d;
        auto& sk = AddSketch(d);
        AddSplitRect(d, sk);
        AddExtrude(d, sk.id, "30");
        auto& fillet = AddEdgeOp<doc::FilletFeature>(d, "5");
        kernel::Model model;
        model.Rebuild(d);
        REQUIRE(fillet.status.state == FeatureState::Ok);
        CHECK(model.Volume() == doctest::Approx(kBox - (1 - kPi / 4) * 25 * 120).epsilon(1e-4));
    }
    SUBCASE("фаска") {
        doc::Document d;
        auto& sk = AddSketch(d);
        AddSplitRect(d, sk);
        AddExtrude(d, sk.id, "30");
        auto& chamfer = AddEdgeOp<doc::ChamferFeature>(d, "5");
        kernel::Model model;
        model.Rebuild(d);
        REQUIRE(chamfer.status.state == FeatureState::Ok);
        CHECK(model.Volume() == doctest::Approx(kBox - 12.5 * 120).epsilon(1e-4));
    }
}

TEST_CASE("шов: отрезок поперёк контура не оставляет линии на теле") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {-60, -40}, {60, 40});
    auto& s = sk.sketch;
    s.AddLine(d.Ids(), s.AddPoint(d.Ids(), {0, -40}), s.AddPoint(d.Ids(), {0, 40}));
    auto& ex = AddExtrude(d, sk.id, "30");
    ex.profiles.automatic = false;   // обе половины
    ex.profiles.regions = {{-30, 0}, {30, 0}};
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(ex.status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(kBox).epsilon(1e-6));
    CHECK(Count(model, TopAbs_FACE) == 6);
    CHECK(Count(model, TopAbs_EDGE) == 12);
}

TEST_CASE("шов: висячий отрезок внутри контура не оставляет линии на теле") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {-60, -40}, {60, 40});
    auto& s = sk.sketch;
    s.AddLine(d.Ids(), s.AddPoint(d.Ids(), {-20, 0}), s.AddPoint(d.Ids(), {20, 0}));
    auto& ex = AddExtrude(d, sk.id, "30");
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(ex.status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(kBox).epsilon(1e-6));
    CHECK(Count(model, TopAbs_FACE) == 6);
    CHECK(Count(model, TopAbs_EDGE) == 12);
}

TEST_CASE("шов: присоединённый брусок вровень с телом сливается с ним") {
    doc::Document d;
    auto& a = AddSketch(d);
    AddRect(d, a, {-60, -40}, {0, 40});
    AddExtrude(d, a.id, "30");
    auto& b = AddSketch(d);
    AddRect(d, b, {0, -40}, {60, 40});
    auto& join = AddExtrude(d, b.id, "30", doc::BooleanOp::Join);
    auto& fillet = AddEdgeOp<doc::FilletFeature>(d, "5");
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(join.status.state == FeatureState::Ok);
    CHECK(Count(model, TopAbs_FACE) == 6 + 1);   // + скругление
    REQUIRE(fillet.status.state == FeatureState::Ok);
}

TEST_CASE("шов: проекция слитой грани — одна линия на сторону") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddSplitRect(d, sk);
    AddExtrude(d, sk.id, "30");
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(model.GetImpl().bodies.size() == 1);
    const TopoDS_Shape& body = model.GetImpl().bodies.front().shape;
    // Передняя грань (−Y): все её рёбра на плоскость XZ.
    TopoDS_Face front;
    for (TopExp_Explorer f(body, TopAbs_FACE); f.More(); f.Next()) {
        const auto n = kernel::occt::PlanarNormal(TopoDS::Face(f.Current()));
        if (n && n->y < -0.99) front = TopoDS::Face(f.Current());
    }
    REQUIRE(!front.IsNull());
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(front, TopAbs_EDGE, edges);
    Frame plane;
    plane.origin = {0, 0, 0};
    plane.normal = {0, -1, 0};
    plane.xDir = {1, 0, 0};
    int lines = 0;
    for (int i = 1; i <= edges.Extent(); ++i) {
        const auto pc = model.ProjectEdge(kernel::occt::DescribeEdge(TopoDS::Edge(edges(i)), body), plane, d.FeatureCount());
        REQUIRE(pc);
        if (pc->kind == kernel::ProjectedCurve::Kind::Line && (pc->end - pc->start).Length() > 1e-6) ++lines;
    }
    CHECK(lines == 4);
}

TEST_CASE("шов: отрезок от стороны внутрь контура не оставляет линии на теле") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {-60, -40}, {60, 40});
    auto& s = sk.sketch;
    s.AddLine(d.Ids(), s.AddPoint(d.Ids(), {0, -40}), s.AddPoint(d.Ids(), {0, 10}));
    auto& ex = AddExtrude(d, sk.id, "30");
    kernel::Model model;
    model.Rebuild(d);
    REQUIRE(ex.status.state == FeatureState::Ok);
    CHECK(model.Volume() == doctest::Approx(kBox).epsilon(1e-6));
    CHECK(Count(model, TopAbs_FACE) == 6);
    CHECK(Count(model, TopAbs_EDGE) == 12);
}

namespace {

/// Брусок 40×20×10 со скруглёнными r 3 вертикальными рёбрами и под ним —
/// выдавливание вниз на 10 контура его верхней грани, спроецированного в
/// эскиз на XY: стопка, как «колпак» из выдавливаний спроецированных
/// контуров. Цилиндр скругления и цилиндр из спроецированной дуги — один
/// цилиндр, но OCCT строит их в системах разной «руки».
struct FilletStack {
    doc::Document d;
    kernel::Model model;
    doc::ExtrudeFeature* down = nullptr;

    explicit FilletStack(bool baseDown = false) {
        auto& base = AddSketch(d);
        AddRect(d, base, {0, 0}, {40, 20});
        AddExtrude(d, base.id, "10").reversed = baseDown;
        model.Rebuild(d);
        auto f = std::make_unique<doc::FilletFeature>();
        f->id = d.Ids().Next();
        f->name = "Скругление";
        f->radius.expression = "3";
        const TopoDS_Shape& body = model.GetImpl().bodies.back().shape;
        TopTools_IndexedMapOfShape edges;
        TopExp::MapShapes(body, TopAbs_EDGE, edges);
        for (int i = 1; i <= edges.Extent(); ++i) {
            const BRepAdaptor_Curve c(TopoDS::Edge(edges(i)));
            if (c.GetType() == GeomAbs_Line && std::abs(c.Line().Direction().Z()) > 0.99)
                f->edges.push_back(kernel::occt::DescribeEdge(TopoDS::Edge(edges(i)), body));
        }
        REQUIRE(f->edges.size() == 4);
        auto* fillet = f.get();
        d.Insert(std::move(f), d.FeatureCount());
        model.Rebuild(d);
        REQUIRE(fillet->status.state == FeatureState::Ok);

        // Верхняя грань — все её рёбра в эскиз на XY, связанные с моделью.
        auto& sk = AddSketch(d);
        const TopoDS_Shape& filleted = model.GetImpl().bodies.back().shape;
        TopoDS_Face top;
        for (TopExp_Explorer e(filleted, TopAbs_FACE); e.More(); e.Next()) {
            const auto n = kernel::occt::PlanarNormal(TopoDS::Face(e.Current()));
            if (n && n->z > 0.99) top = TopoDS::Face(e.Current());
        }
        REQUIRE(!top.IsNull());
        TopTools_IndexedMapOfShape outline;
        TopExp::MapShapes(top, TopAbs_EDGE, outline);
        IdGenerator& ids = d.Ids();
        sketch::Sketch& s = sk.sketch;
        const auto point = [&](Vec2 at) {
            for (const sketch::Point& p : s.points)
                if (p.projected && (p.pos - at).Length() < 1e-6) return p.id;
            const ObjectId id = s.AddPoint(ids, at);
            s.FindPoint(id)->projected = true;
            return id;
        };
        const std::size_t index = static_cast<std::size_t>(d.IndexOf(sk.id));
        for (int i = 1; i <= outline.Extent(); ++i) {
            const doc::EdgeReference ref = kernel::occt::DescribeEdge(TopoDS::Edge(outline(i)), filleted);
            const auto pc = model.ProjectEdge(ref, sk.placement.frame, index);
            REQUIRE(pc);
            ObjectId curve;
            if (pc->kind == kernel::ProjectedCurve::Kind::Line) {
                curve = s.AddLine(ids, point(pc->start), point(pc->end));
                s.FindLine(curve)->projected = true;
            } else {
                REQUIRE(pc->kind == kernel::ProjectedCurve::Kind::Arc);
                curve = s.AddArc(ids, point(pc->centre), point(pc->start), point(pc->end));
                for (auto& a : s.arcs)
                    if (a.id == curve) a.projected = true;
            }
            sk.projections.push_back({ref, curve, {}});
        }
        auto& ex = AddExtrude(d, sk.id, "10", doc::BooleanOp::Join);
        ex.reversed = !baseDown;   // по другую сторону эскиза от бруска
        down = &ex;
        d.MarkChanged(index);
        model.Rebuild(d);
    }
};

std::size_t CountCylinders(const kernel::Model& model) {
    std::size_t n = 0;
    for (TopExp_Explorer e(model.GetImpl().bodies.back().shape, TopAbs_FACE); e.More(); e.Next())
        if (BRepAdaptor_Surface(TopoDS::Face(e.Current())).GetType() == GeomAbs_Cylinder) ++n;
    return n;
}

} // namespace

TEST_CASE("шов: скругление и выдавленный рядом спроецированный контур — один цилиндр на угол, без зависания") {
    for (const bool baseDown : {false, true}) {
        CAPTURE(baseDown);
        const auto t0 = std::chrono::steady_clock::now();
        FilletStack s(baseDown);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        REQUIRE(s.down->status.state == FeatureState::Ok);
        CHECK(s.model.GetImpl().bodies.size() == 1);
        CHECK(CountCylinders(s.model) == 4);               // по одному цилиндру на угол, без линии посередине
        CHECK(Count(s.model, TopAbs_FACE) == 4 + 4 + 2);   // 4 стенки, 4 скругления, верх и низ
        CHECK(seconds < 20.0);
        CHECK(s.model.Volume() == doctest::Approx((40.0 * 20 - (4 - kPi) * 9) * 20).epsilon(1e-6));
    }
}
