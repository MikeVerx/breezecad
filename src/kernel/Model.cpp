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

#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepTools.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_Printer.hxx>
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
    return "Тело" + std::to_string(largest + 1);
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
            f.status = {FeatureState::Error, "В «" + sketch->name + "» должен быть ровно один замкнутый контур"};
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
                       : doc::FeatureStatus{FeatureState::Warning, "Не найдено рёбер: " + std::to_string(lost)};
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

/// Patterns and mirrors: the tools of the source operations, moved by each
/// of `placements`, joined to or cut from the most recent body.
void ApplyCopies(const doc::CopyFeature& f, const std::vector<gp_Trsf>& placements, const Model::Impl& impl,
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
    for (ObjectId source : f.sources) {
        const auto tool = impl.tools.find(source);
        if (tool == impl.tools.end()) {
            const doc::Feature* sf = doc.Find(source);
            status = {FeatureState::Error, "«" + (sf ? sf->name : std::string("?")) + "» нельзя скопировать: копируются "
                                           "только выдавливание, вращение и отверстия без ошибок"};
            return;
        }
        for (const gp_Trsf& t : placements) {
            const TopoDS_Shape copy = BRepBuilderAPI_Transform(tool->second.shape, t, true).Shape();
            (tool->second.op == doc::BooleanOp::Cut ? cuts : joins).push_back(copy);
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
Expected<std::vector<gp_Trsf>> PatternPlacements(const doc::PatternFeature& f) {
    const auto copies = f.Copies();
    if (!copies) return copies.GetError();
    const Vec3 a = doc::AxisDirection(f.axis);
    const gp_Ax1 axis(gp_Pnt(0, 0, 0), gp_Dir(a.x, a.y, a.z));
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
    std::optional<Frame> base;
    if (wp.face.IsSet()) {
        for (auto it = bodies.rbegin(); it != bodies.rend() && !base; ++it) base = occt::ResolvePlaneFrame(it->shape, wp.face);
        if (!base) {
            wp.frame.reset();
            wp.status = {FeatureState::Error, "Грань рабочей плоскости не найдена: модель изменилась слишком сильно"};
            return;
        }
    } else if (const auto originPlane = doc::origin::PlaneFrame(wp.base)) {
        base = originPlane;
    } else if (const auto* other = doc.FindAs<doc::WorkPlaneFeature>(wp.base); other && other->frame) {
        base = other->frame;
    }
    if (!base) {
        wp.frame.reset();
        wp.status = {FeatureState::Error, "Не задана плоскость, от которой строить"};
        return;
    }
    if (wp.offset.HasError()) {
        wp.status = {FeatureState::Error, wp.offset.error};
        return;
    }
    wp.frame = doc::OffsetFrame(*base, wp.reversed ? -wp.offset.value : wp.offset.value);
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
    for (const doc::Projection& p : sk.projections) {
        std::optional<TopoDS_Edge> edge;
        if (!bodies.empty()) edge = occt::ResolveEdges({p.edge}, bodies.back().shape).front();
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
        if (!skip) {
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
                                     return occt::MakeExtrusion(faces, *ex, frame, ThroughAllLength(bodies, frame));
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
            } else if (auto* pattern = dynamic_cast<doc::PatternFeature*>(&f)) {
                if (const auto placements = PatternPlacements(*pattern))
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
            } else if (auto* section = dynamic_cast<doc::SectionFeature*>(&f)) {
                ApplySection(*section, doc, bodies);
            } else if (f.status.state == FeatureState::NotComputed) {
                f.status = {FeatureState::Error, "Операция этого типа не поддерживается"};
            }
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
