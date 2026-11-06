// «Проецировать геометрию» до уровня Inventor (06.11.2026): вершины, рабочая
// геометрия и начало координат, кривые других эскизов, сечение, развёртка,
// подрезка перекрывающихся петель контура. Каждый вид следует за моделью при
// перестроении и сохраняется в файл.
#include "../Fixtures.h"
#include "document/SheetMetalFeature.h"
#include "document/WorkPlaneFeature.h"
#include "kernel/Model.h"
#include "kernel/ProjectionSources.h"
#include "kernel/occt/EdgeMatch.h"
#include "kernel/occt/ModelImpl.h"

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
using Of = doc::Projection::Of;

namespace {

/// The edge of the last body with its middle at `middle`, described.
doc::EdgeReference EdgeAt(const kernel::Model& model, Vec3 middle) {
    const TopoDS_Shape& body = model.GetImpl().bodies.back().shape;
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(body, TopAbs_EDGE, edges);
    for (int i = 1; i <= edges.Extent(); ++i) {
        const BRepAdaptor_Curve c(TopoDS::Edge(edges(i)));
        const gp_Pnt m = c.Value((c.FirstParameter() + c.LastParameter()) / 2);
        if ((Vec3{m.X(), m.Y(), m.Z()} - middle).Length() < 1e-6) return kernel::occt::DescribeEdge(TopoDS::Edge(edges(i)), body);
    }
    FAIL("no edge there");
    return {};
}

void Rebuild(doc::Document& d, kernel::Model& model) {
    d.MarkChanged(0);
    model.Rebuild(d);
}

/// A sketch on a frame of its own (a plane through the part).
doc::SketchFeature& SketchOn(doc::Document& d, const Frame& frame) {
    doc::SketchFeature& sk = AddSketch(d, doc::origin::kPlaneXY);
    sk.placement.frame = frame;
    return sk;
}

ObjectId ProjectedPoint(doc::Document& d, doc::SketchFeature& sk, Vec2 at) {
    const ObjectId p = sk.sketch.AddPoint(d.Ids(), at);
    sk.sketch.FindPoint(p)->projected = true;
    return p;
}

ObjectId ProjectedLine(doc::Document& d, doc::SketchFeature& sk, Vec2 a, Vec2 b) {
    const ObjectId l = sk.sketch.AddLine(d.Ids(), ProjectedPoint(d, sk, a), ProjectedPoint(d, sk, b));
    sk.sketch.FindLine(l)->projected = true;
    return l;
}

/// A 40×20×10 box on XY, its base sketch's corner at (40, 20) movable.
struct Box {
    doc::Document d;
    kernel::Model model;
    doc::SketchFeature* base = nullptr;

    Box() {
        base = &AddSketch(d);
        AddRect(d, *base, {0, 0}, {40, 20});
        AddExtrude(d, base->id, "10");
        model.Rebuild(d);
    }
    /// The box made `width` wide (the rectangle's right side moved).
    void Widen(double width) {
        for (sketch::Point& p : base->sketch.points)
            if (std::abs(p.pos.x - 40.0) < 1e-9) p.pos.x = width;
        Rebuild(d, model);
    }
};

} // namespace

TEST_CASE("проецирование вершины: точка следует за вершиной модели") {
    Box b;
    doc::SketchFeature& sk = AddSketch(b.d);
    doc::Projection p;
    p.of = Of::Vertex;
    p.edge = EdgeAt(b.model, {20, 20, 10});   // the top back edge, ends at (40, 20, 10)
    p.at = {40, 20, 10};
    p.curve = ProjectedPoint(b.d, sk, {40, 20});
    sk.projections.push_back(p);
    Rebuild(b.d, b.model);
    REQUIRE(sk.status.state == FeatureState::Ok);
    CHECK(sk.lostProjections.empty());

    b.Widen(55);
    CHECK(sk.lostProjections.empty());
    const sketch::Point* pt = sk.sketch.FindPoint(p.curve);
    REQUIRE(pt);
    CHECK(pt->pos.x == Approx(55));
    CHECK(pt->pos.y == Approx(20));
    CHECK(sk.projections.front().at.x == Approx(55));
}

TEST_CASE("проецирование рабочей геометрии и начала координат") {
    doc::Document d;
    // A sketch on a plane 5 mm above XY.
    const Frame above{{0, 0, 5}, {1, 0, 0}, {0, 0, 1}};
    SUBCASE("начало координат и оси") {
        const auto origin = kernel::ProjectWork(d, doc::origin::kPoint, above, 30);
        REQUIRE(origin);
        REQUIRE(origin->point);
        CHECK(origin->point->Length() == Approx(0).epsilon(1e-9));
        const auto z = kernel::ProjectWork(d, doc::origin::kAxisZ, above, 30);   // square to the sketch: a point
        REQUIRE(z);
        CHECK(z->point.has_value());
        const auto x = kernel::ProjectWork(d, doc::origin::kAxisX, above, 30);
        REQUIRE(x);
        REQUIRE(x->curve);
        CHECK(std::abs(x->curve->start.y) < 1e-9);
        CHECK((x->curve->end - x->curve->start).Length() == Approx(60));
    }
    SUBCASE("плоскость: линия пересечения; параллельная — ошибка") {
        const auto yz = kernel::ProjectWork(d, doc::origin::kPlaneYZ, above, 25);
        REQUIRE(yz);
        REQUIRE(yz->curve);
        CHECK(std::abs(yz->curve->start.x) < 1e-9);
        CHECK(std::abs(yz->curve->end.x) < 1e-9);
        CHECK((yz->curve->end - yz->curve->start).Length() == Approx(50));
        CHECK_FALSE(kernel::ProjectWork(d, doc::origin::kPlaneXY, above, 25));
    }
    SUBCASE("рабочая плоскость со смещением: линия следует за ней, длина сохраняется") {
        auto wp = std::make_unique<doc::WorkPlaneFeature>();
        wp->id = d.Ids().Next();
        wp->name = "Плоскость1";
        wp->base = doc::origin::kPlaneYZ;
        wp->offset.name = d.NextParameterName();
        wp->offset.expression = "15";
        wp->offset.value = 15;
        doc::WorkPlaneFeature& plane = *wp;
        d.Insert(std::move(wp), 0);
        doc::SketchFeature& sk = AddSketch(d);
        kernel::Model model;
        model.Rebuild(d);
        const auto first = kernel::ProjectWork(d, plane.id, sk.placement.frame, 20);
        REQUIRE(first);
        REQUIRE(first->curve);
        doc::Projection p;
        p.of = Of::Work;
        p.work = plane.id;
        p.curve = ProjectedLine(d, sk, first->curve->start, first->curve->end);
        sk.projections.push_back(p);
        plane.offset.expression = "25";
        plane.offset.value = 25;
        Rebuild(d, model);
        CHECK(sk.lostProjections.empty());
        const sketch::Line* l = sk.sketch.FindLine(p.curve);
        REQUIRE(l);
        CHECK(sk.sketch.PositionOf(l->start).x == Approx(25));
        CHECK(sk.sketch.PositionOf(l->end).x == Approx(25));
        CHECK((sk.sketch.PositionOf(l->end) - sk.sketch.PositionOf(l->start)).Length() == Approx(40));
    }
}

TEST_CASE("проецирование кривой другого эскиза: следует за ней, без ребра тела") {
    doc::Document d;
    doc::SketchFeature& source = AddSketch(d);
    const ObjectId centre = source.sketch.AddPoint(d.Ids(), {10, 10});
    const ObjectId circle = source.sketch.AddCircle(d.Ids(), centre, 5);
    doc::SketchFeature& same = AddSketch(d);                                // the same plane: the circle
    doc::SketchFeature& side = AddSketch(d, doc::origin::kPlaneXZ);         // from the side: a line
    const doc::SketchSource src{source.id, circle, 0.0, 0.0, 1.0};

    const auto flat = kernel::ProjectSketchEntity(d, src, same.placement.frame);
    REQUIRE(flat);
    REQUIRE(flat->curve);
    CHECK(flat->curve->kind == kernel::ProjectedCurve::Kind::Circle);
    const auto edgeOn = kernel::ProjectSketchEntity(d, src, side.placement.frame);
    REQUIRE(edgeOn);
    REQUIRE(edgeOn->curve);
    CHECK(edgeOn->curve->kind == kernel::ProjectedCurve::Kind::Line);
    CHECK((edgeOn->curve->end - edgeOn->curve->start).Length() == Approx(10));

    // Linked: a bigger circle in the source makes a bigger projection.
    const ObjectId c = same.sketch.AddCircle(d.Ids(), ProjectedPoint(d, same, {10, 10}), 5);
    same.sketch.FindCircle(c)->projected = true;
    doc::Projection p;
    p.of = Of::Sketch;
    p.source = src;
    p.curve = c;
    same.projections.push_back(p);
    kernel::Model model;
    model.Rebuild(d);
    source.sketch.FindCircle(circle)->radius = 8;
    Rebuild(d, model);
    CHECK(same.lostProjections.empty());
    CHECK(same.sketch.FindCircle(c)->radius == Approx(8));

    // A sketch below in the tree is not built yet: lost, not followed.
    d.Move(source.id, d.FeatureCount());
    Rebuild(d, model);
    CHECK(same.lostProjections.count(c) == 1);
}

TEST_CASE("окружность под наклоном проецируется эллипсом, дуга — нет") {
    const Frame xy{{0, 0, 0}, {1, 0, 0}, {0, 0, 1}};
    const double s = std::sqrt(0.5);
    const Frame tilted{{0, 0, 0}, {1, 0, 0}, {0, -s, s}};
    kernel::PlanarCurve circle;
    circle.kind = kernel::PlanarCurve::Kind::Circle;
    circle.radius = 10;
    const auto e = kernel::ProjectPlanar(xy, circle, tilted);
    REQUIRE(e);
    REQUIRE(e->curve);
    CHECK(e->curve->kind == kernel::ProjectedCurve::Kind::Ellipse);
    CHECK((e->curve->start - e->curve->centre).Length() == Approx(10));
    CHECK((e->curve->end - e->curve->centre).Length() == Approx(10 * s));
    kernel::PlanarCurve arc = circle;
    arc.kind = kernel::PlanarCurve::Kind::Arc;
    arc.a = {10, 0};
    arc.b = {0, 10};
    CHECK_FALSE(kernel::ProjectPlanar(xy, arc, tilted));
    // From behind, an arc runs the other way round.
    const Frame back{{0, 0, 0}, {1, 0, 0}, {0, 0, -1}};
    const auto r = kernel::ProjectPlanar(xy, arc, back);
    REQUIRE(r);
    REQUIRE(r->curve);
    CHECK(r->curve->kind == kernel::ProjectedCurve::Kind::Arc);
}

TEST_CASE("проецирование сечения: всё, что режет плоскость эскиза") {
    Box b;
    // A plane square to Y through the middle of the box: a 40×10 rectangle.
    const Frame middle{{0, 10, 0}, {1, 0, 0}, {0, -1, 0}};
    doc::SketchFeature& sk = SketchOn(b.d, middle);
    Rebuild(b.d, b.model);
    const auto cut = b.model.ProjectSection(sk.placement.frame, static_cast<std::size_t>(b.d.IndexOf(sk.id)));
    REQUIRE(cut.size() == 4);
    double length = 0;
    for (const kernel::ProjectedCurve& c : cut) {
        CHECK(c.kind == kernel::ProjectedCurve::Kind::Line);
        length += (c.end - c.start).Length();
    }
    CHECK(length == Approx(100));
    // A plane beside the box cuts nothing.
    const Frame beside{{0, 30, 0}, {1, 0, 0}, {0, -1, 0}};
    CHECK(b.model.ProjectSection(beside, static_cast<std::size_t>(b.d.IndexOf(sk.id))).empty());
}

TEST_CASE("контур грани: перекрытия подрезаются, лишнее не дублируется") {
    using K = kernel::ProjectedCurve::Kind;
    const auto line = [](Vec2 a, Vec2 b) {
        kernel::ProjectedCurve c;
        c.start = a;
        c.end = b;
        return c;
    };
    bool changed = true;
    SUBCASE("ничего не пересекается — как было") {
        const auto out = kernel::TrimOverlappingCurves({line({0, 0}, {10, 0}), line({0, 5}, {10, 5})}, changed);
        CHECK_FALSE(changed);
        CHECK(out.size() == 2);
    }
    SUBCASE("отрезки на одной прямой внахлёст — один отрезок") {
        const auto out = kernel::TrimOverlappingCurves({line({0, 0}, {10, 0}), line({5, 0}, {15, 0})}, changed);
        CHECK(changed);
        REQUIRE(out.size() == 1);
        CHECK((out[0].end - out[0].start).Length() == Approx(15));
    }
    SUBCASE("окружность через отрезок — разрезаны в точках пересечения") {
        kernel::ProjectedCurve circle;
        circle.kind = K::Circle;
        circle.radius = 5;
        const auto out = kernel::TrimOverlappingCurves({circle, line({-10, 0}, {10, 0})}, changed);
        CHECK(changed);
        CHECK(std::ranges::count_if(out, [](const auto& c) { return c.kind == K::Arc; }) == 2);
        CHECK(std::ranges::count_if(out, [](const auto& c) { return c.kind == K::Line; }) == 3);
    }
}

TEST_CASE("проецирование развёртки листовой детали следует за ней") {
    doc::Document d;
    auto& base = AddSketch(d);
    AddRect(d, base, {0, 0}, {100, 50});
    auto sheet = std::make_unique<doc::SheetMetalFeature>();
    sheet->id = d.Ids().Next();
    sheet->name = "Лист";
    sheet->sketch = base.id;
    sheet->thickness.name = "s";
    sheet->thickness.expression = "2";
    const ObjectId sheetId = sheet->id;
    d.Insert(std::move(sheet), d.FeatureCount());
    auto flange = std::make_unique<doc::FlangeFeature>();
    flange->id = d.Ids().Next();
    flange->name = "Отбортовка";
    flange->sheet = sheetId;
    flange->edge = {50, 0, 0};
    flange->edgeSet = true;
    flange->height.expression = "20";
    flange->angle.expression = "90";
    doc::FlangeFeature& fl = *flange;
    d.Insert(std::move(flange), d.FeatureCount());
    doc::SketchFeature& sk = AddSketch(d);
    kernel::Model model;
    model.Rebuild(d);
    const std::size_t at = static_cast<std::size_t>(d.IndexOf(sk.id));
    const auto info = model.SheetBefore(d, sheetId, at);
    REQUIRE(info);
    const std::vector<kernel::PlanarCurve> curves = kernel::FlatPatternCurves(*info);
    CHECK(kernel::FlatPatternBends(*info) == 1);
    REQUIRE(curves.size() > 4);
    double low = 0;   // how far the blank reaches down (−Y): the flange unrolled
    for (std::size_t i = 0; i < curves.size(); ++i) {
        const auto item = kernel::ProjectPlanar(info->frame, curves[i], sk.placement.frame);
        REQUIRE(item);
        REQUIRE(item->curve);
        REQUIRE(item->curve->kind == kernel::ProjectedCurve::Kind::Line);
        doc::Projection p;
        p.of = Of::Flat;
        p.work = sheetId;
        p.part = static_cast<int>(i);
        p.curve = ProjectedLine(d, sk, item->curve->start, item->curve->end);
        sk.projections.push_back(p);
        low = std::min({low, item->curve->start.y, item->curve->end.y});
    }
    CHECK(low < -15);
    fl.height.expression = "30";
    Rebuild(d, model);
    CHECK(sk.lostProjections.empty());
    double lowAfter = 0;
    for (const sketch::Point& p : sk.sketch.points) lowAfter = std::min(lowAfter, p.pos.y);
    CHECK(lowAfter < low - 5);
}

TEST_CASE("новые виды проекций сохраняются и читаются") {
    doc::SketchFeature sk;
    sk.id = ObjectId{100};
    sk.placement = {doc::origin::kPlaneXY, *doc::origin::PlaneFrame(doc::origin::kPlaneXY), false, {}};
    doc::Projection vertex;
    vertex.of = Of::Vertex;
    vertex.edge.middle = {1, 2, 3};
    vertex.at = {4, 5, 6};
    vertex.curve = ObjectId{101};
    doc::Projection work;
    work.of = Of::Work;
    work.work = doc::origin::kAxisX;
    work.curve = ObjectId{102};
    doc::Projection other;
    other.of = Of::Sketch;
    other.source = {ObjectId{50}, ObjectId{51}, 0.0, 0.0, 1.0};
    other.curve = ObjectId{103};
    doc::Projection flat;
    flat.of = Of::Flat;
    flat.work = ObjectId{60};
    flat.part = 7;
    flat.curve = ObjectId{104};
    sk.projections = {vertex, work, other, flat};
    nlohmann::json j;
    sk.SaveFields(j);
    CHECK_FALSE(j["projections"][1].contains("edge"));   // only edges and vertices keep an edge
    doc::SketchFeature back;
    REQUIRE(back.LoadFields(j).Ok());
    REQUIRE(back.projections.size() == 4);
    CHECK(back.projections[0].of == Of::Vertex);
    CHECK(back.projections[0].at.y == Approx(5));
    CHECK(back.projections[0].edge.middle.z == Approx(3));
    CHECK(back.projections[1].of == Of::Work);
    CHECK(back.projections[1].work == doc::origin::kAxisX);
    CHECK(back.projections[2].of == Of::Sketch);
    CHECK(back.projections[2].source.curve == ObjectId{51});
    CHECK(back.projections[3].of == Of::Flat);
    CHECK(back.projections[3].part == 7);
    CHECK(back.projections[3].work == ObjectId{60});
    // A file of an older version (an edge without "of") reads as an edge.
    nlohmann::json old = j;
    old["projections"] = nlohmann::json::array({old["projections"][0]});
    old["projections"][0].erase("of");
    old["projections"][0].erase("at");
    doc::SketchFeature older;
    REQUIRE(older.LoadFields(old).Ok());
    CHECK(older.projections.front().of == Of::Edge);
}
