#include "kernel/Model.h"

#include "core/Log.h"
#include "document/CoilFeature.h"
#include "document/SectionFeature.h"
#include "document/EdgeFeature.h"
#include "document/HoleFeature.h"
#include "document/ExtrudeFeature.h"
#include "document/Recompute.h"
#include "document/RevolveFeature.h"
#include "document/LoftFeature.h"
#include "document/Origin.h"
#include "document/PatternFeature.h"
#include "document/SketchFeature.h"
#include "document/SweepFeature.h"
#include "document/ThreadFeature.h"
#include "document/WorkAxisFeature.h"
#include "document/WorkPlaneFeature.h"
#include "kernel/occt/Coil.h"
#include "kernel/occt/Section.h"
#include "kernel/occt/EdgeMatch.h"
#include "kernel/occt/Extrude.h"
#include "kernel/occt/FaceMatch.h"
#include "kernel/occt/Fillet.h"
#include "kernel/occt/Hole.h"
#include "kernel/occt/Loft.h"
#include "kernel/occt/ModelImpl.h"
#include "kernel/occt/Project.h"
#include "kernel/occt/Revolve.h"
#include "kernel/occt/Sweep.h"
#include "kernel/occt/Thread.h"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Message.hxx>
#include <Standard_Failure.hxx>
#include <Message_Messenger.hxx>
#include <Message_Printer.hxx>
#include <format>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace bz::kernel {
namespace {

using doc::FeatureState;

/// Length that surely crosses every body, for "through all".
double ThroughAllLength(const std::vector<Body>& bodies, const Frame& frame) {
    Bnd_Box box;
    for (const Body& b : bodies) BRepBndLib::Add(b.shape, box);
    if (box.IsVoid()) return 1000.0;
    const double diagonal = std::sqrt(box.SquareExtent());
    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const Vec3 center{(xmin + xmax) / 2, (ymin + ymax) / 2, (zmin + zmax) / 2};
    return 2.0 * (diagonal + (center - frame.origin).Length()) + 1.0;
}

std::string NextBodyName(const std::vector<Body>& bodies) {
    int largest = 0;
    for (const Body& b : bodies) {
        const std::string prefix = "Тело";
        if (b.name.rfind(prefix, 0) == 0) largest = std::max(largest, std::atoi(b.name.c_str() + prefix.size()));
    }
    return std::format("Тело{}", largest + 1);
}

/// The last step of every operation that makes a solid: keep it as a new
/// body, or join, cut or intersect the most recent body with it. The solid
/// is also kept as the operation's tool for patterns and mirrors.
void CombineSolid(ObjectId id, doc::BooleanOp op, const TopoDS_Shape& tool, Model::Impl& impl, std::vector<Body>& bodies,
                  doc::FeatureStatus& status) {
    // A new body copied by a pattern joins the body it started.
    if (op != doc::BooleanOp::Intersect) impl.tools[id] = {tool, op == doc::BooleanOp::Cut ? doc::BooleanOp::Cut : doc::BooleanOp::Join};

    if (op == doc::BooleanOp::NewBody || bodies.empty()) {
        if (op == doc::BooleanOp::Cut || op == doc::BooleanOp::Intersect) {
            status = {FeatureState::Error, "Нет тела, к которому применить операцию"};
            return;
        }
        bodies.push_back({id, NextBodyName(bodies), tool});
        status = {FeatureState::Ok, {}};
        return;
    }
    // Join, Cut and Intersect act on the most recent body.
    Body& target = bodies.back();
    Expected<TopoDS_Shape> result = occt::ApplyBoolean(target.shape, tool, op);
    if (!result) {
        status = {FeatureState::Error, result.GetError().message};
        return;
    }
    target.shape = *result;
    status = {FeatureState::Ok, {}};
}

/// A loft through the sections' outlines, then combined like any solid.
void ApplyLoft(doc::LoftFeature& f, Model::Impl& impl, std::vector<Body>& bodies, const doc::Document& doc) {
    if (f.sections.size() < 2) {
        f.status = {FeatureState::Error, "Выберите хотя бы два эскиза-сечения"};
        return;
    }
    std::vector<TopoDS_Wire> outlines;
    for (ObjectId id : f.sections) {
        const auto* sketch = doc.FindAs<doc::SketchFeature>(id);
        const auto profile = impl.sketches.find(id);
        if (!sketch || profile == impl.sketches.end() || sketch->status.state == FeatureState::Error) {
            f.status = {FeatureState::Error, "Сечение лофта не найдено или содержит ошибки"};
            return;
        }
        const std::vector<TopoDS_Face> faces = occt::SelectFaces(profile->second, {});
        if (faces.size() != 1) {
            f.status = {FeatureState::Error, std::format("В «{}» должен быть ровно один замкнутый контур", sketch->name)};
            return;
        }
        outlines.push_back(BRepTools::OuterWire(faces.front()));
    }
    Expected<TopoDS_Shape> solid = occt::MakeLoft(outlines, f.ruled);
    if (!solid) {
        f.status = {FeatureState::Error, solid.GetError().message};
        return;
    }
    CombineSolid(f.id, f.operation, *solid, impl, bodies, f.status);
}

/// Common path of every sketch-to-solid operation: check the sketch, build
/// the tool solid from the chosen regions with `makeTool`, then add it as a
/// new body or combine it with the most recent body.
template <class MakeTool>
void ApplySketchSolid(const doc::SketchSolidFeature& f, Model::Impl& impl, std::vector<Body>& bodies,
                      doc::FeatureStatus& status, const doc::Document& doc, MakeTool makeTool) {
    const auto* sketch = doc.FindAs<doc::SketchFeature>(f.sketch);
    const auto profile = impl.sketches.find(f.sketch);
    if (!sketch || profile == impl.sketches.end()) {
        status = {FeatureState::Error, "Эскиз операции не найден"};
        return;
    }
    if (sketch->status.state == FeatureState::Error || sketch->status.state == FeatureState::NotComputed) {
        status = {FeatureState::Error, "Эскиз операции содержит ошибки"};
        return;
    }
    for (const Parameter* p : f.Parameters())
        if (p->HasError()) {
            status = {FeatureState::Error, p->error};
            return;
        }

    const std::vector<TopoDS_Face> faces = occt::SelectFaces(profile->second, f.profiles);
    if (faces.empty() && !f.profiles.automatic && !profile->second.faces.empty()) {
        status = {FeatureState::Error, "Щёлкните замкнутый контур эскиза, который использовать"};
        return;
    }
    Expected<TopoDS_Shape> tool = makeTool(faces, *sketch, profile->second.frame);
    if (!tool) {
        status = {FeatureState::Error, tool.GetError().message};
        return;
    }
    CombineSolid(f.id, f.operation, *tool, impl, bodies, status);
}

/// Fillet and chamfer: find the referenced edges on the most recent body
/// and modify it. Edges that no longer exist are reported, not fatal while
/// at least one is still there.
template <class MakeShape>
void ApplyEdgeFeature(const doc::EdgeFeature& f, std::vector<Body>& bodies, doc::FeatureStatus& status, MakeShape make) {
    if (bodies.empty()) {
        status = {FeatureState::Error, "Нет тела, к которому применить операцию"};
        return;
    }
    for (const Parameter* p : f.Parameters())
        if (p->HasError()) {
            status = {FeatureState::Error, p->error};
            return;
        }
    Body& target = bodies.back();
    std::vector<TopoDS_Edge> edges;
    std::size_t lost = 0;
    for (const auto& edge : occt::ResolveEdges(f.edges, target.shape)) {
        if (edge) edges.push_back(*edge);
        else ++lost;
    }
    if (edges.empty() && !f.edges.empty()) {
        status = {FeatureState::Error, "Рёбра операции не найдены: модель изменилась слишком сильно"};
        return;
    }
    Expected<TopoDS_Shape> result = make(target.shape, edges);
    if (!result) {
        status = {FeatureState::Error, result.GetError().message};
        return;
    }
    target.shape = *result;
    status = lost == 0 ? doc::FeatureStatus{FeatureState::Ok, {}}
                       : doc::FeatureStatus{FeatureState::Warning, std::format("Не найдено рёбер: {}", lost)};
}

/// Holes are cut from the most recent body.
/// Operations that change the most recent body (hole, shell): `make` turns
/// its shape into the new one.
template <class Make>
void ApplyToLastBody(std::vector<Body>& bodies, doc::FeatureStatus& status, const char* noBody, Make&& make) {
    if (bodies.empty()) {
        status = {FeatureState::Error, noBody};
        return;
    }
    Expected<TopoDS_Shape> result = make(bodies.back().shape);
    if (!result) {
        status = {FeatureState::Error, result.GetError().message};
        return;
    }
    bodies.back().shape = *result;
    status = {FeatureState::Ok, {}};
}

/// A thread is cut from the body that has its face (the most recent one first).
void ApplyThread(doc::ThreadFeature& thread, Model::Impl& impl, std::vector<Body>& bodies) {
    thread.site.reset();
    if (bodies.empty()) {
        thread.status = {FeatureState::Error, "Нет тела, на котором делать резьбу"};
        return;
    }
    Body* target = &bodies.back();
    if (thread.face)
        for (auto it = bodies.rbegin(); it != bodies.rend(); ++it)
            if (const auto site = occt::FindThreadSite(it->shape, *thread.face)) {
                thread.site = doc::ThreadFeature::Site{site->external, site->nominal, site->length};
                target = &*it;
                break;
            }
    const Expected<TopoDS_Shape> tool = occt::ThreadTool(target->shape, thread);
    Expected<TopoDS_Shape> result = tool ? occt::ApplyBoolean(target->shape, *tool, doc::BooleanOp::Cut) : tool;
    if (!result) {
        thread.status = {FeatureState::Error, result.GetError().message};
        return;
    }
    impl.tools[thread.id] = {*tool, doc::BooleanOp::Cut};
    target->shape = *result;
    thread.status = {FeatureState::Ok, {}};
}

/// Patterns and mirrors: the tools of the source operations, moved by each
/// of `placements`, joined to or cut from the most recent body.
void ApplyCopies(const doc::CopyFeature& f, const std::vector<gp_Trsf>& placements, Model::Impl& impl,
                 std::vector<Body>& bodies, doc::FeatureStatus& status, const doc::Document& doc) {
    if (bodies.empty()) {
        status = {FeatureState::Error, "Нет тела, к которому применить копии"};
        return;
    }
    if (f.sources.empty()) {
        status = {FeatureState::Error, "Выберите операции, которые нужно скопировать"};
        return;
    }
    std::vector<TopoDS_Shape> joins, cuts;
    Model::Impl::CopyPreview& preview = impl.copyPreviews[f.id];
    preview = {};
    for (ObjectId source : f.sources) {
        const auto tool = impl.tools.find(source);
        if (tool == impl.tools.end()) {
            const doc::Feature* sf = doc.Find(source);
            status = {FeatureState::Error, std::format("«{}» нельзя скопировать: копируются только выдавливание, вращение и отверстия без ошибок",
                                                       sf ? sf->name : std::string("?"))};
            return;
        }
        preview.sources.push_back(tool->second.shape);
        for (const gp_Trsf& t : placements) {
            const TopoDS_Shape copy = BRepBuilderAPI_Transform(tool->second.shape, t, true).Shape();
            (tool->second.op == doc::BooleanOp::Cut ? cuts : joins).push_back(copy);
            preview.copies.push_back(copy);
        }
    }
    Body& target = bodies.back();
    for (auto [tools, op] : {std::pair{&joins, doc::BooleanOp::Join}, {&cuts, doc::BooleanOp::Cut}}) {
        Expected<TopoDS_Shape> result = occt::ApplyBoolean(target.shape, *tools, op);
        if (!result) {
            status = {FeatureState::Error, result.GetError().message};
            return;
        }
        target.shape = *result;
    }
    status = {FeatureState::Ok, {}};
}

/// Placements of a pattern's copies.
Expected<std::vector<gp_Trsf>> PatternPlacements(const doc::PatternFeature& f, const doc::Document& doc) {
    const auto copies = f.Copies();
    if (!copies) return copies.GetError();
    Vec3 base{}, a = doc::AxisDirection(f.axis);
    if (f.workAxis) {
        const auto* wa = doc.FindAs<doc::WorkAxisFeature>(f.workAxis);
        if (!wa || !wa->line) return Error{"Рабочая ось массива не построена"};
        base = wa->line->first;
        a = wa->line->second;
    }
    const gp_Ax1 axis(gp_Pnt(base.x, base.y, base.z), gp_Dir(a.x, a.y, a.z));
    std::vector<gp_Trsf> placements;
    for (const doc::PatternFeature::Copy& c : *copies) {
        gp_Trsf turn, move;
        turn.SetRotation(axis, c.turn);
        move.SetTranslation(gp_Vec(c.offset.x, c.offset.y, c.offset.z));
        placements.push_back(move * turn);   // turn first, then move
    }
    return placements;
}

/// Frame of an origin plane or of a placed work plane; nullopt otherwise.
std::optional<Frame> PlaneOf(ObjectId id, const doc::Document& doc) {
    if (const auto origin = doc::origin::PlaneFrame(id)) return origin;
    if (const auto* wp = doc.FindAs<doc::WorkPlaneFeature>(id); wp && wp->frame) return wp->frame;
    return std::nullopt;
}

/// Puts a work axis where its edge, cylinder or two planes say.
void PlaceWorkAxis(doc::WorkAxisFeature& wa, const doc::Document& doc, const std::vector<Body>& bodies) {
    const auto fail = [&](const char* message) {
        wa.line.reset();
        wa.status = {FeatureState::Error, message};
    };
    switch (wa.method) {
        case doc::WorkAxisMethod::Edge: {
            if (!wa.edge) return fail("Щёлкните прямое ребро");
            std::optional<std::pair<Vec3, Vec3>> line;
            for (auto it = bodies.rbegin(); it != bodies.rend() && !line; ++it) line = occt::ResolveLine(*wa.edge, it->shape);
            if (!line) return fail("Ребро не найдено или оно не прямое");
            wa.line = line;
            break;
        }
        case doc::WorkAxisMethod::Cylinder: {
            if (!wa.cylinder) return fail("Щёлкните цилиндрическую грань");
            std::optional<doc::CylinderTouch> now;
            for (auto it = bodies.rbegin(); it != bodies.rend() && !now; ++it) now = occt::ResolveCylinder(it->shape, *wa.cylinder);
            if (!now) return fail("Цилиндрическая грань не найдена: модель изменилась слишком сильно");
            wa.line = std::make_pair(now->axisPoint, now->axisDir.Normalized());
            break;
        }
        case doc::WorkAxisMethod::Planes: {
            const std::optional<Frame> a = PlaneOf(wa.plane1, doc), b = PlaneOf(wa.plane2, doc);
            if (!a || !b) return fail("Выберите две плоскости");
            const Vec3 dir = a->normal.Cross(b->normal);
            const double len2 = dir.Dot(dir);
            if (len2 < 1e-12) return fail("Плоскости параллельны — линии пересечения нет");
            // The point of the line nearest the origin.
            const double d1 = a->normal.Dot(a->origin), d2 = b->normal.Dot(b->origin);
            const Vec3 p = (b->normal.Cross(dir) * d1 + dir.Cross(a->normal) * d2) * (1.0 / len2);
            wa.line = std::make_pair(p, dir.Normalized());
            break;
        }
    }
    wa.status = {FeatureState::Ok, {}};
}

/// Every body cut by the section plane; bodies wholly on the far side go.
void ApplySection(doc::SectionFeature& section, const doc::Document& doc, std::vector<Body>& bodies) {
    std::optional<Frame> plane;
    if (section.face.IsSet()) {
        for (auto it = bodies.rbegin(); it != bodies.rend() && !plane; ++it) plane = occt::ResolvePlaneFrame(it->shape, section.face);
        if (!plane) {
            section.status = {FeatureState::Error, "Грань сечения не найдена: модель изменилась слишком сильно"};
            return;
        }
    } else {
        plane = PlaneOf(section.plane, doc);
    }
    if (!plane) {
        section.status = {FeatureState::Error, "Выберите плоскость или плоскую грань сечения"};
        return;
    }
    if (bodies.empty()) {
        section.status = {FeatureState::Error, "Нет тела, которое рассечь"};
        return;
    }
    std::vector<Body> kept;
    for (Body& body : bodies) {
        const auto cut = occt::CutByPlane(body.shape, *plane, section.flipped);
        if (!cut) {
            section.status = {FeatureState::Error, cut.GetError().message};
            return;
        }
        if (*cut) {
            body.shape = **cut;
            kept.push_back(body);
        }
    }
    section.status = kept.empty() ? doc::FeatureStatus{FeatureState::Warning, "Плоскость отсекла всю модель — переверните сторону"}
                                  : doc::FeatureStatus{FeatureState::Ok, {}};
    bodies = std::move(kept);
}


/// The reflection of a mirror, about an origin plane or a work plane.
Expected<gp_Trsf> MirrorPlacement(const doc::MirrorFeature& f, const doc::Document& doc) {
    const std::optional<Frame> plane = PlaneOf(f.plane, doc);
    if (!plane) return Error{"Выберите плоскость зеркала"};
    gp_Trsf mirror;
    mirror.SetMirror(gp_Ax2(gp_Pnt(plane->origin.x, plane->origin.y, plane->origin.z),
                            gp_Dir(plane->normal.x, plane->normal.y, plane->normal.z)));
    return mirror;
}

/// Puts a work plane where its base (origin plane, work plane or face of a
/// body) and offset say.
void PlaceWorkPlane(doc::WorkPlaneFeature& wp, const doc::Document& doc, const std::vector<Body>& bodies) {
    using doc::WorkPlaneMethod;
    const auto fail = [&](const char* message) {
        wp.frame.reset();
        wp.status = {FeatureState::Error, message};
    };
    // A start: a flat face of a body, an origin plane or a work plane before.
    const auto start = [&](ObjectId base, const doc::FaceReference& face) -> std::optional<Frame> {
        if (face.IsSet()) {
            for (auto it = bodies.rbegin(); it != bodies.rend(); ++it)
                if (auto f = occt::ResolvePlaneFrame(it->shape, face)) return f;
            return std::nullopt;
        }
        if (const auto originPlane = doc::origin::PlaneFrame(base)) return originPlane;
        if (const auto* other = doc.FindAs<doc::WorkPlaneFeature>(base); other && other->frame) return other->frame;
        return std::nullopt;
    };
    if (wp.method == WorkPlaneMethod::Tangent) {
        if (!wp.cylinder) return fail("Щёлкните цилиндрическую грань");
        std::optional<doc::CylinderTouch> now;
        for (auto it = bodies.rbegin(); it != bodies.rend() && !now; ++it) now = occt::ResolveCylinder(it->shape, *wp.cylinder);
        if (!now) return fail("Цилиндрическая грань не найдена: модель изменилась слишком сильно");
        const Vec3 n = (now->radial * (wp.reversed ? -1.0 : 1.0)).Normalized();
        Frame f;
        f.origin = now->axisPoint + now->radial * now->radius;
        f.normal = n;
        f.xDir = now->axisDir.Normalized();
        wp.frame = f;
        wp.status = {FeatureState::Ok, {}};
        return;
    }
    if (wp.method == WorkPlaneMethod::ThreePoints) {
        if (wp.points.size() < 3) return fail("Выберите три вершины");
        // Each vertex found again: the end of its edge nearest to where it was.
        wp.pointEdges.resize(wp.points.size());
        for (std::size_t i = 0; i < wp.points.size(); ++i) {
            if (!wp.pointEdges[i]) continue;
            for (auto it = bodies.rbegin(); it != bodies.rend(); ++it)
                if (const auto at = occt::ResolveVertex(*wp.pointEdges[i], wp.points[i], it->shape)) {
                    wp.points[i] = *at;
                    break;
                }
        }
        const Vec3 a = wp.points[0], u = wp.points[1] - a, v = wp.points[2] - a;
        const Vec3 n = u.Cross(v);
        if (n.Length() < 1e-9 * std::max(1.0, u.Length() * v.Length())) return fail("Точки лежат на одной прямой — плоскость через них не одна");
        Frame f;
        f.origin = a;
        f.xDir = u.Normalized();
        f.normal = n.Normalized() * (wp.reversed ? -1.0 : 1.0);
        wp.frame = f;
        wp.status = {FeatureState::Ok, {}};
        return;
    }
    const std::optional<Frame> first = start(wp.base, wp.face);
    if (!first) {
        return fail(wp.face.IsSet() ? "Грань рабочей плоскости не найдена: модель изменилась слишком сильно"
                                    : "Не задана плоскость, от которой строить");
    }
    switch (wp.method) {
        case WorkPlaneMethod::Offset:
            if (wp.offset.HasError()) return fail("Смещение задано с ошибкой");
            wp.frame = doc::OffsetFrame(*first, wp.reversed ? -wp.offset.value : wp.offset.value);
            break;
        case WorkPlaneMethod::Midplane: {
            const std::optional<Frame> second = start(wp.base2, wp.face2);
            if (!second) return fail("Выберите вторую плоскость или грань");
            if (std::abs(std::abs(first->normal.Dot(second->normal)) - 1.0) > 1e-6)
                return fail("Плоскости не параллельны — середины между ними нет");
            const double gap = (second->origin - first->origin).Dot(first->normal);
            if (std::abs(gap) < 1e-9) return fail("Это одна и та же плоскость");
            wp.frame = doc::OffsetFrame(*first, gap / 2);
            break;
        }
        case WorkPlaneMethod::ThreePoints:
        case WorkPlaneMethod::Tangent: break;   // handled above
        case WorkPlaneMethod::Angle: {
            if (!wp.edge) return fail("Выберите прямое ребро — ось поворота плоскости");
            if (wp.angle.HasError()) return fail("Угол задан с ошибкой");
            std::optional<std::pair<Vec3, Vec3>> line;
            for (auto it = bodies.rbegin(); it != bodies.rend() && !line; ++it) line = occt::ResolveLine(*wp.edge, it->shape);
            if (!line) return fail("Ребро не найдено или оно не прямое");
            const auto& [point, axis] = *line;
            // The start's normal made square to the edge (unchanged when the
            // edge is parallel to the start), then turned about the edge.
            const Vec3 across = first->normal - axis * first->normal.Dot(axis);
            if (across.Length() < 1e-6) return fail("Ребро перпендикулярно плоскости, от которой строить — повернуть её вокруг него нельзя");
            const Vec3 n0 = across.Normalized();
            const double t = wp.reversed ? -wp.angle.value : wp.angle.value;
            Frame f;
            f.origin = point;
            f.normal = (n0 * std::cos(t) + axis.Cross(n0) * std::sin(t)).Normalized();
            f.xDir = axis;
            wp.frame = f;
            break;
        }
    }
    wp.status = {FeatureState::Ok, {}};
}

/// Puts a projected sketch curve where its edge projects now. False when the
/// curve is gone or the edge now projects as a different kind of curve.
bool PlaceProjected(sketch::Sketch& sk, ObjectId curve, const ProjectedCurve& pc) {
    const auto move = [&](ObjectId point, Vec2 to) {
        if (sketch::Point* p = sk.FindPoint(point)) p->pos = to;
    };
    switch (pc.kind) {
        case ProjectedCurve::Kind::Line:
            if (const sketch::Line* l = sk.FindLine(curve)) {
                move(l->start, pc.start);
                move(l->end, pc.end);
                return true;
            }
            return false;
        case ProjectedCurve::Kind::Circle:
            if (sketch::Circle* c = sk.FindCircle(curve)) {
                move(c->center, pc.centre);
                c->radius = pc.radius;
                return true;
            }
            return false;
        case ProjectedCurve::Kind::Arc:
            if (const sketch::Arc* a = sk.FindArc(curve)) {
                move(a->center, pc.centre);
                move(a->start, pc.start);
                move(a->end, pc.end);
                return true;
            }
            return false;
    }
    return false;
}

/// Projected model edges follow the model: each is found again on the most
/// recent body and its curve moved, then the sketch is solved again so what
/// is constrained to them follows too. Returns false when an edge is lost
/// (its curve then stays where it was).
bool UpdateProjections(doc::SketchFeature& sk, const std::vector<Body>& bodies) {
    if (sk.projections.empty()) return true;
    bool allFound = true;
    // All edges found in one pass: describing the body's edges is the costly part.
    std::vector<doc::EdgeReference> refs;
    for (const doc::Projection& p : sk.projections) refs.push_back(p.edge);
    const std::vector<std::optional<TopoDS_Edge>> edges =
        bodies.empty() ? std::vector<std::optional<TopoDS_Edge>>(refs.size()) : occt::ResolveEdges(refs, bodies.back().shape);
    for (std::size_t i = 0; i < sk.projections.size(); ++i) {
        const doc::Projection& p = sk.projections[i];
        const std::optional<TopoDS_Edge>& edge = edges[i];
        const Expected<ProjectedCurve> projected =
            edge ? occt::ProjectEdge(*edge, sk.placement.frame) : Expected<ProjectedCurve>(Error{"ребро не найдено"});
        allFound = projected && PlaceProjected(sk.sketch, p.curve, *projected) && allFound;
    }
    doc::SolveSketch(sk);
    return allFound;
}

/// A sketch on a work plane moves with it; a sketch on a face moves along
/// the face's normal when the face moved. Sketch coordinates stay the same.
/// Returns false when the face is gone (the sketch then stays where it was).
bool FollowSupport(doc::SketchFeature& sk, const doc::Document& doc, const std::vector<Body>& bodies) {
    doc::SketchPlacement& p = sk.placement;
    if (const auto* wp = doc.FindAs<doc::WorkPlaneFeature>(p.support); wp && wp->frame) {
        p.frame = *wp->frame;
        return true;
    }
    if (!p.onFace || !p.face.IsSet()) return true;
    for (auto it = bodies.rbegin(); it != bodies.rend(); ++it)
        if (const auto plane = occt::ResolvePlaneFrame(it->shape, p.face)) {
            const Vec3 n = p.frame.normal;
            const double shift = (plane->origin - p.frame.origin).Dot(n);
            p.frame.origin = p.frame.origin + n * shift;
            p.face.point = p.face.point + n * shift;   // keep the reference on the face as it is now
            return true;
        }
    return false;
}

/// OCCT prints progress reports (STEP statistics...) to stdout; keep only
/// warnings and errors, our own log covers the rest.
void QuietOcctMessages() {
    static std::once_flag once;
    std::call_once(once, [] {
        const Handle(Message_Messenger)& messenger = Message::DefaultMessenger();
        // Range-for works with both OCCT 7.x and 8.x sequence types.
        for (const auto& printer : messenger->Printers()) printer->SetTraceLevel(Message_Warning);
    });
}

} // namespace

Model::Model() : myImpl(std::make_unique<Impl>()) { QuietOcctMessages(); }
Model::~Model() = default;
Model::Model(Model&&) noexcept = default;
Model& Model::operator=(Model&&) noexcept = default;

void Model::Rebuild(doc::Document& doc) {
    Impl& impl = *myImpl;
    doc::RecomputeModel(doc);

    const std::size_t count = doc.FeatureCount();
    // Reuse cached results up to the first changed feature, as long as the
    // order of features has not changed before that point.
    std::size_t start = std::min(doc.DirtyFrom(), count);
    for (std::size_t i = 0; i < start; ++i) {
        if (i >= impl.cachedOrder.size() || impl.cachedOrder[i] != doc.Features()[i]->id) {
            start = i;
            break;
        }
    }
    impl.cachedOrder.resize(count);
    impl.bodiesAfter.resize(count);

    std::vector<Body> bodies = start > 0 ? impl.bodiesAfter[start - 1] : std::vector<Body>{};
    for (std::size_t i = start; i < count; ++i) {
        doc::Feature& f = *doc.Features()[i];
        impl.cachedOrder[i] = f.id;
        impl.tools.erase(f.id);   // set again below if the operation succeeds

        const bool skip = !doc.IsActive(i) || f.suppressed;
        // One failing operation must not take the program down: whatever OCCT
        // or our code throws becomes that operation's error, and the bodies
        // stay as they were before it.
        const std::vector<Body> before = skip ? std::vector<Body>{} : bodies;
        if (!skip) try {
            if (auto* sk = dynamic_cast<doc::SketchFeature*>(&f)) {
                const bool placed = FollowSupport(*sk, doc, bodies);
                const bool projectedFound = UpdateProjections(*sk, bodies);
                Expected<occt::SketchProfile> profile = occt::BuildProfile(*sk);
                if (profile) {
                    impl.sketches[sk->id] = std::move(*profile);
                } else {
                    impl.sketches.erase(sk->id);
                    sk->status = {FeatureState::Error, profile.GetError().message};
                }
                if (!placed && sk->status.state == FeatureState::Ok)
                    sk->status = {FeatureState::Warning, "Грань эскиза не найдена: эскиз остался на прежнем месте"};
                if (!projectedFound && sk->status.state == FeatureState::Ok)
                    sk->status = {FeatureState::Warning, "Часть проецированных рёбер не найдена: они остались на прежнем месте"};
            } else if (auto* ex = dynamic_cast<doc::ExtrudeFeature*>(&f)) {
                ApplySketchSolid(*ex, impl, bodies, ex->status, doc,
                                 [&](const std::vector<TopoDS_Face>& faces, const doc::SketchFeature&, const Frame& frame) {
                                     std::optional<Frame> target;
                                     if (ex->extent == doc::ExtentType::ToPlane) {
                                         if (ex->toPlane) target = PlaneOf(ex->toPlane, doc);
                                         else if (ex->toFace.IsSet())
                                             for (auto it = bodies.rbegin(); it != bodies.rend() && !target; ++it)
                                                 target = occt::ResolvePlaneFrame(it->shape, ex->toFace);
                                     }
                                     return occt::MakeExtrusion(faces, *ex, frame, ThroughAllLength(bodies, frame), target);
                                 });
            } else if (auto* rev = dynamic_cast<doc::RevolveFeature*>(&f)) {
                ApplySketchSolid(*rev, impl, bodies, rev->status, doc,
                                 [&](const std::vector<TopoDS_Face>& faces, const doc::SketchFeature& profile, const Frame& frame) {
                                     return occt::MakeRevolution(faces, *rev, profile.sketch, frame);
                                 });
            } else if (auto* fillet = dynamic_cast<doc::FilletFeature*>(&f)) {
                ApplyEdgeFeature(*fillet, bodies, fillet->status, [&](const TopoDS_Shape& body, const std::vector<TopoDS_Edge>& edges) {
                    return occt::MakeFillet(body, edges, fillet->radius.value);
                });
            } else if (auto* chamfer = dynamic_cast<doc::ChamferFeature*>(&f)) {
                ApplyEdgeFeature(*chamfer, bodies, chamfer->status, [&](const TopoDS_Shape& body, const std::vector<TopoDS_Edge>& edges) {
                    return occt::MakeChamfer(body, edges, chamfer->distance.value);
                });
            } else if (auto* hole = dynamic_cast<doc::HoleFeature*>(&f)) {
                ApplyToLastBody(bodies, hole->status, "Нет тела, в котором делать отверстие",
                                [&](const TopoDS_Shape& body) -> Expected<TopoDS_Shape> {
                                    // Keep the reference on the face as it is now, so the
                                    // panel draws the centres where the holes are.
                                    if (const auto plane = occt::ResolvePlaneFrame(body, hole->face)) {
                                        const Vec3 n = hole->face.normal;
                                        hole->face.point = hole->face.point + n * (plane->origin - hole->face.point).Dot(n);
                                    }
                                    const Expected<TopoDS_Shape> tools = occt::HoleTools(body, *hole);
                                    if (!tools) return tools.GetError();
                                    impl.tools[hole->id] = {*tools, doc::BooleanOp::Cut};
                                    return occt::ApplyBoolean(body, *tools, doc::BooleanOp::Cut);
                                });
            } else if (auto* thread = dynamic_cast<doc::ThreadFeature*>(&f)) {
                ApplyThread(*thread, impl, bodies);
            } else if (auto* pattern = dynamic_cast<doc::PatternFeature*>(&f)) {
                if (const auto placements = PatternPlacements(*pattern, doc))
                    ApplyCopies(*pattern, *placements, impl, bodies, pattern->status, doc);
                else
                    pattern->status = {FeatureState::Error, placements.GetError().message};
            } else if (auto* mirror = dynamic_cast<doc::MirrorFeature*>(&f)) {
                if (const auto placement = MirrorPlacement(*mirror, doc))
                    ApplyCopies(*mirror, {*placement}, impl, bodies, mirror->status, doc);
                else
                    mirror->status = {FeatureState::Error, placement.GetError().message};
            } else if (auto* sweep = dynamic_cast<doc::SweepFeature*>(&f)) {
                const auto* pathSketch = doc.FindAs<doc::SketchFeature>(sweep->path);
                const Expected<TopoDS_Wire> path =
                    pathSketch ? occt::BuildPath(*pathSketch) : Expected<TopoDS_Wire>(Error{"Выберите эскиз траектории"});
                if (!path)
                    sweep->status = {FeatureState::Error, path.GetError().message};
                else
                    ApplySketchSolid(*sweep, impl, bodies, sweep->status, doc,
                                     [&](const std::vector<TopoDS_Face>& faces, const doc::SketchFeature&, const Frame&) {
                                         return occt::MakeSweep(faces, *path);
                                     });
            } else if (auto* coil = dynamic_cast<doc::CoilFeature*>(&f)) {
                ApplySketchSolid(*coil, impl, bodies, coil->status, doc,
                                 [&](const std::vector<TopoDS_Face>& faces, const doc::SketchFeature& profile, const Frame& frame) {
                                     return occt::MakeCoil(faces, *coil, profile.sketch, frame);
                                 });
            } else if (auto* loft = dynamic_cast<doc::LoftFeature*>(&f)) {
                ApplyLoft(*loft, impl, bodies, doc);
            } else if (auto* wp = dynamic_cast<doc::WorkPlaneFeature*>(&f)) {
                PlaceWorkPlane(*wp, doc, bodies);
            } else if (auto* wa = dynamic_cast<doc::WorkAxisFeature*>(&f)) {
                PlaceWorkAxis(*wa, doc, bodies);
            } else if (auto* section = dynamic_cast<doc::SectionFeature*>(&f)) {
                ApplySection(*section, doc, bodies);
            } else if (f.status.state == FeatureState::NotComputed) {
                f.status = {FeatureState::Error, "Операция этого типа не поддерживается"};
            }
        } catch (const Standard_Failure& failure) {
            bodies = before;
            f.status = {FeatureState::Error, std::format("Ошибка геометрического ядра: {}", failure.GetMessageString())};
        } catch (const std::exception& e) {
            bodies = before;
            f.status = {FeatureState::Error, std::format("Внутренняя ошибка: {}", e.what())};
        }
        if (f.status.state == FeatureState::Error) log::Warn("{}: {}", f.name, f.status.message);
        impl.bodiesAfter[i] = bodies;
    }

    // The visible result is the state at the end-of-part marker.
    const std::size_t marker = std::min(doc.EndMarker(), count);
    impl.bodies = marker > 0 ? impl.bodiesAfter[marker - 1] : std::vector<Body>{};
    impl.infos.clear();
    for (const Body& b : impl.bodies) impl.infos.push_back({b.id, b.name});

    doc.ClearDirty();
    ++impl.revision;
}

std::uint64_t Model::Revision() const { return myImpl->revision; }

const std::vector<BodyInfo>& Model::Bodies() const { return myImpl->infos; }

std::optional<Bounds> Model::BoundingBox() const {
    Bnd_Box box;
    for (const Body& b : myImpl->bodies) BRepBndLib::Add(b.shape, box);
    if (box.IsVoid()) return std::nullopt;
    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    return Bounds{{xmin, ymin, zmin}, {xmax, ymax, zmax}};
}

const std::vector<SketchRegion>* Model::Regions(ObjectId sketchFeature) const {
    const auto it = myImpl->sketches.find(sketchFeature);
    return it != myImpl->sketches.end() ? &it->second.regions : nullptr;
}

Expected<ProjectedCurve> Model::ProjectEdge(const doc::EdgeReference& edge, const Frame& plane, std::size_t featureIndex) const {
    const auto& after = myImpl->bodiesAfter;
    if (featureIndex == 0 || featureIndex > after.size() || after[featureIndex - 1].empty()) return Error{"Нет тела, с которого проецировать"};
    const std::optional<TopoDS_Edge> found = occt::ResolveEdges({edge}, after[featureIndex - 1].back().shape).front();
    if (!found) return Error{"Ребро не найдено"};
    return occt::ProjectEdge(*found, plane);
}

std::optional<MassProperties> Model::Properties() const {
    if (myImpl->bodies.empty()) return std::nullopt;
    GProp_GProps volume, surface;
    Bnd_Box box;
    for (const Body& b : myImpl->bodies) {
        // BRepGProp overwrites its output, so each body is measured alone
        // and added to the totals.
        GProp_GProps bodyVolume, bodySurface;
        BRepGProp::VolumeProperties(b.shape, bodyVolume);
        BRepGProp::SurfaceProperties(b.shape, bodySurface);
        volume.Add(bodyVolume);
        surface.Add(bodySurface);
        BRepBndLib::AddOptimal(b.shape, box, false);      // exact, not padded by tolerances
    }
    MassProperties p;
    p.volume = volume.Mass();
    p.area = surface.Mass();
    const gp_Pnt c = volume.CentreOfMass();
    p.centre = {c.X(), c.Y(), c.Z()};
    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    p.bounds = {{xmin, ymin, zmin}, {xmax, ymax, zmax}};
    return p;
}

const std::vector<Interference>& Model::Interferences() const {
    Impl& m = *myImpl;
    if (m.interferenceRevision == m.revision) return m.interferences;
    m.interferences.clear();
    m.overlaps.clear();
    m.interferenceRevision = m.revision;
    std::vector<Bnd_Box> boxes(m.bodies.size());
    for (std::size_t i = 0; i < m.bodies.size(); ++i) BRepBndLib::Add(m.bodies[i].shape, boxes[i]);
    for (std::size_t i = 0; i < m.bodies.size(); ++i)
        for (std::size_t j = i + 1; j < m.bodies.size(); ++j) {
            if (boxes[i].IsOut(boxes[j])) continue;
            try {
                BRepAlgoAPI_Common common(m.bodies[i].shape, m.bodies[j].shape);
                if (!common.IsDone()) continue;
                GProp_GProps props;
                BRepGProp::VolumeProperties(common.Shape(), props);
                if (props.Mass() < 1e-6) continue;   // only touching
                const gp_Pnt c = props.CentreOfMass();
                m.interferences.push_back({i, j, props.Mass(), {c.X(), c.Y(), c.Z()}});
                m.overlaps.push_back(common.Shape());
            } catch (const Standard_Failure&) {
                // a pair OCCT cannot intersect is left out rather than failing the check
            }
        }
    return m.interferences;
}

double Model::Volume() const {
    double volume = 0.0;
    for (const Body& b : myImpl->bodies) {
        GProp_GProps props;
        BRepGProp::VolumeProperties(b.shape, props);
        volume += props.Mass();
    }
    return volume;
}

} // namespace bz::kernel
