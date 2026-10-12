#include "../Fixtures.h"
#include "kernel/Model.h"
#include "kernel/Topology.h"
#include "kernel/occt/FaceMatch.h"
#include "kernel/occt/EdgeMatch.h"
#include "kernel/occt/ModelImpl.h"

#include <doctest.h>

#include <algorithm>

using namespace bz;
using namespace bz::test;

TEST_CASE("topology lists faces and edges with what they are and where") {
    doc::Document d;
    auto& sk = AddSketch(d);
    AddRect(d, sk, {0, 0}, {20, 10});
    AddCircle(d, sk, {10, 5}, 2);   // a hole through
    AddExtrude(d, sk.id, "5");
    kernel::Model model;
    model.Rebuild(d);
    const kernel::Topology t = kernel::DescribeTopology(model);

    // 6 sides + the bore.
    REQUIRE(t.faces.size() == 7);
    const auto planes = std::count_if(t.faces.begin(), t.faces.end(), [](const auto& f) { return f.kind == doc::SurfaceKind::Plane; });
    CHECK(planes == 6);
    const auto bore = std::find_if(t.faces.begin(), t.faces.end(), [](const auto& f) { return f.kind == doc::SurfaceKind::Cylinder; });
    REQUIRE(bore != t.faces.end());
    CHECK(bore->hole);
    REQUIRE(bore->axis);
    CHECK(bore->axis->radius == doctest::Approx(2));
    CHECK(bore->axis->axisDir.Cross({0, 0, 1}).Length() < 1e-9);

    // The top face: normal +Z, area of the plate less the hole, and its reference finds it again.
    const auto top = std::find_if(t.faces.begin(), t.faces.end(), [](const auto& f) {
        return f.kind == doc::SurfaceKind::Plane && f.normal.z > 0.99 && f.point.z > 4.9;
    });
    REQUIRE(top != t.faces.end());
    CHECK(top->area == doctest::Approx(200 - kPi * 4).epsilon(1e-6));
    const auto again = kernel::occt::ResolveFace(model.GetImpl().bodies.front().shape, top->ref);
    CHECK(again.has_value());
    CHECK(top->edges.size() == 5);   // four sides and the hole's rim

    // Edges: 12 of the box, 2 rims and the bore's seam; each line with its length.
    const auto lines = std::count_if(t.edges.begin(), t.edges.end(), [](const auto& e) { return e.kind == doc::CurveKind::Line; });
    const auto circles = std::count_if(t.edges.begin(), t.edges.end(), [](const auto& e) { return e.kind == doc::CurveKind::Circle; });
    CHECK(circles == 2);
    CHECK(lines >= 12);
    const auto rim = std::find_if(t.edges.begin(), t.edges.end(), [](const auto& e) { return e.kind == doc::CurveKind::Circle; });
    CHECK(rim->radius == doctest::Approx(2));
    REQUIRE(rim->centre);
    CHECK(rim->centre->x == doctest::Approx(10));
    CHECK(rim->faces.size() == 2);
    const auto longEdges = std::count_if(t.edges.begin(), t.edges.end(), [](const auto& e) {
        return e.kind == doc::CurveKind::Line && std::abs(e.length - 20) < 1e-6;
    });
    CHECK(longEdges == 4);

    // Distance between the bottom and the top face.
    const auto bottom = std::find_if(t.faces.begin(), t.faces.end(), [](const auto& f) { return f.kind == doc::SurfaceKind::Plane && f.normal.z < -0.99; });
    REQUIRE(bottom != t.faces.end());
    const auto m = kernel::MeasureBetween(model, true, top->index, true, bottom->index);
    REQUIRE(m);
    CHECK(m->distance == doctest::Approx(5));
    CHECK_FALSE(kernel::MeasureBetween(model, true, 99, true, 0));
}
