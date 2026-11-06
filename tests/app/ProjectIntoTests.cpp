// «Проецировать геометрию»: how projected curves and points go into the sketch
// (app::Projector) — linked or an independent copy, shared corners, nothing
// doubled — and the part's origin projected when a sketch is made.
#include "../Fixtures.h"
#include "app/Settings.h"
#include "app/sketch/ProjectInto.h"

#include <doctest.h>

#include <algorithm>

using namespace bz;
using namespace bz::test;
using doctest::Approx;

namespace {

kernel::ProjectedCurve Line(Vec2 a, Vec2 b) {
    kernel::ProjectedCurve c;
    c.start = a;
    c.end = b;
    return c;
}

std::size_t Count(const sketch::Sketch& sk, sketch::ConstraintType type) {
    return static_cast<std::size_t>(std::ranges::count_if(sk.constraints, [&](const sketch::Constraint& c) { return c.type == type; }));
}

} // namespace

TEST_CASE("связанная точка запоминает источник, независимая — закреплена") {
    doc::Document d;
    doc::SketchFeature& sk = AddSketch(d);
    doc::Projection link;
    link.of = app::Of::Work;
    link.work = doc::origin::kAxisZ;
    {
        app::Projector put(d, sk, 1e-6, true, false);
        CHECK(put.LonePoint({5, 5}, &link));
        REQUIRE(sk.projections.size() == 1);
        CHECK(sk.projections.front().curve == put.last);
        CHECK(sk.sketch.FindPoint(put.last)->projected);
        // The same source again: not doubled.
        CHECK_FALSE(put.LonePoint({5, 5}, &link));
        CHECK(put.already == 1);
    }
    {
        app::Projector put(d, sk, 1e-6, false, false);
        CHECK(put.LonePoint({7, 3}, nullptr));
        CHECK_FALSE(sk.sketch.FindPoint(put.last)->projected);
        CHECK(Count(sk.sketch, sketch::ConstraintType::Fix) == 1);
        CHECK(sk.projections.size() == 1);
    }
    // The sketch's own origin is there already.
    app::Projector put(d, sk, 1e-6, true, false);
    CHECK_FALSE(put.LonePoint({0, 0}, nullptr));
}

TEST_CASE("независимая копия: концы закреплены, у окружности — размер; соседние кривые делят угол") {
    doc::Document d;
    doc::SketchFeature& sk = AddSketch(d);
    app::Projector put(d, sk, 1e-6, false, false);
    CHECK(put.Curve(Line({0, 0}, {10, 0}), nullptr));
    CHECK(put.Curve(Line({10, 0}, {10, 5}), nullptr));
    CHECK(sk.sketch.lines.size() == 2);
    CHECK(sk.sketch.lines[0].end == sk.sketch.lines[1].start);   // one corner
    kernel::ProjectedCurve circle;
    circle.kind = kernel::ProjectedCurve::Kind::Circle;
    circle.centre = {20, 0};
    circle.radius = 3;
    CHECK(put.Curve(circle, nullptr));
    CHECK(Count(sk.sketch, sketch::ConstraintType::Diameter) == 1);
    CHECK(Count(sk.sketch, sketch::ConstraintType::Fix) == 4);   // three line ends and the centre
    CHECK(sk.projections.empty());
    // The same line once more: there already.
    CHECK_FALSE(put.Curve(Line({10, 0}, {0, 0}), nullptr));
    CHECK(put.added == 3);
}

TEST_CASE("кривая другого эскиза: связь через эскиз-источник, повтор не задваивает") {
    doc::Document d;
    doc::SketchFeature& sk = AddSketch(d);
    doc::Projection link;
    link.of = app::Of::Sketch;
    link.source = {ObjectId{77}, ObjectId{78}, 0.0, 0.0, 1.0};
    app::Projector put(d, sk, 1e-6, true, false);
    CHECK(put.Curve(Line({0, 0}, {5, 5}), &link));
    REQUIRE(sk.projections.size() == 1);
    CHECK(sk.projections.front().source.curve == ObjectId{78});
    CHECK(sk.sketch.FindLine(sk.projections.front().curve)->projected);
    CHECK_FALSE(put.Curve(Line({0, 0}, {5, 5}), &link));
    CHECK(sk.projections.size() == 1);
}

TEST_CASE("линии сгиба развёртки ложатся осевыми, тип линии «вспомогательная» — вспомогательными") {
    doc::Document d;
    doc::SketchFeature& sk = AddSketch(d);
    app::Projector put(d, sk, 1e-6, true, true);
    CHECK(put.Curve(Line({0, 0}, {10, 0}), nullptr));
    CHECK(sk.sketch.FindLine(put.last)->construction);
    CHECK(put.Curve(Line({0, 5}, {10, 5}), nullptr, true));
    CHECK(sk.sketch.FindLine(put.last)->centerline);
    CHECK_FALSE(sk.sketch.FindLine(put.last)->construction);
}

TEST_CASE("начало координат детали при создании эскиза") {
    doc::Document d;
    SUBCASE("эскиз на плоскости через начало — своя точка начала уже там") {
        doc::SketchFeature& sk = AddSketch(d);
        CHECK_FALSE(app::ProjectPartOrigin(d, sk));
        CHECK(sk.projections.empty());
    }
    SUBCASE("эскиз со сдвигом — связанная точка начала координат") {
        doc::SketchFeature& sk = AddSketch(d);
        sk.placement.frame.origin = {10, 20, 5};
        CHECK(app::ProjectPartOrigin(d, sk));
        REQUIRE(sk.projections.size() == 1);
        CHECK(sk.projections.front().of == app::Of::Work);
        CHECK(sk.projections.front().work == doc::origin::kPoint);
        const sketch::Point* p = sk.sketch.FindPoint(sk.projections.front().curve);
        REQUIRE(p);
        CHECK(p->pos.x == Approx(-10));
        CHECK(p->pos.y == Approx(-20));
    }
}

TEST_CASE("настройки проецирования по умолчанию: автопроецирование включено, начало координат — нет") {
    app::Settings s;
    CHECK(s.autoProjectEdges);
    CHECK_FALSE(s.projectOriginOnCreate);
}
