#include "kernel/Model.h"
#include <gp_Pnt.hxx>

#include <Standard_Version.hxx>

#include "core/FaultGuard.h"
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
#include "document/ShellFeature.h"
#include "document/ThreadFeature.h"
#include "document/WorkAxisFeature.h"
#include "document/WorkPointFeature.h"
#include "document/WorkPlaneFeature.h"
#include "kernel/occt/Coil.h"
#include "kernel/occt/Section.h"
#include "kernel/occt/EdgeMatch.h"
#include "kernel/occt/Extrude.h"
#include "kernel/occt/FaceMatch.h"
#include "kernel/occt/Fillet.h"
#include "kernel/occt/Points.h"
#include "kernel/occt/Shell.h"
#include "kernel/occt/Hole.h"
#include "kernel/occt/Loft.h"
#include "kernel/occt/ModelImpl.h"
#include "kernel/occt/Project.h"
#include "kernel/occt/Revolve.h"
#include "kernel/occt/Sweep.h"
#include "kernel/occt/Thread.h"

#include <BOPAlgo_Options.hxx>
#include <TopoDS.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Vertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <TopoDS_Compound.hxx>
#include <BRep_Builder.hxx>
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
#include <chrono>
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

/// Whether `tool` shares material with `body` — or, with `touching`, also
/// just meets it (a boss standing on a face). Boxes first: most bodies of a
/// part are far from the tool and need no boolean.
bool Meets(const TopoDS_Shape& body, const TopoDS_Shape& tool, bool touching) {
    Bnd_Box a, b;
    BRepBndLib::Add(body, a);
    BRepBndLib::Add(tool, b);
    if (a.IsVoid() || b.IsVoid()) return false;
    a.Enlarge(1e-4);
    if (a.IsOut(b)) return false;
    try {
        BRepAlgoAPI_Common common(body, tool);
        if (common.IsDone()) {
            GProp_GProps props;
            BRepGProp::VolumeProperties(common.Shape(), props);
            if (props.Mass() > 1e-6) return true;
        }
        if (!touching) return false;
        // Touching: fused, they make fewer pieces than apart. (Not by the
        // distance between them: BRepExtrema_DistShapeShape between solids
        // crashes in OCCT 8, see FaceMatch.cpp.)
        const auto pieces = [](const TopoDS_Shape& s) {
            int n = 0;
            for (TopExp_Explorer e(s, TopAbs_SOLID); e.More(); e.Next()) ++n;
            return n;
        };
        BRepAlgoAPI_Fuse fuse(body, tool);
        return fuse.IsDone() && !fuse.Shape().IsNull() && pieces(fuse.Shape()) < pieces(body) + pieces(tool);
    } catch (const Standard_Failure&) {
        return false;
    }
}

/// Indices of the bodies `tool` acts on (see Meets), in body order.
std::vector<std::size_t> BodiesMet(const std::vector<Body>& bodies, const TopoDS_Shape& tool, bool touching) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < bodies.size(); ++i)
        if (Meets(bodies[i].shape, tool, touching)) out.push_back(i);
    return out;
}

/// The last step of every operation that makes a solid: keep it as a new
/// body, or join, cut or intersect it with the bodies it reaches. A join
/// merges every body it touches into the first of them; a cut and an
/// intersection change every body they cross. A tool that reaches no body
/// is reported: a cut that misses is a warning (nothing changed), a join
/// goes to the most recent body as a separate piece. The solid is also kept
/// as the operation's tool for patterns and mirrors.
void CombineSolid(ObjectId id, doc::BooleanOp op, const TopoDS_Shape& tool, Model::Impl& impl, std::vector<Body>& bodies,
                  doc::FeatureStatus& status) {
    // A new body copied by a pattern joins the body it started.
    if (op != doc::BooleanOp::Intersect) impl.tools[id] = {tool, op == doc::BooleanOp::Cut ? doc::BooleanOp::Cut : doc::BooleanOp::Join, {}};

    if (op == doc::BooleanOp::NewBody || bodies.empty()) {
        if (op == doc::BooleanOp::Cut || op == doc::BooleanOp::Intersect) {
            status = {FeatureState::Error, "Нет тела, к которому применить операцию"};
            return;
        }
        bodies.push_back({id, NextBodyName(bodies), tool});
        status = {FeatureState::Ok, {}};
        return;
    }
    const std::vector<std::size_t> met = BodiesMet(bodies, tool, op == doc::BooleanOp::Join);
    if (met.empty()) {
        if (op == doc::BooleanOp::Cut) {
            status = {FeatureState::Warning, "Вырез не задел ни одного тела — ничего не изменилось"};
            return;
        }
        if (op == doc::BooleanOp::Intersect) {
            status = {FeatureState::Error, "Пересечение пустое: ни одно тело не перекрывается с операцией"};
            return;
        }
        Expected<TopoDS_Shape> result = occt::ApplyBoolean(bodies.back().shape, tool, op);
        if (!result) {
            status = {FeatureState::Error, result.GetError().message};
            return;
        }
        bodies.back().shape = *result;
        status = {FeatureState::Warning,
                  std::format("Не касается ни одного тела: добавлено к «{}» отдельным куском", bodies.back().name)};
        return;
    }
    if (op == doc::BooleanOp::Join) {
        // Every touched body and the tool become the first touched body.
        std::vector<TopoDS_Shape> tools{tool};
        for (std::size_t k = 1; k < met.size(); ++k) tools.push_back(bodies[met[k]].shape);
        Expected<TopoDS_Shape> result = occt::ApplyBoolean(bodies[met.front()].shape, tools, op);
        if (!result) {
            status = {FeatureState::Error, result.GetError().message};
            return;
        }
        bodies[met.front()].shape = *result;
        for (std::size_t k = met.size(); k-- > 1;) bodies.erase(bodies.begin() + static_cast<std::ptrdiff_t>(met[k]));
        status = {FeatureState::Ok, {}};
        return;
    }
    for (std::size_t i : met) {
        Expected<TopoDS_Shape> result = occt::ApplyBoolean(bodies[i].shape, tool, op);
        if (!result) {
            status = {FeatureState::Error, std::format("«{}»: {}", bodies[i].name, result.GetError().message)};
            return;
        }
        bodies[i].shape = *result;
    }
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

/// Distance from `p` to `shape`, mm (1e300 when it cannot be measured).
double DistanceTo(const TopoDS_Shape& shape, const Vec3& p) {
    const TopoDS_Vertex probe = BRepBuilderAPI_MakeVertex(gp_Pnt(p.x, p.y, p.z));
    BRepExtrema_DistShapeShape distance(probe, shape);
    return distance.IsDone() && distance.NbSolution() > 0 ? distance.Value() : 1e300;
}

/// How far the face `ref` found on `body` is from where it was picked: the
/// body that has the face is the one where it is nearest (a face with the
/// same normal is found on any body — the matching follows model changes).
double FaceMiss(const TopoDS_Shape& body, const doc::FaceReference& ref) {
    const auto face = ref.curved ? occt::ResolveAnyFace(body, ref) : occt::ResolveFace(body, ref);
    return face ? DistanceTo(*face, ref.point) : 1e300;
}

/// Fillet and chamfer: find the referenced edges on the body that has them
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
    // The body that has the most of the edges, and of those the one where
    // they are nearest where they were picked (the most recent on a tie).
    Body* target = &bodies.back();
    std::vector<TopoDS_Edge> edges;
    std::vector<std::size_t> kept;   // index in f.edges of each found edge
    double bestMiss = 1e300;
    for (auto it = bodies.rbegin(); it != bodies.rend(); ++it) {
        std::vector<TopoDS_Edge> found;
        std::vector<std::size_t> which;
        double miss = 0;
        const auto resolved = occt::ResolveEdges(f.edges, it->shape);
        for (std::size_t k = 0; k < resolved.size(); ++k)
            if (resolved[k]) {
                found.push_back(*resolved[k]);
                which.push_back(k);
                miss += DistanceTo(*resolved[k], f.edges[k].middle);
            }
        if (found.empty()) continue;
        if (found.size() > edges.size() || (found.size() == edges.size() && miss < bestMiss - 1e-6)) {
            edges = std::move(found);
            kept = std::move(which);
            bestMiss = miss;
            target = &*it;
        }
    }
    const std::size_t lost = f.edges.size() - edges.size();
    if (edges.empty() && !f.edges.empty()) {
        status = {FeatureState::Error, "Рёбра операции не найдены: модель изменилась слишком сильно"};
        return;
    }
    Expected<TopoDS_Shape> result = make(target->shape, edges, kept);
    if (!result) {
        status = {FeatureState::Error, result.GetError().message};
        return;
    }
    target->shape = *result;
    status = lost == 0 ? doc::FeatureStatus{FeatureState::Ok, {}}
                       : doc::FeatureStatus{FeatureState::Warning, std::format("Не найдено рёбер: {}", lost)};
}

/// Operations that change one body (hole, shell): `miss` says how far the
/// operation's face is from a body (FaceMiss); the nearest body is changed,
/// the most recent one on a tie or when `miss` is not given. `make` turns
/// its shape into the new one.
template <class Miss, class Make>
void ApplyToBody(std::vector<Body>& bodies, doc::FeatureStatus& status, const char* noBody, Miss&& miss, Make&& make) {
    if (bodies.empty()) {
        status = {FeatureState::Error, noBody};
        return;
    }
    Body* target = &bodies.back();
    double best = 1e300;
    for (auto it = bodies.rbegin(); it != bodies.rend(); ++it)
        if (const double m = miss(it->shape); m < best - 1e-6) {
            best = m;
            target = &*it;
        }
    Expected<TopoDS_Shape> result = make(target->shape);
    if (!result) {
        status = {FeatureState::Error, result.GetError().message};
        return;
    }
    target->shape = *result;
    status = {FeatureState::Ok, {}};
}

template <class Make>
void ApplyToLastBody(std::vector<Body>& bodies, doc::FeatureStatus& status, const char* noBody, Make&& make) {
    ApplyToBody(bodies, status, noBody, [](const TopoDS_Shape&) { return 0.0; }, std::forward<Make>(make));
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
    if (thread.cosmetic) {   // drawn on the face only; exports cut it (Model::WithRealThreads)
        const Expected<TopoDS_Shape> drawn = occt::CosmeticThread(target->shape, thread);
        if (!drawn) {
            thread.status = {FeatureState::Error, drawn.GetError().message};
            return;
        }
        impl.decorations[thread.id] = *drawn;
        thread.status = {FeatureState::Ok, {}};
        return;
    }
    const Expected<TopoDS_Shape> tool = occt::ThreadTool(target->shape, thread);
    Expected<TopoDS_Shape> result = tool ? occt::ApplyBoolean(target->shape, *tool, doc::BooleanOp::Cut) : tool;
    if (!result) {
        thread.status = {FeatureState::Error, result.GetError().message};
        return;
    }
    impl.tools[thread.id] = {*tool, doc::BooleanOp::Cut, {}};
    target->shape = *result;
    thread.status = {FeatureState::Ok, {}};
}

/// Patterns and mirrors: the tools of the source operations, moved by each
/// of `placements`, joined to or cut from the bodies they reach.
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
            status = {FeatureState::Error, std::format("«{}» нельзя скопировать: копируются операции без ошибок, которые добавляют или вырезают материал "
                                                       "(выдавливание, вращение, сдвиг, лофт, пружина, отверстие, резьба, массив, зеркало); "
                                                       "скругление, фаска и оболочка — нет",
                                                       sf ? sf->name : std::string("?"))};
            return;
        }
        preview.sources.push_back(tool->second.shape);
        // A pattern's tool is a compound of copies that may overlap one
        // another: a boolean must get them as separate tools (an
        // overlapping compound is invalid input and can crash OCCT).
        const auto add = [](std::vector<TopoDS_Shape>& to, const TopoDS_Shape& shape) {
            if (shape.ShapeType() != TopAbs_COMPOUND) {
                to.push_back(shape);
                return;
            }
            for (TopExp_Explorer e(shape, TopAbs_SOLID); e.More(); e.Next()) to.push_back(e.Current());
        };
        for (const gp_Trsf& t : placements) {
            const TopoDS_Shape copy = BRepBuilderAPI_Transform(tool->second.shape, t, true).Shape();
            add(tool->second.op == doc::BooleanOp::Cut ? cuts : joins, copy);
            preview.copies.push_back(copy);
            if (!tool->second.cuts.IsNull()) add(cuts, BRepBuilderAPI_Transform(tool->second.cuts, t, true).Shape());
        }
    }
    // What this operation added and cut: a later pattern or mirror can copy it in turn.
    const auto compound = [](const std::vector<TopoDS_Shape>& shapes) {
        TopoDS_Compound c;
        BRep_Builder builder;
        builder.MakeCompound(c);
        for (const TopoDS_Shape& s : shapes) builder.Add(c, s);
        return TopoDS_Shape(c);
    };
    if (!joins.empty()) impl.tools[f.id] = {compound(joins), doc::BooleanOp::Join, cuts.empty() ? TopoDS_Shape() : compound(cuts)};
    else if (!cuts.empty()) impl.tools[f.id] = {compound(cuts), doc::BooleanOp::Cut, {}};
    // Each copy goes to the bodies it reaches: added ones to the body they
    // touch, cut ones from every body they cross. Added copies that touch
    // nothing go to the most recent body (as separate pieces, reported).
    std::vector<std::vector<TopoDS_Shape>> joinsOf(bodies.size()), cutsOf(bodies.size());
    std::size_t loose = 0, missed = 0;
    for (const TopoDS_Shape& t : joins) {
        const std::vector<std::size_t> met = BodiesMet(bodies, t, true);
        if (met.empty()) ++loose;
        joinsOf[met.empty() ? bodies.size() - 1 : met.front()].push_back(t);
    }
    for (const TopoDS_Shape& t : cuts) {
        const std::vector<std::size_t> met = BodiesMet(bodies, t, false);
        if (met.empty()) ++missed;
        for (std::size_t i : met) cutsOf[i].push_back(t);
    }
    for (std::size_t i = 0; i < bodies.size(); ++i)
        for (auto [tools, op] : {std::pair{&joinsOf[i], doc::BooleanOp::Join}, {&cutsOf[i], doc::BooleanOp::Cut}}) {
            Expected<TopoDS_Shape> result = occt::ApplyBoolean(bodies[i].shape, *tools, op);
            if (!result) {
                status = {FeatureState::Error, result.GetError().message};
                return;
            }
            bodies[i].shape = *result;
        }
    if (loose > 0)
        status = {FeatureState::Warning, std::format("Копий не касается тела: {} — добавлены к «{}» отдельными кусками", loose,
                                                     bodies.back().name)};
    else if (missed > 0 && missed == cuts.size())
        status = {FeatureState::Warning, "Ни одна копия выреза не задела тело — ничего не изменилось"};
    else
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

/// Points picked for a plane or an axis, moved to where they are now: work
/// points where they were placed, vertices found again by an edge ending
/// there. False (with `error`) when a work point is missing or not placed.
bool ResolvePointPicks(std::vector<Vec3>& points, std::vector<std::optional<doc::EdgeReference>>& edges,
                       std::vector<ObjectId>& workPoints, const doc::Document& doc, const std::vector<Body>& bodies,
                       const char*& error) {
    edges.resize(points.size());
    workPoints.resize(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (workPoints[i]) {
            const auto* wp = doc.FindAs<doc::WorkPointFeature>(workPoints[i]);
            if (!wp || !wp->position) {
                error = "Рабочая точка не построена";
                return false;
            }
            points[i] = *wp->position;
            continue;
        }
        if (!edges[i]) continue;
        for (auto it = bodies.rbegin(); it != bodies.rend(); ++it) {
            doc::EdgeReference ref = *edges[i];
            if (const auto at = occt::ResolveVertex(ref, points[i], it->shape)) {
                points[i] = *at;
                edges[i] = ref;
                break;
            }
        }
    }
    return true;
}

/// The edge `ref` on the most recent body that has it.
std::optional<TopoDS_Edge> FindEdge(const doc::EdgeReference& ref, const std::vector<Body>& bodies) {
    for (auto it = bodies.rbegin(); it != bodies.rend(); ++it)
        if (auto e = occt::ResolveEdges({ref}, it->shape).front()) return e;
    return std::nullopt;
}

/// Puts a work point where its method says.
void PlaceWorkPoint(doc::WorkPointFeature& wp, const doc::Document& doc, const std::vector<Body>& bodies) {
    const auto fail = [&](const char* message) {
        wp.position.reset();
        wp.status = {FeatureState::Error, message};
    };
    using doc::WorkPointMethod;
    switch (wp.method) {
        case WorkPointMethod::Vertex: {
            if (!wp.edge) return fail("Щёлкните вершину тела");
            std::optional<Vec3> at;
            for (auto it = bodies.rbegin(); it != bodies.rend() && !at; ++it) {
                doc::EdgeReference ref = *wp.edge;   // updated only where the vertex is found
                if ((at = occt::ResolveVertex(ref, wp.vertex, it->shape))) wp.edge = ref;
            }
            if (!at) return fail("Вершина не найдена: модель изменилась слишком сильно");
            wp.vertex = *at;
            wp.position = *at;
            break;
        }
        case WorkPointMethod::Center: {
            if (wp.sphere) {
                std::optional<Vec3> c;
                for (auto it = bodies.rbegin(); it != bodies.rend() && !c; ++it)
                    if (const auto face = occt::ResolveAnyFace(it->shape, *wp.sphere)) c = occt::SphereCenter(*face);
                if (!c) return fail("Сферическая грань не найдена");
                wp.position = c;
                break;
            }
            if (!wp.edge) return fail("Щёлкните круглое ребро или сферу");
            const auto edge = FindEdge(*wp.edge, bodies);
            if (!edge) return fail("Ребро не найдено: модель изменилась слишком сильно");
            const auto c = occt::EdgeCenter(*edge);
            if (!c) return fail("У ребра нет центра: нужна окружность, дуга или эллипс");
            wp.position = c;
            break;
        }
        case WorkPointMethod::Midpoint: {
            if (!wp.edge) return fail("Щёлкните ребро");
            const auto edge = FindEdge(*wp.edge, bodies);
            if (!edge) return fail("Ребро не найдено: модель изменилась слишком сильно");
            wp.position = occt::EdgeMiddle(*edge);
            break;
        }
        case WorkPointMethod::Intersection: {
            std::optional<std::pair<Vec3, Vec3>> line;
            if (wp.axis) {
                const auto* wa = doc.FindAs<doc::WorkAxisFeature>(wp.axis);
                if (wa) line = wa->line;
            } else if (wp.edge) {
                for (auto it = bodies.rbegin(); it != bodies.rend() && !line; ++it) line = occt::ResolveLine(*wp.edge, it->shape);
            }
            if (!line) return fail("Щёлкните прямое ребро или рабочую ось");
            const std::optional<Frame> plane = PlaneOf(wp.plane, doc);
            if (!plane) return fail("Выберите плоскость");
            const double along = line->second.Dot(plane->normal);
            if (std::abs(along) < 1e-9) return fail("Линия параллельна плоскости — пересечения нет");
            const double t = (plane->origin - line->first).Dot(plane->normal) / along;
            wp.position = line->first + line->second * t;
            break;
        }
        case WorkPointMethod::Coordinates:
            for (const Parameter* p : {&wp.x, &wp.y, &wp.z})
                if (p->HasError()) return fail("Координата задана с ошибкой");
            wp.position = Vec3{wp.x.value, wp.y.value, wp.z.value};
            break;
    }
    wp.status = {FeatureState::Ok, {}};
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
        case doc::WorkAxisMethod::Points: {
            if (wa.points.size() < 2) return fail("Выберите две точки");
            const char* error = nullptr;
            if (!ResolvePointPicks(wa.points, wa.pointEdges, wa.pointFeatures, doc, bodies, error)) return fail(error);
            const Vec3 dir = wa.points[1] - wa.points[0];
            if (dir.Length() < 1e-9) return fail("Точки совпадают — ось через них не одна");
            wa.line = std::make_pair(wa.points[0], dir.Normalized());
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


/// The reflection of a mirror, about an origin plane, a work plane or a
/// flat face of a body.
Expected<gp_Trsf> MirrorPlacement(const doc::MirrorFeature& f, const doc::Document& doc, const std::vector<Body>& bodies) {
    std::optional<Frame> plane = f.plane ? PlaneOf(f.plane, doc) : std::nullopt;
    if (!f.plane && f.face.IsSet()) {
        for (auto it = bodies.rbegin(); it != bodies.rend() && !plane; ++it) plane = occt::ResolvePlaneFrame(it->shape, f.face);
        if (!plane) return Error{"Грань плоскости зеркала не найдена: выберите плоскость заново"};
    }
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
        if (wp.points.size() < 3) return fail("Выберите три точки");
        const char* error = nullptr;
        if (!ResolvePointPicks(wp.points, wp.pointEdges, wp.pointFeatures, doc, bodies, error)) return fail(error);
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
        case ProjectedCurve::Kind::Ellipse:
            if (const sketch::Ellipse* e = sk.FindEllipse(curve)) {
                move(e->center, pc.centre);
                move(e->major, pc.start);
                move(e->minor, pc.end);
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
/// Process-wide OCCT setup: fewer messages, booleans on the calling thread.
void SetUpOcct() {
    static std::once_flag once;
    std::call_once(once, [] {
        // Booleans on the thread that asked for them, not OCCT's worker pool:
        // FaultGuard catches a fault only on the thread that called
        // RunGuarded, so a fault in a worker closed the program (audit B-08;
        // the author's decision 2026-10-13: stability first, a 2-core laptop
        // loses little speed).
        BOPAlgo_Options::SetParallelMode(false);
        const Handle(Message_Messenger)& messenger = Message::DefaultMessenger();
        // Range-for works with both OCCT 7.x and 8.x sequence types.
        for (const auto& printer : messenger->Printers()) printer->SetTraceLevel(Message_Warning);
    });
}

} // namespace

Model::Model() : myImpl(std::make_unique<Impl>()) { SetUpOcct(); }
Model::~Model() = default;
Model::Model(Model&&) noexcept = default;
Model& Model::operator=(Model&&) noexcept = default;

void Model::Rebuild(doc::Document& doc) { RebuildUpTo(doc, static_cast<std::size_t>(-1), Stopped::KeepShown); }

void Model::RebuildUpTo(doc::Document& doc, std::size_t last, Stopped shown) { RebuildSlice(doc, last, shown, -1.0); }

Model::Progress Model::RebuildProgress() const { return {myImpl->sliceDone, myImpl->sliceTotal}; }

bool Model::RebuildSlice(doc::Document& doc, std::size_t last, Stopped shown, double budgetMs) {
    Impl& impl = *myImpl;
    const auto sliceStart = std::chrono::steady_clock::now();
    doc::RecomputeModel(doc);

    const std::size_t count = doc.FeatureCount();
    const std::size_t start = FirstToRebuild(doc);
    impl.cachedOrder.resize(count);
    impl.bodiesAfter.resize(count);

    std::vector<Body> bodies = start > 0 ? impl.bodiesAfter[start - 1] : std::vector<Body>{};
    const std::size_t end = last < count ? last + 1 : count;
    if (!impl.slicing || start < impl.sliceFirst) {   // a new rebuild (or an edit went further back)
        impl.sliceFirst = start;
        impl.slicing = true;
    }
    impl.sliceTotal = end > impl.sliceFirst ? end - impl.sliceFirst : 0;
    std::size_t stoppedAt = end;
    for (std::size_t i = start; i < end; ++i) {
        // Out of time (after at least one operation): the rest next time.
        if (budgetMs >= 0 && i > start &&
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sliceStart).count() > budgetMs) {
            stoppedAt = i;
            break;
        }
        impl.sliceDone = i - std::min(i, impl.sliceFirst);
        doc::Feature& f = *doc.Features()[i];
        impl.cachedOrder[i] = f.id;
        impl.tools.erase(f.id);   // set again below if the operation succeeds
        impl.decorations.erase(f.id);

        const auto featureStart = std::chrono::steady_clock::now();
        const bool skip = !doc.IsActive(i) || f.suppressed;
        // One failing operation must not take the program down: whatever OCCT
        // or our code throws becomes that operation's error, and the bodies
        // stay as they were before it.
        const std::vector<Body> before = skip ? std::vector<Body>{} : bodies;
        // A crash inside OCCT itself (it happens on some degenerate shapes)
        // costs this operation only: RunGuarded stops it and the part goes on.
        std::string fault;
        const bool survived = skip || guard::RunGuarded([&] { try {
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
                                     std::optional<TopoDS_Face> until;
                                     if (ex->extent == doc::ExtentType::ToPlane) {
                                         if (ex->toPlane) target = PlaneOf(ex->toPlane, doc);
                                         else if (ex->toFace.IsSet() && ex->toFace.curved) {
                                             for (auto it = bodies.rbegin(); it != bodies.rend() && !until; ++it)
                                                 until = occt::ResolveAnyFace(it->shape, ex->toFace);
                                             if (!until) return Expected<TopoDS_Shape>(Error{"Грань «до» не найдена: выберите её заново"});
                                         } else if (ex->toFace.IsSet())
                                             for (auto it = bodies.rbegin(); it != bodies.rend() && !target; ++it)
                                                 target = occt::ResolvePlaneFrame(it->shape, ex->toFace);
                                     }
                                     return occt::MakeExtrusion(faces, *ex, frame, ThroughAllLength(bodies, frame), target, until);
                                 });
            } else if (auto* rev = dynamic_cast<doc::RevolveFeature*>(&f)) {
                ApplySketchSolid(*rev, impl, bodies, rev->status, doc,
                                 [&](const std::vector<TopoDS_Face>& faces, const doc::SketchFeature& profile, const Frame& frame) {
                                     return occt::MakeRevolution(faces, *rev, profile.sketch, frame);
                                 });
            } else if (auto* fillet = dynamic_cast<doc::FilletFeature*>(&f)) {
                ApplyEdgeFeature(*fillet, bodies, fillet->status,
                                 [&](const TopoDS_Shape& body, const std::vector<TopoDS_Edge>& edges, const std::vector<std::size_t>&) {
                                     return occt::MakeFillet(body, edges, fillet->radius.value);
                                 });
            } else if (auto* chamfer = dynamic_cast<doc::ChamferFeature*>(&f)) {
                ApplyEdgeFeature(*chamfer, bodies, chamfer->status,
                                 [&](const TopoDS_Shape& body, const std::vector<TopoDS_Edge>& edges, const std::vector<std::size_t>& kept) {
                                     occt::ChamferSize size{chamfer->mode, chamfer->distance.value, chamfer->distance2.value,
                                                            chamfer->angle.value, chamfer->flipped, {}};
                                     // Per edge: its own turn of side (the global one is `flipped`).
                                     for (const std::size_t k : kept) size.flips.push_back(chamfer->FlippedAt(k) != chamfer->flipped);
                                     return occt::MakeChamfer(body, edges, size);
                                 });
            } else if (auto* hole = dynamic_cast<doc::HoleFeature*>(&f)) {
                ApplyToBody(bodies, hole->status, "Нет тела, в котором делать отверстие",
                                [&](const TopoDS_Shape& body) { return FaceMiss(body, hole->face); },
                                [&](const TopoDS_Shape& body) -> Expected<TopoDS_Shape> {
                                    // Keep the reference on the face as it is now, so the
                                    // panel draws the centres where the holes are.
                                    if (const auto plane = occt::ResolvePlaneFrame(body, hole->face)) {
                                        const Vec3 n = hole->face.normal;
                                        hole->face.point = hole->face.point + n * (plane->origin - hole->face.point).Dot(n);
                                    }
                                    // Centres held coaxial with a round edge or a cylinder go
                                    // where that axis is now.
                                    if (occt::FollowAxes(body, *hole) > 0)
                                        return Error{"Кромка или цилиндр, с которыми отверстие соосно, не найдены: укажите их заново"};
                                    const Expected<TopoDS_Shape> tools = occt::HoleTools(body, *hole);
                                    if (!tools) return tools.GetError();
                                    impl.tools[hole->id] = {*tools, doc::BooleanOp::Cut, {}};
                                    return occt::ApplyBoolean(body, *tools, doc::BooleanOp::Cut);
                                });
            } else if (auto* shell = dynamic_cast<doc::ShellFeature*>(&f)) {
                if (shell->thickness.HasError()) {
                    shell->status = {FeatureState::Error, shell->thickness.error};
                } else {
                    std::size_t lost = 0;
                    const auto openFaceMiss = [&](const TopoDS_Shape& body) {
                        double miss = 0;   // a closed shell (no faces): the most recent body
                        for (const doc::FaceReference& r : shell->faces) miss += std::min(FaceMiss(body, r), 1e6);
                        return miss;
                    };
                    ApplyToBody(bodies, shell->status, "Нет тела, из которого делать оболочку", openFaceMiss,
                                    [&](const TopoDS_Shape& body) -> Expected<TopoDS_Shape> {
                                        std::vector<TopoDS_Face> open;
                                        for (const doc::FaceReference& r : shell->faces) {
                                            const auto face = r.curved ? occt::ResolveAnyFace(body, r) : occt::ResolveFace(body, r);
                                            if (face) open.push_back(*face);
                                            else ++lost;
                                        }
                                        if (open.empty() && !shell->faces.empty())
                                            return Error{"Грани оболочки не найдены: модель изменилась слишком сильно"};
                                        return occt::MakeShell(body, open, shell->thickness.value, shell->outward);
                                    });
                    if (lost > 0 && shell->status.state == FeatureState::Ok)
                        shell->status = {FeatureState::Warning, std::format("Не найдено граней: {}", lost)};
                }
            } else if (auto* thread = dynamic_cast<doc::ThreadFeature*>(&f)) {
                ApplyThread(*thread, impl, bodies);
            } else if (auto* pattern = dynamic_cast<doc::PatternFeature*>(&f)) {
                if (const auto placements = PatternPlacements(*pattern, doc))
                    ApplyCopies(*pattern, *placements, impl, bodies, pattern->status, doc);
                else
                    pattern->status = {FeatureState::Error, placements.GetError().message};
            } else if (auto* mirror = dynamic_cast<doc::MirrorFeature*>(&f)) {
                const auto placement = MirrorPlacement(*mirror, doc, bodies);
                if (!placement) {
                    mirror->status = {FeatureState::Error, placement.GetError().message};
                } else if (mirror->wholeBody) {
                    // The body and its mirror image, joined (touching halves become one).
                    ApplyToLastBody(bodies, mirror->status, "Нет тела, которое отражать",
                                    [&](const TopoDS_Shape& body) -> Expected<TopoDS_Shape> {
                                        const TopoDS_Shape image = BRepBuilderAPI_Transform(body, *placement, true).Shape();
                                        impl.tools[mirror->id] = {image, doc::BooleanOp::Join, {}};
                                        return occt::ApplyBoolean(body, image, doc::BooleanOp::Join);
                                    });
                } else {
                    ApplyCopies(*mirror, {*placement}, impl, bodies, mirror->status, doc);
                }
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
            } else if (auto* wpt = dynamic_cast<doc::WorkPointFeature*>(&f)) {
                PlaceWorkPoint(*wpt, doc, bodies);
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
        } }, &fault);
        if (!survived) {
            bodies = before;
            f.status = {FeatureState::Error, "Геометрическое ядро не смогло выполнить операцию (сбой внутри OpenCASCADE): измените параметры или геометрию"};
            log::Error("{}: OCCT fault ({}) — operation skipped", f.name, fault);
        }
        if (f.status.state == FeatureState::Error) log::Warn("{}: {}", f.name, f.status.message);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - featureStart).count();
        if (!skip) impl.costMs[f.id] = ms;
        if (ms > 50) log::Info("Rebuild: {} took {:.0f} ms", f.name, ms);
        impl.bodiesAfter[i] = bodies;
    }

    if (stoppedAt < end) {   // a slice: the part shown stays until the rebuild is done
        doc.KeepDirtyFrom(stoppedAt);
        impl.sliceDone = stoppedAt - std::min(stoppedAt, impl.sliceFirst);
        return false;
    }
    impl.slicing = false;
    impl.sliceDone = impl.sliceTotal = 0;
    // The visible result is the state at the end-of-part marker (or, while
    // an operation is edited, right after it).
    if (end < count) {
        doc.KeepDirtyFrom(end);
        if (shown == Stopped::ShowAtStop) {
            impl.bodies = end > 0 ? impl.bodiesAfter[end - 1] : std::vector<Body>{};
            impl.bodiesSet = true;
        }
    } else {
        const std::size_t marker = std::min(doc.EndMarker(), count);
        impl.bodies = marker > 0 ? impl.bodiesAfter[marker - 1] : std::vector<Body>{};
        impl.bodiesSet = true;
        doc.ClearDirty();
    }
    impl.infos.clear();
    for (const Body& b : impl.bodies) impl.infos.push_back({b.id, b.name});
    // Drawings of features deleted or past the end-of-part marker go.
    std::erase_if(impl.decorations, [&](const auto& d) {
        const int i = doc.IndexOf(d.first);
        return i < 0 || !doc.IsActive(static_cast<std::size_t>(i));
    });
    ++impl.revision;
    return true;
}

std::size_t Model::FirstToRebuild(const doc::Document& doc) const {
    // Cached results are reused up to the first changed feature, as long as
    // the order of features has not changed before that point.
    const std::size_t start = std::min(doc.DirtyFrom(), doc.FeatureCount());
    for (std::size_t i = 0; i < start; ++i)
        if (i >= myImpl->cachedOrder.size() || myImpl->cachedOrder[i] != doc.Features()[i]->id) return i;
    return start;
}

double Model::EstimateRebuildMs(const doc::Document& doc, std::size_t last) const {
    constexpr double kNeverBuilt = 1000.0;
    const std::size_t count = doc.FeatureCount();
    const std::size_t end = last < count ? last + 1 : count;
    double total = 0;
    for (std::size_t i = FirstToRebuild(doc); i < end; ++i) {
        const doc::Feature& f = *doc.Features()[i];
        if (!doc.IsActive(i) || f.suppressed) continue;
        const auto it = myImpl->costMs.find(f.id);
        total += it != myImpl->costMs.end() ? it->second : kNeverBuilt;
    }
    return total;
}

Model Model::Fork(std::size_t start) const {
    const Impl& a = *myImpl;
    Model fork;
    Impl& b = *fork.myImpl;
    b.cachedOrder = a.cachedOrder;
    b.bodiesAfter.resize(a.bodiesAfter.size());
    b.costMs = a.costMs;
    b.revision = a.revision;   // `bodies` stays empty: the shown ones are not the fork's to touch
    start = std::min(start, a.bodiesAfter.size());

    // Every shape the rebuild can start from, copied in one go so shapes
    // that shared parts still share them in the copy.
    TopoDS_Compound all;
    BRep_Builder builder;
    builder.MakeCompound(all);
    const auto add = [&](const TopoDS_Shape& s) {
        if (!s.IsNull()) builder.Add(all, s);
    };
    if (start > 0)
        for (const Body& body : a.bodiesAfter[start - 1]) add(body.shape);
    for (const auto& [id, tool] : a.tools) {
        add(tool.shape);
        add(tool.cuts);
    }
    for (const auto& [id, profile] : a.sketches)
        for (const TopoDS_Face& face : profile.faces) add(face);
    BRepBuilderAPI_Copy copier(all, true, false);
    const auto copied = [&](const TopoDS_Shape& s) {
        if (s.IsNull()) return s;
        TopoDS_Shape image = copier.ModifiedShape(s);
        image.Orientation(s.Orientation());
        return image;
    };

    if (start > 0)
        for (const Body& body : a.bodiesAfter[start - 1]) b.bodiesAfter[start - 1].push_back({body.id, body.name, copied(body.shape)});
    for (const auto& [id, tool] : a.tools) b.tools[id] = {copied(tool.shape), tool.op, copied(tool.cuts)};
    for (const auto& [id, profile] : a.sketches) {
        occt::SketchProfile& p = b.sketches[id];
        p.frame = profile.frame;
        p.regions = profile.regions;
        for (const TopoDS_Face& face : profile.faces) p.faces.push_back(TopoDS::Face(copied(face)));
    }
    return fork;
}

void Model::Adopt(Model&& fork, std::size_t start) {
    Impl& a = *myImpl;
    Impl& b = *fork.myImpl;
    a.cachedOrder = b.cachedOrder;
    a.bodiesAfter.resize(b.bodiesAfter.size());
    for (std::size_t i = start; i < b.bodiesAfter.size(); ++i) a.bodiesAfter[i] = std::move(b.bodiesAfter[i]);
    // What the features from `start` on made; earlier ones keep their own shapes.
    for (std::size_t i = start; i < b.cachedOrder.size(); ++i) {
        const ObjectId id = b.cachedOrder[i];
        const auto take = [id](auto& to, auto& from) {
            if (const auto it = from.find(id); it != from.end()) to[id] = std::move(it->second);
            else to.erase(id);
        };
        take(a.tools, b.tools);
        take(a.sketches, b.sketches);
        take(a.copyPreviews, b.copyPreviews);
        take(a.decorations, b.decorations);
    }
    std::erase_if(a.decorations, [&](const auto& d) { return std::ranges::find(a.cachedOrder, d.first) == a.cachedOrder.end(); });
    a.costMs = std::move(b.costMs);
    if (b.bodiesSet) {   // otherwise the part shown stays as it was
        a.bodies = std::move(b.bodies);
        a.infos = std::move(b.infos);
    }
    a.slicing = false;
    a.sliceDone = a.sliceTotal = 0;
    a.revision = std::max(a.revision, b.revision) + 1;
}

std::uint64_t Model::Revision() const { return myImpl->revision; }

const std::vector<BodyInfo>& Model::Bodies() const { return myImpl->infos; }

ObjectId Model::BodyNearest(Vec3 point) const {
    const TopoDS_Vertex probe = BRepBuilderAPI_MakeVertex(gp_Pnt(point.x, point.y, point.z));
    ObjectId best;
    double bestDistance = 1e300;
    for (const Body& b : myImpl->bodies) {
        BRepExtrema_DistShapeShape distance(probe, b.shape);
        if (!distance.IsDone() || distance.Value() >= bestDistance) continue;
        best = b.id;
        bestDistance = distance.Value();
    }
    return best;
}

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

Expected<RevolvedProjection> Model::ProjectRevolved(const doc::FaceReference& face, const Frame& plane, std::size_t featureIndex) const {
    const auto& after = myImpl->bodiesAfter;
    if (featureIndex == 0 || featureIndex > after.size() || after[featureIndex - 1].empty()) return Error{"Нет тела, с которого проецировать"};
    const TopoDS_Shape& body = after[featureIndex - 1].back().shape;
    const std::optional<TopoDS_Face> found = occt::ResolveAnyFace(body, face);
    if (!found) return Error{"Грань не найдена"};
    return occt::ProjectRevolved(body, *found, plane);
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

ObjectId Model::FeatureAt(Vec3 point, const doc::Document& doc, std::size_t beforeIndex) const {
    const TopoDS_Vertex probe = BRepBuilderAPI_MakeVertex(gp_Pnt(point.x, point.y, point.z));
    // A clicked point lies on the displayed triangles, not the exact
    // surface: allow for the tessellation (a small share of the model size).
    double tolerance = 0.05;
    if (const auto bounds = BoundingBox()) tolerance = std::max(tolerance, (bounds->max - bounds->min).Length() * 0.003);
    for (std::size_t i = std::min(beforeIndex, doc.FeatureCount()); i-- > 0;) {
        const ObjectId id = doc.Features()[i]->id;
        const auto tool = myImpl->tools.find(id);
        if (tool == myImpl->tools.end()) continue;
        // Face by face: measured against a solid, a point inside it counts
        // as on it (and OCCT 8 can crash in that inside test).
        for (const TopoDS_Shape* s : {&tool->second.shape, &tool->second.cuts}) {
            if (s->IsNull()) continue;
            Bnd_Box box;
            BRepBndLib::Add(*s, box);
            box.Enlarge(tolerance);
            if (box.IsOut(gp_Pnt(point.x, point.y, point.z))) continue;
            for (TopExp_Explorer e(*s, TopAbs_FACE); e.More(); e.Next()) {
                BRepExtrema_DistShapeShape distance(probe, e.Current());
                if (distance.IsDone() && distance.Value() < tolerance) return id;
            }
        }
    }
    return {};
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

std::string GeometryKernelVersion() { return std::string("OpenCASCADE ") + OCC_VERSION_COMPLETE; }

bool PlaceProjectedCurve(sketch::Sketch& sk, ObjectId curve, const ProjectedCurve& projected) {
    return PlaceProjected(sk, curve, projected);
}

bool CurveMatches(const sketch::Sketch& sk, ObjectId curve, const ProjectedCurve& pc, double tolerance) {
    const auto at = [&](ObjectId point, Vec2 want) {
        const sketch::Point* p = sk.FindPoint(point);
        return p && (p->pos - want).Length() <= tolerance;
    };
    switch (pc.kind) {
        case ProjectedCurve::Kind::Line:
            if (const sketch::Line* l = sk.FindLine(curve)) return at(l->start, pc.start) && at(l->end, pc.end);
            return false;
        case ProjectedCurve::Kind::Circle:
            if (const sketch::Circle* c = sk.FindCircle(curve)) return at(c->center, pc.centre) && std::abs(c->radius - pc.radius) <= tolerance;
            return false;
        case ProjectedCurve::Kind::Arc:
            if (const sketch::Arc* a = sk.FindArc(curve)) return at(a->center, pc.centre) && at(a->start, pc.start) && at(a->end, pc.end);
            return false;
        case ProjectedCurve::Kind::Ellipse:
            if (const sketch::Ellipse* e = sk.FindEllipse(curve)) return at(e->center, pc.centre) && at(e->major, pc.start) && at(e->minor, pc.end);
            return false;
    }
    return false;
}

} // namespace bz::kernel
