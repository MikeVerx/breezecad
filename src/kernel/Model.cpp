#include "kernel/Model.h"

#include "core/Log.h"
#include "document/EdgeFeature.h"
#include "document/HoleFeature.h"
#include "document/ExtrudeFeature.h"
#include "document/Recompute.h"
#include "document/RevolveFeature.h"
#include "document/ShellFeature.h"
#include "document/Origin.h"
#include "document/PatternFeature.h"
#include "document/SketchFeature.h"
#include "document/WorkPlaneFeature.h"
#include "kernel/occt/EdgeMatch.h"
#include "kernel/occt/Extrude.h"
#include "kernel/occt/FaceMatch.h"
#include "kernel/occt/Fillet.h"
#include "kernel/occt/Hole.h"
#include "kernel/occt/ModelImpl.h"
#include "kernel/occt/Revolve.h"
#include "kernel/occt/Shell.h"

#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
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
    // A new body copied by a pattern joins the body it started.
    if (f.operation != doc::BooleanOp::Intersect)
        impl.tools[f.id] = {*tool, f.operation == doc::BooleanOp::Cut ? doc::BooleanOp::Cut : doc::BooleanOp::Join};

    if (f.operation == doc::BooleanOp::NewBody || bodies.empty()) {
        if (f.operation == doc::BooleanOp::Cut || f.operation == doc::BooleanOp::Intersect) {
            status = {FeatureState::Error, "Нет тела, к которому применить операцию"};
            return;
        }
        bodies.push_back({f.id, NextBodyName(bodies), *tool});
        status = {FeatureState::Ok, {}};
        return;
    }

    // Join, Cut and Intersect act on the most recent body.
    Body& target = bodies.back();
    Expected<TopoDS_Shape> result = occt::ApplyBoolean(target.shape, *tool, f.operation);
    if (!result) {
        status = {FeatureState::Error, result.GetError().message};
        return;
    }
    target.shape = *result;
    status = {FeatureState::Ok, {}};
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

/// The reflection of a mirror, about an origin plane or a work plane.
Expected<gp_Trsf> MirrorPlacement(const doc::MirrorFeature& f, const doc::Document& doc) {
    std::optional<Frame> plane = doc::origin::PlaneFrame(f.plane);
    if (!plane)
        if (const auto* wp = doc.FindAs<doc::WorkPlaneFeature>(f.plane); wp && wp->frame) plane = wp->frame;
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

/// A sketch on a work plane moves with it (sketch coordinates stay the same).
void FollowWorkPlane(doc::SketchFeature& sk, const doc::Document& doc) {
    if (const auto* wp = doc.FindAs<doc::WorkPlaneFeature>(sk.placement.support); wp && wp->frame)
        sk.placement.frame = *wp->frame;
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
                FollowWorkPlane(*sk, doc);
                Expected<occt::SketchProfile> profile = occt::BuildProfile(*sk);
                if (profile) {
                    impl.sketches[sk->id] = std::move(*profile);
                } else {
                    impl.sketches.erase(sk->id);
                    sk->status = {FeatureState::Error, profile.GetError().message};
                }
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
            } else if (auto* wp = dynamic_cast<doc::WorkPlaneFeature*>(&f)) {
                PlaceWorkPlane(*wp, doc, bodies);
            } else if (auto* shell = dynamic_cast<doc::ShellFeature*>(&f)) {
                ApplyToLastBody(bodies, shell->status, "Нет тела для оболочки",
                                [&](const TopoDS_Shape& body) { return occt::MakeShell(body, *shell); });
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
