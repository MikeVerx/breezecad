// Designing in an assembly: a part shown from memory (edited in place), the
// geometry a constraint can take (edges, vertices, origin and work planes),
// edges of one part projected into another's sketch and following it
// (adaptivity), cycles refused, and what all of it costs.
#include "../Fixtures.h"
#include "assembly/Solver.h"
#include "document/BzcFile.h"
#include "document/Recompute.h"
#include "document/WorkPlaneFeature.h"
#include "kernel/AssemblyModel.h"
#include "kernel/ContextProjection.h"
#include "kernel/Model.h"
#include "kernel/occt/ModelImpl.h"
#include "kernel/Projection.h"

#include <doctest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <map>

using namespace bz;
using namespace bz::assembly;
using doctest::Approx;

namespace {

/// `w` x 40 x 10 plate from the origin up, with a hole of radius 4 at (hx, 20).
doc::Document Plate(double w = 40, double hx = 20) {
    doc::Document d;
    auto& sk = test::AddSketch(d);
    test::AddRect(d, sk, {0, 0}, {w, 40});
    test::AddCircle(d, sk, {hx, 20}, 4);
    test::AddExtrude(d, sk.id, "10");
    return d;
}

/// The plate's hole moved to `hx` and its length to `w`: what an edit in place does.
void Reshape(doc::Document& d, double w, double hx) {
    auto* sk = dynamic_cast<doc::SketchFeature*>(d.Features()[0].get());
    REQUIRE(sk);
    for (sketch::Point& p : sk->sketch.points) {
        if (p.isOrigin) continue;
        if (std::abs(p.pos.y - 20) < 1e-9 && p.pos.x > 0 && p.pos.x < 1000) p.pos.x = hx;   // the hole's centre
        else if (p.pos.x > 1e-9) p.pos.x = w;                                              // the far side
    }
    d.MarkChanged(0);
}

/// A part in memory, as AssemblyState keeps one edited in place.
struct MemoryPart {
    doc::Document doc;
    kernel::Model model;
    void Rebuild() { model.Rebuild(doc); }
};

struct Bench {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "bz_assembly_design";
    std::filesystem::path file = dir / "Узел.bza";
    Assembly              a;
    kernel::AssemblyModel m;
    ObjectId              plate;

    Bench() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        REQUIRE(doc::SaveBzc(Plate(), dir / "plate.bzc", "tests"));
        plate = a.AddComponent("plate.bzc", "Плита").id;   // grounded (the first)
        m.Update(a, file);
    }
    ~Bench() { std::filesystem::remove_all(dir); }

    /// A component whose part lives in memory, placed at `at`.
    ObjectId AddMemoryPart(MemoryPart& p, const std::string& name, const Placement& at) {
        const ObjectId id = a.AddComponent(name + ".bzc", name, at).id;
        p.Rebuild();
        m.UsePartInMemory(dir / (name + ".bzc"), &p.doc, &p.model);
        m.Update(a, file);
        return id;
    }
};

/// Projects the source edge nearest `worldPoint` (on `source`) into the first
/// sketch of `target`'s part `part`, linked, as the sketch editor does.
ObjectId ProjectInto(Bench& b, MemoryPart& part, ObjectId target, ObjectId source, Vec3 worldPoint) {
    auto* sk = dynamic_cast<doc::SketchFeature*>(part.doc.Features()[0].get());
    REQUIRE(sk);
    const std::optional<doc::EdgeReference> edge = b.m.EdgeNear(b.a, source, worldPoint, 1.0);
    REQUIRE(edge);
    doc::ContextProjection link{"Узел", source, *edge, {}, true};
    const Expected<kernel::ProjectedCurve> pc = b.m.ProjectSourceEdge(b.a, target, link, sk->placement.frame);
    REQUIRE(pc);
    IdGenerator& ids = part.doc.Ids();
    ObjectId curve;
    if (pc->kind == kernel::ProjectedCurve::Kind::Circle) {
        const ObjectId c = sk->sketch.AddPoint(ids, pc->centre);
        sk->sketch.FindPoint(c)->projected = true;
        curve = sk->sketch.AddCircle(ids, c, pc->radius);
        sk->sketch.FindCircle(curve)->projected = true;
    } else {
        REQUIRE(pc->kind == kernel::ProjectedCurve::Kind::Line);
        const ObjectId p0 = sk->sketch.AddPoint(ids, pc->start), p1 = sk->sketch.AddPoint(ids, pc->end);
        sk->sketch.FindPoint(p0)->projected = true;
        sk->sketch.FindPoint(p1)->projected = true;
        curve = sk->sketch.AddLine(ids, p0, p1);
        sk->sketch.FindLine(curve)->projected = true;
    }
    link.curve = curve;
    sk->contextProjections.push_back(link);
    part.doc.MarkChanged(0);
    return curve;
}

} // namespace

TEST_CASE("a part edited in memory is shown from memory, then from its file again") {
    Bench b;
    MemoryPart edited{Plate(), {}};
    const std::filesystem::path plateFile = b.dir / "plate.bzc";
    edited.Rebuild();
    const double before = b.m.Shown().Volume();
    b.m.UsePartInMemory(plateFile, &edited.doc, &edited.model);
    CHECK(b.m.IsPartInMemory(plateFile));
    // The edit makes the plate longer: the assembly shows it at once, the file is unchanged.
    Reshape(edited.doc, 60, 20);
    edited.Rebuild();
    b.m.Update(b.a, b.file);
    CHECK(b.m.Shown().Volume() == Approx(60 * 40 * 10 - kPi * 16 * 10).epsilon(1e-3));
    CHECK(b.m.PartOf(b.plate) == &edited.doc);
    // Nothing changed: nothing is taken again.
    const auto generation = b.m.PartGenerationOf(b.plate);
    b.m.Update(b.a, b.file);
    CHECK(b.m.PartGenerationOf(b.plate) == generation);
    // Forgotten (the edit was not kept): the file as it is.
    b.m.ForgetPartInMemory(plateFile);
    b.m.Update(b.a, b.file);
    CHECK_FALSE(b.m.IsPartInMemory(plateFile));
    CHECK(b.m.Shown().Volume() == Approx(before).epsilon(1e-6));
}

TEST_CASE("components that did not move keep the very shape they are shown with") {
    // The view keeps a body's presentation only for exactly the same shape:
    // a change elsewhere must not make every body of the assembly new.
    Bench b;
    std::vector<ObjectId> ids;
    for (int i = 0; i < 5; ++i) ids.push_back(b.a.AddComponent("plate.bzc", "Плита", {Rotation{}, {i * 100.0, 0, 0}}).id);
    b.m.Update(b.a, b.file);
    const auto shapes = [&] {
        std::map<std::uint64_t, TopoDS_Shape> out;
        for (const kernel::Body& body : b.m.Shown().GetImpl().bodies) out[body.id.value] = body.shape;
        return out;
    };
    const auto before = shapes();
    REQUIRE(before.size() == 6);
    b.a.FindComponent(ids[2])->placement.translation.z += 5;   // one moves
    b.a.Touch();
    b.m.Update(b.a, b.file);
    const auto after = shapes();
    for (const auto& [id, shape] : after) {
        CAPTURE(id);
        if (id == ids[2].value) CHECK_FALSE(shape.IsEqual(before.at(id)));
        else CHECK(shape.IsEqual(before.at(id)));
    }
}

TEST_CASE("constraint geometry: vertices and edges near the cursor, origin and work planes of a part") {
    Bench b;
    // Near a corner: the vertex; near an edge: the line; elsewhere on the face: the face.
    auto v = b.m.GeometryAt(b.a, b.plate, {40.1, 39.9, 10}, kernel::AssemblyPick::Any, 0.5);
    REQUIRE(v);
    CHECK(v->kind == GeometryKind::Point);
    CHECK(v->source == PickSource::Vertex);
    CHECK((v->origin - Vec3{40, 40, 10}).Length() == Approx(0).epsilon(1e-9));
    auto e = b.m.GeometryAt(b.a, b.plate, {10, 0.2, 10}, kernel::AssemblyPick::Any, 0.5);
    REQUIRE(e);
    CHECK(e->kind == GeometryKind::Axis);
    CHECK(e->source == PickSource::Edge);
    CHECK(std::abs(e->direction.x) == Approx(1));
    auto f = b.m.GeometryAt(b.a, b.plate, {10, 10, 10}, kernel::AssemblyPick::Any, 0.5);
    REQUIRE(f);
    CHECK(f->kind == GeometryKind::Plane);
    // The hole's rim as an edge: a circle.
    auto rim = b.m.GeometryAt(b.a, b.plate, {24.1, 20, 10}, kernel::AssemblyPick::Any, 0.5);
    REQUIRE(rim);
    CHECK(rim->kind == GeometryKind::Circle);
    CHECK(rim->radius == Approx(4));
    // Line: a straight edge, or the axis of the round face.
    auto line = b.m.GeometryAt(b.a, b.plate, {20, 24, 5}, kernel::AssemblyPick::Line, 0.5);
    REQUIRE(line);
    CHECK(line->kind == GeometryKind::Axis);
    CHECK(std::abs(line->direction.z) == Approx(1));

    // Named elements: the origin of a part, a work plane, the assembly's origin.
    auto xz = b.m.ElementGeometry(b.plate, doc::origin::kPlaneXZ);
    REQUIRE(xz);
    CHECK(xz->direction.y == Approx(-1));
    CHECK(b.m.ElementGeometry(kAssemblyOrigin, doc::origin::kAxisZ)->kind == GeometryKind::Axis);
    CHECK_FALSE(b.m.ElementGeometry(b.plate, ObjectId{999}));
    const auto elements = b.m.ElementsOf(b.plate);
    CHECK(elements.size() == 7);   // only the origin: the plate has no work geometry
}

TEST_CASE("edge, vertex and named geometry are found again after the part changes") {
    Bench b;
    MemoryPart edited{Plate(), {}};
    edited.Rebuild();
    // A work plane at mid-height of the plate, offset from XY.
    {
        auto wp = std::make_unique<doc::WorkPlaneFeature>();
        wp->id = edited.doc.Ids().Next();
        wp->name = "Плоскость1";
        wp->method = doc::WorkPlaneMethod::Offset;
        wp->base = doc::origin::kPlaneXY;
        wp->offset.name = edited.doc.NextParameterName();
        wp->offset.expression = "5";
        edited.doc.Insert(std::move(wp), edited.doc.FeatureCount());
        edited.Rebuild();
    }
    b.m.UsePartInMemory(b.dir / "plate.bzc", &edited.doc, &edited.model);
    b.m.Update(b.a, b.file);
    const auto elements = b.m.ElementsOf(b.plate);
    REQUIRE(elements.size() == 8);
    CHECK(elements.back().name == "Плоскость1");
    GeometryRef plane = *b.m.ElementGeometry(b.plate, elements.back().id);
    CHECK(plane.origin.z == Approx(5));

    GeometryRef corner = *b.m.GeometryAt(b.a, b.plate, {40, 40, 10}, kernel::AssemblyPick::Any, 0.5);
    GeometryRef edge = *b.m.GeometryAt(b.a, b.plate, {20, 0, 10}, kernel::AssemblyPick::Any, 0.5);
    REQUIRE(corner.source == PickSource::Vertex);
    REQUIRE(edge.source == PickSource::Edge);
    corner.component = edge.component = plane.component = b.plate;

    // The plate grows to 60; the work plane goes to 7.
    Reshape(edited.doc, 60, 20);
    dynamic_cast<doc::WorkPlaneFeature*>(edited.doc.Find(elements.back().id))->offset.expression = "7";
    edited.doc.MarkChanged(0);
    edited.Rebuild();
    b.m.Update(b.a, b.file);
    REQUIRE(b.m.Refresh(b.a, corner));
    CHECK(corner.origin.x == Approx(60).epsilon(1e-6));   // the same corner, moved
    REQUIRE(b.m.Refresh(b.a, edge));
    CHECK(std::abs(edge.direction.x) == Approx(1));
    CHECK(edge.origin.y == Approx(0).epsilon(1e-6));
    REQUIRE(b.m.Refresh(b.a, plane));
    CHECK(plane.origin.z == Approx(7));
}

TEST_CASE("a part projected from its neighbour follows it: the plate changes, the cover's pin moves") {
    Bench b;
    // The cover sits on the plate (its XY on the plate's top) and has a pin
    // drawn on the plate's hole — projected, linked — extruded up from it.
    MemoryPart cover;
    test::AddSketch(cover.doc);
    const ObjectId coverId = b.AddMemoryPart(cover, "Крышка", {Rotation{}, {0, 0, 10}});
    const ObjectId curve = ProjectInto(b, cover, coverId, b.plate, {24, 20, 10});
    test::AddExtrude(cover.doc, cover.doc.Features()[0]->id, "8");
    cover.Rebuild();
    b.m.Update(b.a, b.file);
    REQUIRE(cover.model.BoundingBox());
    CHECK((cover.model.BoundingBox()->min.x + cover.model.BoundingBox()->max.x) / 2 == Approx(20).epsilon(1e-6));

    const std::string name = "Узел";
    REQUIRE(kernel::HasContextProjections(cover.doc, name));
    CHECK_FALSE(kernel::HasContextProjections(cover.doc, "Другая сборка"));
    std::uint64_t key = kernel::ContextKey(b.m, b.a, coverId, cover.doc, name);
    // Nothing changed: the same key, and adapting moves nothing.
    CHECK(kernel::ContextKey(b.m, b.a, coverId, cover.doc, name) == key);
    CHECK(kernel::AdaptContextProjections(b.m, b.a, coverId, cover.doc, name).moved == 0);

    // The plate is edited: its hole goes to x = 30.
    MemoryPart plate{Plate(), {}};
    plate.Rebuild();
    b.m.UsePartInMemory(b.dir / "plate.bzc", &plate.doc, &plate.model);
    Reshape(plate.doc, 40, 30);
    plate.Rebuild();
    b.m.Update(b.a, b.file);
    const std::uint64_t key2 = kernel::ContextKey(b.m, b.a, coverId, cover.doc, name);
    CHECK(key2 != key);
    const kernel::AdaptResult r = kernel::AdaptContextProjections(b.m, b.a, coverId, cover.doc, name);
    CHECK(r.moved == 1);
    CHECK(r.lost == 0);
    auto* sk = dynamic_cast<doc::SketchFeature*>(cover.doc.Features()[0].get());
    const sketch::Circle* c = sk->sketch.FindCircle(curve);
    REQUIRE(c);
    CHECK(sk->sketch.PositionOf(c->center).x == Approx(30).epsilon(1e-6));
    cover.Rebuild();
    CHECK((cover.model.BoundingBox()->min.x + cover.model.BoundingBox()->max.x) / 2 == Approx(30).epsilon(1e-6));
    CHECK(cover.model.Volume() == Approx(kPi * 16 * 8).epsilon(1e-3));

    // The plate moves (unfixed, shifted 5 mm along Y): the cover is fixed — its pin follows the hole.
    b.a.FindComponent(b.plate)->grounded = false;
    b.a.FindComponent(coverId)->grounded = true;
    b.a.FindComponent(b.plate)->placement.translation.y = 5;
    b.a.Touch();
    b.m.Update(b.a, b.file);
    CHECK(kernel::ContextKey(b.m, b.a, coverId, cover.doc, name) != key2);
    CHECK(kernel::AdaptContextProjections(b.m, b.a, coverId, cover.doc, name).moved == 1);
    CHECK(sk->sketch.PositionOf(c->center).y == Approx(25).epsilon(1e-6));

    // Not adaptive any more: it stays where it is.
    sk->contextProjections[0].adaptive = false;
    b.a.FindComponent(b.plate)->placement.translation.y = 0;
    b.a.Touch();
    b.m.Update(b.a, b.file);
    CHECK_FALSE(kernel::HasContextProjections(cover.doc, name));
    CHECK(kernel::AdaptContextProjections(b.m, b.a, coverId, cover.doc, name).moved == 0);
    CHECK(sk->sketch.PositionOf(c->center).y == Approx(25).epsilon(1e-6));
}

TEST_CASE("a lost source edge keeps its curve where it was; a deleted source component too") {
    Bench b;
    MemoryPart cover;
    test::AddSketch(cover.doc);
    const ObjectId coverId = b.AddMemoryPart(cover, "Крышка", {Rotation{}, {0, 0, 10}});
    const ObjectId curve = ProjectInto(b, cover, coverId, b.plate, {24, 20, 10});
    cover.Rebuild();
    // The hole is gone from the plate.
    MemoryPart plate{Plate(), {}};
    auto* psk = dynamic_cast<doc::SketchFeature*>(plate.doc.Features()[0].get());
    psk->sketch.circles.clear();
    plate.doc.MarkChanged(0);
    plate.Rebuild();
    b.m.UsePartInMemory(b.dir / "plate.bzc", &plate.doc, &plate.model);
    b.m.Update(b.a, b.file);
    const kernel::AdaptResult r = kernel::AdaptContextProjections(b.m, b.a, coverId, cover.doc, "Узел");
    CHECK(r.moved == 0);
    CHECK(r.lost == 1);
    auto* sk = dynamic_cast<doc::SketchFeature*>(cover.doc.Features()[0].get());
    CHECK(sk->sketch.PositionOf(sk->sketch.FindCircle(curve)->center).x == Approx(20).epsilon(1e-6));

    b.a.RemoveComponent(b.plate);
    b.m.Update(b.a, b.file);
    CHECK(kernel::AdaptContextProjections(b.m, b.a, coverId, cover.doc, "Узел").lost == 1);
}

TEST_CASE("projections that would make parts follow each other round in a circle are refused") {
    Bench b;
    MemoryPart cover, lid;
    test::AddSketch(cover.doc);
    test::AddSketch(lid.doc);
    const ObjectId coverId = b.AddMemoryPart(cover, "Крышка", {Rotation{}, {0, 0, 10}});
    const ObjectId lidId = b.AddMemoryPart(lid, "Накладка", {Rotation{}, {0, 0, 20}});
    ProjectInto(b, cover, coverId, b.plate, {24, 20, 10});   // the cover follows the plate
    cover.Rebuild();
    b.m.Update(b.a, b.file);
    // The plate cannot follow the cover now, nor itself.
    CHECK(kernel::WouldCycle(b.m, b.a, coverId, b.plate, "Узел"));
    CHECK(kernel::WouldCycle(b.m, b.a, b.plate, b.plate, "Узел"));
    // The lid may follow the cover (a chain), the cover may follow the plate again.
    CHECK_FALSE(kernel::WouldCycle(b.m, b.a, coverId, lidId, "Узел"));
    CHECK_FALSE(kernel::WouldCycle(b.m, b.a, b.plate, coverId, "Узел"));
    // A chain: lid ← cover ← plate. The plate cannot follow the lid.
    test::AddExtrude(cover.doc, cover.doc.Features()[0]->id, "5");   // the cover gets a body (the projected pin)
    cover.Rebuild();
    b.m.Update(b.a, b.file);
    ProjectInto(b, lid, lidId, coverId, {24, 20, 15});
    lid.Rebuild();
    b.m.Update(b.a, b.file);
    CHECK(kernel::WouldCycle(b.m, b.a, lidId, b.plate, "Узел"));
    CHECK(kernel::WouldCycle(b.m, b.a, lidId, coverId, "Узел"));
}

TEST_CASE("the surroundings of a part edited in place, in its coordinates") {
    Bench b;
    MemoryPart cover;
    test::AddSketch(cover.doc);
    const ObjectId coverId = b.AddMemoryPart(cover, "Крышка", {Rotation::AxisAngle({0, 0, 1}, kPi / 2), {100, 0, 10}});
    const kernel::Model around = b.m.ContextOf(b.a, coverId);
    REQUIRE(around.Bodies().size() == 1);   // the plate, not the cover itself
    CHECK(around.Bodies()[0].id == b.plate);
    const auto box = around.BoundingBox();
    REQUIRE(box);
    // The plate (0..40, 0..40, 0..10 in the assembly) seen from the cover
    // turned a quarter round at (100, 0, 10): x' = y, y' = 100 - x, z' = z - 10.
    CHECK(box->min.x == Approx(0).epsilon(1e-3));
    CHECK(box->max.x == Approx(40).epsilon(1e-3));
    CHECK(box->min.y == Approx(60).epsilon(1e-3));
    CHECK(box->max.y == Approx(100).epsilon(1e-3));
    CHECK(box->min.z == Approx(-10).epsilon(1e-3));
    CHECK(box->max.z == Approx(0).epsilon(1e-3));
}

TEST_CASE("adaptivity costs little: 40 parts follow a plate; an unchanged assembly costs next to nothing") {
    Bench b;
    std::vector<std::unique_ptr<MemoryPart>> covers;
    std::vector<ObjectId> ids;
    for (int i = 0; i < 40; ++i) {
        auto part = std::make_unique<MemoryPart>();
        test::AddSketch(part->doc);
        const ObjectId id = b.AddMemoryPart(*part, "Крышка" + std::to_string(i), {Rotation{}, {0, 0, 10.0 + i}});
        ProjectInto(b, *part, id, b.plate, {24, 20, 10});
        test::AddExtrude(part->doc, part->doc.Features()[0]->id, "1");
        part->Rebuild();
        ids.push_back(id);
        covers.push_back(std::move(part));
    }
    b.m.Update(b.a, b.file);
    std::vector<std::uint64_t> keys;
    for (std::size_t i = 0; i < covers.size(); ++i) keys.push_back(kernel::ContextKey(b.m, b.a, ids[i], covers[i]->doc, "Узел"));

    // Nothing changed: checking all of them is cheap (no geometry touched).
    auto t0 = std::chrono::steady_clock::now();
    int same = 0;
    for (int pass = 0; pass < 10; ++pass)
        for (std::size_t i = 0; i < covers.size(); ++i)
            same += kernel::ContextKey(b.m, b.a, ids[i], covers[i]->doc, "Узел") == keys[i] ? 1 : 0;
    const double idle = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 10;
    MESSAGE("40 adaptive parts, nothing changed: ", idle, " ms per check of all");
    CHECK(same == 400);
    CHECK(idle < 20);

    // The hole moves: all 40 follow and rebuild.
    MemoryPart plate{Plate(), {}};
    plate.Rebuild();
    b.m.UsePartInMemory(b.dir / "plate.bzc", &plate.doc, &plate.model);
    Reshape(plate.doc, 40, 28);
    plate.Rebuild();
    b.m.Update(b.a, b.file);
    t0 = std::chrono::steady_clock::now();
    int moved = 0;
    for (std::size_t i = 0; i < covers.size(); ++i) {
        CHECK(kernel::ContextKey(b.m, b.a, ids[i], covers[i]->doc, "Узел") != keys[i]);
        moved += kernel::AdaptContextProjections(b.m, b.a, ids[i], covers[i]->doc, "Узел").moved;
    }
    const double adapt = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    t0 = std::chrono::steady_clock::now();
    for (auto& c : covers) c->Rebuild();
    const double rebuild = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    MESSAGE("40 adaptive parts followed in ", adapt, " ms, rebuilt in ", rebuild, " ms");
    CHECK(moved == 40);
    for (auto& c : covers) CHECK((c->model.BoundingBox()->min.x + c->model.BoundingBox()->max.x) / 2 == Approx(28).epsilon(1e-6));
}

TEST_CASE("context projections survive saving the part") {
    doc::Document d;
    auto& sk = test::AddSketch(d);
    const ObjectId p = sk.sketch.AddPoint(d.Ids(), {1, 2});
    const ObjectId c = sk.sketch.AddCircle(d.Ids(), p, 3);
    doc::EdgeReference e;
    e.curve = doc::CurveKind::Circle;
    e.radius = 3;
    e.middle = {4, 2, 0};
    sk.contextProjections.push_back({"Узел", ObjectId{7}, e, c, true});
    sk.contextProjections.push_back({"Узел 2", ObjectId{9}, e, c, false});
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "bz_context_projection.bzc";
    REQUIRE(doc::SaveBzc(d, file, "tests"));
    Expected<doc::Document> back = doc::LoadBzc(file);
    std::filesystem::remove(file);
    REQUIRE(back);
    const auto* sk2 = dynamic_cast<const doc::SketchFeature*>(back->Features()[0].get());
    REQUIRE(sk2);
    REQUIRE(sk2->contextProjections.size() == 2);
    CHECK(sk2->contextProjections[0].assembly == "Узел");
    CHECK(sk2->contextProjections[0].component == ObjectId{7});
    CHECK(sk2->contextProjections[0].edge.radius == Approx(3));
    CHECK(sk2->contextProjections[0].curve == c);
    CHECK(sk2->contextProjections[1].adaptive == false);
    // The curve deleted: its link goes too.
    auto* own = dynamic_cast<doc::SketchFeature*>(back->Features()[0].get());
    own->sketch.circles.clear();
    own->PruneProjections();
    CHECK(own->contextProjections.empty());
}
