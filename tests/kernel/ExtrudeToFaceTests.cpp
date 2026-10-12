#include "../Fixtures.h"
#include "kernel/Model.h"

#include <doctest.h>

#include <cmath>
#include <vector>

using namespace bz;
using namespace bz::test;
using doc::BooleanOp;
using doc::FeatureState;

namespace {

/// A closed polygon in `sk` (sketch coordinates).
void AddPolygon(doc::Document& d, doc::SketchFeature& sk, const std::vector<Vec2>& corners) {
    std::vector<ObjectId> points;
    for (const Vec2& c : corners) points.push_back(sk.sketch.AddPoint(d.Ids(), c));
    for (std::size_t i = 0; i < points.size(); ++i) sk.sketch.AddLine(d.Ids(), points[i], points[(i + 1) % points.size()]);
}

/// A construction line from `a` to `b`: the axis of a revolve.
ObjectId AddAxis(doc::Document& d, doc::SketchFeature& sk, Vec2 a, Vec2 b) {
    return sk.sketch.AddLine(d.Ids(), sk.sketch.AddPoint(d.Ids(), a), sk.sketch.AddPoint(d.Ids(), b), true);
}

/// A body of revolution around the line z = 40 along X (sketch on XZ):
/// `profile` in sketch coordinates (x, z), turned a full turn.
doc::RevolveFeature& AddLathe(doc::Document& d, const std::vector<Vec2>& profile) {
    auto& sk = AddSketch(d, doc::origin::kPlaneXZ);
    AddPolygon(d, sk, profile);
    const ObjectId axis = AddAxis(d, sk, {-40, 40}, {40, 40});
    return AddRevolve(d, sk.id, {doc::RevolveAxisKind::SketchLine, axis});
}

/// A 10 x 10 post from the XY plane up to the curved face picked at
/// (1, y, z), outward normal `normal`.
doc::ExtrudeFeature& AddPostUpTo(doc::Document& d, Vec3 point, Vec3 normal) {
    auto& sk = AddSketch(d);
    AddRect(d, sk, {-5, -5}, {5, 5});
    auto& post = AddExtrude(d, sk.id, "1", BooleanOp::NewBody);
    post.extent = doc::ExtentType::ToPlane;
    post.toFace = {.normal = normal.Normalized(), .point = point, .curved = true};
    return post;
}

/// The post's volume under a cylinder of radius `r` around z = 40 along X.
double PostUnder(double r) {
    double area = 0;
    constexpr int n = 2000;
    for (int i = 0; i < n; ++i) {
        const double y = -5 + 10.0 * (i + 0.5) / n;
        area += (40 - std::sqrt(r * r - y * y)) * 10.0 / n;
    }
    return area * 10;
}

} // namespace

// Б1, этап 1 п.2. A body of revolution has its seam where its profile was;
// a face picked on the seam used to get the normal of the far side, so the
// face was not found again (or a wrong face behind it was taken).
TEST_CASE("extrude up to a curved face of a body of revolution, picked on its seam or off it") {
    struct Case { const char* name; std::vector<Vec2> profile; double lathe; };
    const std::vector<Case> cases{
        {"solid", {{-30, 20}, {30, 20}, {30, 40}, {-30, 40}}, kPi * 400 * 60},
        {"tube", {{-30, 20}, {30, 20}, {30, 30}, {-30, 30}}, kPi * 300 * 60},   // an inner face behind
    };
    for (const Case& c : cases)
        for (const double y : {0.0, 3.0})          // 0: on the seam (the sketch plane XZ)
            for (const double sag : {0.0, 0.05}) {   // picked on the mesh: a little inside
                CAPTURE(c.name);
                CAPTURE(y);
                CAPTURE(sag);
                doc::Document d;
                auto& rev = AddLathe(d, c.profile);
                const double z = 40 - std::sqrt(400 - y * y);
                const Vec3 normal = Vec3{0, y, z - 40}.Normalized();
                auto& post = AddPostUpTo(d, Vec3{1, y, z} - normal * sag, normal);
                kernel::Model model;
                model.Rebuild(d);
                REQUIRE(rev.status.state == FeatureState::Ok);
                REQUIRE(post.status.state == FeatureState::Ok);
                CHECK(model.Volume() - c.lathe == doctest::Approx(PostUnder(20)).epsilon(1e-3));
            }
}

TEST_CASE("extrude up to a cone, a torus and a sphere picked on the seam") {
    for (const int kind : {0, 1, 2}) {
        CAPTURE(kind);
        double built[2] = {};
        for (const double y : {0.0, 3.0}) {
            doc::Document d;
            auto& sk = AddSketch(d, doc::origin::kPlaneXZ);
            double rho = 0;   // distance from the axis under the post's corner (1, y)
            if (kind == 0) {
                AddPolygon(d, sk, {{-30, 30}, {30, 15}, {30, 40}, {-30, 40}});   // radius 10..25
                rho = 17.75;
            } else if (kind == 1) {
                AddCircle(d, sk, {0, 20}, 8);   // a ring: 20 from the axis, 8 thick
                rho = 20 + std::sqrt(63.0);
            } else {
                auto& s = sk.sketch;
                const ObjectId o = s.AddPoint(d.Ids(), {0, 40});
                const ObjectId a = s.AddPoint(d.Ids(), {-20, 40}), b = s.AddPoint(d.Ids(), {20, 40});
                s.AddArc(d.Ids(), o, a, b);
                s.AddLine(d.Ids(), b, a);
                rho = std::sqrt(399.0);
            }
            auto& rev = AddRevolve(d, sk.id, {doc::RevolveAxisKind::SketchLine, AddAxis(d, sk, {-40, 40}, {40, 40})});
            const double z = 40 - std::sqrt(rho * rho - y * y);
            auto& post = AddPostUpTo(d, {1, y, z}, {0, y, z - 40});
            kernel::Model model;
            model.Rebuild(d);
            REQUIRE(rev.status.state == FeatureState::Ok);
            REQUIRE(post.status.state == FeatureState::Ok);
            built[y == 0 ? 0 : 1] = model.Volume();
        }
        CHECK(built[0] == doctest::Approx(built[1]));   // the same post wherever the face was picked
    }
}
