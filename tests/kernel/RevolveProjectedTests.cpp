// Б4: revolving projected geometry. A body of revolution is projected into a
// sketch (axis, outline, cut) exactly as the "Проецировать геометрию" tool
// does it, and every region of that sketch is turned around the projected
// axis in every way: full, by an angle, symmetric; as a new body, joined,
// cut. Each result must be an operation error or a valid body that meshes
// (the viewport meshes it right away, outside any fault guard).

#include "../Fixtures.h"
#include "document/WorkPlaneFeature.h"
#include "kernel/Model.h"
#include "kernel/occt/ModelImpl.h"

#include <BRepCheck_Analyzer.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <doctest.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <functional>
#include <string>
#include <string_view>

using namespace bz;
using namespace bz::test;
using doc::BooleanOp;
using doc::FeatureState;
using doc::RevolveAxisKind;
using doc::RevolveExtent;

namespace {

void AddPolygon(doc::Document& d, doc::SketchFeature& sk, const std::vector<Vec2>& corners) {
    std::vector<ObjectId> points;
    for (const Vec2& c : corners) points.push_back(sk.sketch.AddPoint(d.Ids(), c));
    for (std::size_t i = 0; i < points.size(); ++i) sk.sketch.AddLine(d.Ids(), points[i], points[(i + 1) % points.size()]);
}

/// A body of revolution around the line z = 40 along X (profile on XZ).
void Lathe(doc::Document& d, const std::function<void(doc::SketchFeature&)>& profile) {
    auto& sk = AddSketch(d, doc::origin::kPlaneXZ);
    profile(sk);
    const ObjectId axis = sk.sketch.AddLine(d.Ids(), sk.sketch.AddPoint(d.Ids(), {-40, 40}), sk.sketch.AddPoint(d.Ids(), {40, 40}), true);
    AddRevolve(d, sk.id, {RevolveAxisKind::SketchLine, axis});
}

/// Puts the projection into `sk` the way SketchEditor::HandleProject does
/// (linked: points and curves held by the model). Returns the axis line.
ObjectId Place(doc::Document& d, doc::SketchFeature& sk, const kernel::RevolvedProjection& r) {
    sketch::Sketch& s = sk.sketch;
    IdGenerator& ids = d.Ids();
    const auto point = [&](Vec2 at) {
        for (const sketch::Point& p : s.points)
            if (p.projected && (p.pos - at).Length() < 1e-6) return p.id;
        const ObjectId id = s.AddPoint(ids, at);
        s.FindPoint(id)->projected = true;
        return id;
    };
    const auto place = [&](const kernel::ProjectedCurve& c, bool axis) -> ObjectId {
        switch (c.kind) {
            case kernel::ProjectedCurve::Kind::Line: {
                if ((c.end - c.start).Length() < 1e-9) return {};
                const ObjectId l = s.AddLine(ids, point(c.start), point(c.end));
                s.FindLine(l)->projected = true;
                s.FindLine(l)->centerline = axis;
                return l;
            }
            case kernel::ProjectedCurve::Kind::Circle: {
                const ObjectId id = s.AddCircle(ids, point(c.centre), c.radius);
                s.FindCircle(id)->projected = true;
                return id;
            }
            case kernel::ProjectedCurve::Kind::Arc: {
                const ObjectId id = s.AddArc(ids, point(c.centre), point(c.start), point(c.end));
                for (auto& a : s.arcs)
                    if (a.id == id) a.projected = true;
                return id;
            }
            case kernel::ProjectedCurve::Kind::Ellipse: return s.AddEllipse(ids, point(c.centre), point(c.start), point(c.end));
        }
        return {};
    };
    ObjectId axis;
    if (r.axis) axis = place(*r.axis, true);
    for (const auto& c : r.outline) place(c, false);
    for (const auto& c : r.section) place(c, false);
    return axis;
}

struct Base {
    const char*                             name;
    std::function<void(doc::Document&)>     build;
    Vec3                                    point, normal;   ///< a point of the face to project
};

struct Plane {
    const char* name;
    ObjectId    base;
    const char* offset;   ///< "" for the origin plane itself
};

std::vector<Base> Bases() {
    return {
        {"solid cylinder", [](doc::Document& d) { Lathe(d, [&](auto& sk) { AddPolygon(d, sk, {{-30, 20}, {30, 20}, {30, 40}, {-30, 40}}); }); },
         {0, 0, 20}, {0, 0, -1}},
        {"tube", [](doc::Document& d) { Lathe(d, [&](auto& sk) { AddPolygon(d, sk, {{-30, 20}, {30, 20}, {30, 30}, {-30, 30}}); }); },
         {0, 0, 20}, {0, 0, -1}},
        {"stepped shaft", [](doc::Document& d) {
             Lathe(d, [&](auto& sk) { AddPolygon(d, sk, {{-30, 40}, {-30, 25}, {0, 25}, {0, 15}, {30, 15}, {30, 40}}); }); },
         {15, 0, 15}, {0, 0, -1}},
        {"cone", [](doc::Document& d) { Lathe(d, [&](auto& sk) { AddPolygon(d, sk, {{-30, 40}, {-30, 20}, {30, 30}, {30, 40}}); }); },
         {0, 0, 25}, {0, -0.16, -0.98}},
        {"sphere", [](doc::Document& d) {
             Lathe(d, [&](doc::SketchFeature& sk) {
                 auto& s = sk.sketch;
                 const ObjectId o = s.AddPoint(d.Ids(), {0, 40});
                 const ObjectId a = s.AddPoint(d.Ids(), {20, 40}), b = s.AddPoint(d.Ids(), {-20, 40});
                 s.AddArc(d.Ids(), o, a, b);
                 s.AddLine(d.Ids(), b, a);
             }); },
         {0, 0, 20}, {0, 0, -1}},
        {"torus", [](doc::Document& d) { Lathe(d, [&](auto& sk) { AddCircle(d, sk, {0, 25}, 6); }); }, {0, 0, 19}, {0, 0, -1}},
        {"extruded cylinder (axis Z)", [](doc::Document& d) {
             auto& sk = AddSketch(d);
             AddCircle(d, sk, {0, 0}, 15);
             AddExtrude(d, sk.id, "30");
         },
         {15, 0, 10}, {1, 0, 0}},
    };
}

std::vector<Plane> Planes() {
    return {
        {"XZ", doc::origin::kPlaneXZ, ""},          // through the axis (both kinds of body)
        {"XZ-8", doc::origin::kPlaneXZ, "8"},       // beside it, cutting the body
        {"XY", doc::origin::kPlaneXY, ""},          // parallel to the lathe axis, below it; across the Z cylinder
        {"YZ", doc::origin::kPlaneYZ, ""},          // across the lathe axis; through the Z axis
    };
}

doc::WorkPlaneFeature& AddOffsetPlane(doc::Document& d, ObjectId base, const std::string& offset) {
    auto wp = std::make_unique<doc::WorkPlaneFeature>();
    wp->id = d.Ids().Next();
    wp->name = d.UniqueFeatureName("Плоскость");
    wp->base = base;
    wp->offset.expression = offset;
    auto* raw = wp.get();
    d.Insert(std::move(wp), d.FeatureCount());
    return *raw;
}

/// Every body of `model` is valid and meshes as the viewport would mesh it.
bool BodiesMesh(const kernel::Model& model) {
    for (const auto& body : model.GetImpl().bodies) {
        if (!BRepCheck_Analyzer(body.shape).IsValid()) return false;
        BRepMesh_IncrementalMesh mesh(body.shape, 0.05, false, 0.5, false);
        if (!mesh.IsDone()) return false;
    }
    return true;
}

} // namespace

TEST_CASE("revolving projected geometry of bodies of revolution never breaks the model") {
    // The whole matrix takes minutes; by default (CI, the Android emulator) a
    // part of it: full turns, new body and cut, around the projected axis.
    // BZ_STRESS=1 runs all of it.
    const bool all = std::getenv("BZ_STRESS") != nullptr;
    int built = 0, refused = 0;
    for (const Base& base : Bases())
        for (const Plane& plane : Planes()) {
            if (!all && std::string_view(plane.name) != "XZ" && std::string_view(plane.name) != "XZ-8") continue;
            // The sketch with the projection, its regions known after a rebuild.
            doc::Document d;
            base.build(d);
            ObjectId support = plane.base;
            if (*plane.offset) support = AddOffsetPlane(d, plane.base, plane.offset).id;
            auto& sk = AddSketch(d, support);
            kernel::Model model;
            model.Rebuild(d);
            const auto index = static_cast<std::size_t>(d.IndexOf(sk.id));
            const doc::FaceReference face{.normal = base.normal.Normalized(), .point = base.point, .curved = true};
            const Expected<kernel::RevolvedProjection> r = model.ProjectRevolved(face, sk.placement.frame, index);
            std::fprintf(stderr, "[revolve-projected] %s on %s: %s\n", base.name, plane.name,
                         r ? "projected" : r.GetError().message.c_str());
            if (!r) continue;
            const ObjectId axis = Place(d, sk, *r);
            d.MarkChanged(index);
            model.Rebuild(d);
            REQUIRE_MESSAGE(sk.status.state != FeatureState::Error, sk.status.message);
            const auto* regions = model.Regions(sk.id);
            std::vector<std::optional<Vec2>> picks{std::nullopt};   // automatic first
            if (regions)
                for (const kernel::SketchRegion& region : *regions) picks.push_back(region.sample);

            for (const auto& pick : picks)
                for (const RevolveExtent extent : {RevolveExtent::Full, RevolveExtent::Angle, RevolveExtent::Symmetric})
                    for (const BooleanOp op : {BooleanOp::NewBody, BooleanOp::Join, BooleanOp::Cut})
                        for (const bool projectedAxis : {true, false}) {
                            if (projectedAxis && !axis) continue;
                            if (!all && (extent != RevolveExtent::Full || op == BooleanOp::Join || !projectedAxis)) continue;
                            doc::Document copy = d.Clone();
                            auto* s = copy.FindAs<doc::SketchFeature>(sk.id);
                            REQUIRE(s);
                            const doc::RevolveAxis turn = projectedAxis ? doc::RevolveAxis{RevolveAxisKind::SketchLine, axis}
                                                                        : doc::RevolveAxis{RevolveAxisKind::SketchX, {}};
                            auto& rev = AddRevolve(copy, s->id, turn, extent, "120");
                            rev.operation = op;
                            if (pick) {
                                rev.profiles.automatic = false;
                                rev.profiles.regions = {*pick};
                            }
                            if (all) std::fprintf(stderr, "  region %s, extent %d, op %d, axis %s\n",
                                         pick ? std::format("({:.2f}, {:.2f})", pick->x, pick->y).c_str() : "auto",
                                         static_cast<int>(extent), static_cast<int>(op), projectedAxis ? "projected" : "sketch X");
                            kernel::Model m;
                            m.Rebuild(copy);
                            if (rev.status.state == FeatureState::Ok) {
                                ++built;
                                CHECK_MESSAGE(BodiesMesh(m), base.name, " on ", plane.name);
                            } else {
                                ++refused;
                                CHECK_FALSE(rev.status.message.empty());
                            }
                        }
        }
    MESSAGE("built ", built, ", refused ", refused);
    CHECK(built > 0);
}

// Found by the test above: a tube's outline on YZ (across its axis), one
// region turned a full turn around sketch X (model Y) and cut from the tube.
// The cut itself is quick; cleaning up its faces (UnifySameDomain) ran for
// more than ten minutes — the program hung on "Вращение".
TEST_CASE("cutting a tube with a ring across it does not hang on cleaning up") {
    doc::Document d;
    Bases()[1].build(d);   // tube
    auto& sk = AddSketch(d, doc::origin::kPlaneYZ);
    kernel::Model model;
    model.Rebuild(d);
    const auto index = static_cast<std::size_t>(d.IndexOf(sk.id));
    const doc::FaceReference face{.normal = {0, 0, -1}, .point = {0, 0, 20}, .curved = true};
    const auto r = model.ProjectRevolved(face, sk.placement.frame, index);
    REQUIRE(r);
    // The outline is a circle: one or two arcs (the halves), not short lines.
    CHECK(r->outline.size() <= 4);
    Place(d, sk, *r);
    d.MarkChanged(index);
    model.Rebuild(d);
    auto& rev = AddRevolve(d, sk.id, {RevolveAxisKind::SketchX, {}}, RevolveExtent::Full);
    rev.operation = BooleanOp::Cut;
    rev.profiles.automatic = false;
    rev.profiles.regions = {{-1.36, 38.66}};
    const auto start = std::chrono::steady_clock::now();
    model.Rebuild(d);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(seconds < 10);
    CHECK_MESSAGE((rev.status.state == FeatureState::Ok || !rev.status.message.empty()), rev.status.message);
    if (rev.status.state == FeatureState::Ok) CHECK(BodiesMesh(model));
}

// Found by the test above: a sphere's outline (on XZ, through its axis) turned
// around sketch X. The outline is a circle; it came as hundreds of short lines
// and turning them made OCCT fault (SIGSEGV) inside the revolve.
TEST_CASE("a projected sphere outline turns into a body without an OCCT fault") {
    doc::Document d;
    Bases()[4].build(d);   // sphere
    auto& sk = AddSketch(d, doc::origin::kPlaneXZ);
    kernel::Model model;
    model.Rebuild(d);
    const auto index = static_cast<std::size_t>(d.IndexOf(sk.id));
    const doc::FaceReference face{.normal = {0, 0, -1}, .point = {0, 0, 20}, .curved = true};
    const auto r = model.ProjectRevolved(face, sk.placement.frame, index);
    REQUIRE(r);
    // The outline is a circle: one or two arcs (the halves), not short lines.
    CHECK(r->outline.size() <= 4);
    Place(d, sk, *r);
    d.MarkChanged(index);
    model.Rebuild(d);
    auto& rev = AddRevolve(d, sk.id, {RevolveAxisKind::SketchX, {}}, RevolveExtent::Full);
    model.Rebuild(d);
    CHECK(rev.status.message.find("OpenCASCADE") == std::string::npos);
    CHECK_MESSAGE(rev.status.state == FeatureState::Ok, rev.status.message);
    if (rev.status.state == FeatureState::Ok) CHECK(BodiesMesh(model));
}
