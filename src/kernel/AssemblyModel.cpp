#include "kernel/AssemblyModel.h"

#include "assembly/AssemblyFile.h"
#include "core/PathText.h"
#include "document/BzcFile.h"
#include "document/Origin.h"
#include "document/WorkAxisFeature.h"
#include "document/WorkPlaneFeature.h"
#include "document/WorkPointFeature.h"
#include "kernel/occt/AssemblyModelImpl.h"
#include "kernel/occt/ModelImpl.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <gp_Pln.hxx>
#include <gp_Lin.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <map>
#include <unordered_map>

namespace bz::kernel {

using assembly::GeometryKind;
using assembly::GeometryRef;
using assembly::Placement;

namespace {

Vec3   ToVec(const gp_XYZ& v) { return {v.X(), v.Y(), v.Z()}; }
gp_Pnt ToPnt(Vec3 v) { return {v.x, v.y, v.z}; }


double DistanceTo(const TopoDS_Shape& shape, const gp_Pnt& p) {
    const TopoDS_Vertex probe = BRepBuilderAPI_MakeVertex(p);
    BRepExtrema_DistShapeShape d(probe, shape);
    return d.IsDone() && d.NbSolution() > 0 ? d.Value() : 1e300;
}

/// Outward normal of a planar face.
std::optional<Vec3> PlaneNormal(const TopoDS_Face& face) {
    const BRepAdaptor_Surface s(face);
    if (s.GetType() != GeomAbs_Plane) return std::nullopt;
    const gp_Pln plane = s.Plane();
    Vec3 n = ToVec(plane.Axis().Direction().XYZ()).Normalized();
    if (!plane.Position().Direct()) n = -n;   // a left-handed (mirrored) frame: X × Y is opposite its axis
    if (face.Orientation() == TopAbs_REVERSED) n = -n;
    return n;
}

/// The face of `shape` nearest `p` (the one clicked), within `tolerance`.
std::optional<TopoDS_Face> NearestFace(const TopoDS_Shape& shape, const gp_Pnt& p, double tolerance) {
    std::optional<TopoDS_Face> best;
    double bestDistance = tolerance;
    for (TopExp_Explorer e(shape, TopAbs_FACE); e.More(); e.Next()) {
        const TopoDS_Face face = TopoDS::Face(e.Current());
        Bnd_Box box;
        BRepBndLib::Add(face, box);
        box.Enlarge(bestDistance);
        if (box.IsOut(p)) continue;
        const double d = DistanceTo(face, p);
        if (d < bestDistance) {
            bestDistance = d;
            best = face;
        }
    }
    return best;
}

std::optional<GeometryRef> FromFace(const TopoDS_Face& face, Vec3 pick, bool axisOnly) {
    GeometryRef g;
    g.pick = pick;
    const BRepAdaptor_Surface s(face);
    switch (s.GetType()) {
        case GeomAbs_Plane: {
            if (axisOnly) return std::nullopt;
            const Vec3 n = *PlaneNormal(face);
            g.kind = GeometryKind::Plane;
            g.direction = n;
            // The foot of the pick on the plane: exact even if the click was a hair off.
            const Vec3 o = ToVec(s.Plane().Location().XYZ());
            g.origin = pick - n * (pick - o).Dot(n);
            return g;
        }
        case GeomAbs_Cylinder: {
            const gp_Ax1 axis = s.Cylinder().Axis();
            g.kind = axisOnly ? GeometryKind::Axis : GeometryKind::Cylinder;
            g.direction = ToVec(axis.Direction().XYZ()).Normalized();
            const Vec3 o = ToVec(axis.Location().XYZ());
            g.origin = o + g.direction * (pick - o).Dot(g.direction);   // axis point level with the pick
            g.radius = s.Cylinder().Radius();
            return g;
        }
        case GeomAbs_Cone: {
            const gp_Ax1 axis = s.Cone().Axis();
            g.kind = GeometryKind::Axis;
            g.direction = ToVec(axis.Direction().XYZ()).Normalized();
            g.origin = ToVec(axis.Location().XYZ());
            return g;
        }
        case GeomAbs_Torus: {
            const gp_Ax1 axis = s.Torus().Axis();
            g.kind = GeometryKind::Axis;
            g.direction = ToVec(axis.Direction().XYZ()).Normalized();
            g.origin = ToVec(axis.Location().XYZ());
            return g;
        }
        case GeomAbs_Sphere: {
            if (axisOnly) return std::nullopt;
            g.kind = GeometryKind::Sphere;
            g.origin = ToVec(s.Sphere().Location().XYZ());
            g.radius = s.Sphere().Radius();
            return g;
        }
        default: return std::nullopt;
    }
}

/// The circular edge of `shape` nearest `p`, oriented by the flat face it
/// bounds (outward), so "Вставка" puts faces against each other.
GeometryRef CircleOf(const BRepAdaptor_Curve& curve, const TopTools_ListOfShape& faces, Vec3 pick) {
    const gp_Circ circle = curve.Circle();
    GeometryRef g;
    g.kind = GeometryKind::Circle;
    g.origin = ToVec(circle.Location().XYZ());
    g.direction = ToVec(circle.Axis().Direction().XYZ()).Normalized();
    g.radius = circle.Radius();
    g.pick = pick;
    for (const TopoDS_Shape& f : faces)
        if (auto n = PlaneNormal(TopoDS::Face(f)); n && std::abs(n->Dot(g.direction)) > 0.999) {
            g.direction = *n;
            break;
        }
    return g;
}

/// A corner of the part: a vertex where a real edge ends (not only where a
/// round edge or a seam closes on itself — that is no corner a user sees).
bool IsCorner(const TopoDS_Vertex& vertex, const TopTools_IndexedDataMapOfShapeListOfShape& edgesOf,
              const TopTools_IndexedDataMapOfShapeListOfShape& facesOf) {
    const int i = edgesOf.FindIndex(vertex);
    if (i == 0) return false;
    for (const TopoDS_Shape& e : edgesOf.FindFromIndex(i)) {
        const TopoDS_Edge edge = TopoDS::Edge(e);
        const int k = facesOf.FindIndex(edge);
        if (k == 0 || IsSeam(edge, facesOf.FindFromIndex(k))) continue;
        if (BRep_Tool::IsClosed(edge)) continue;   // a full circle: its start is not a corner
        return true;
    }
    return false;
}

/// The vertex of `shape` nearest `p` within `tolerance`, as a point.
std::optional<GeometryRef> NearestVertex(const TopoDS_Shape& shape, Vec3 p, double tolerance) {
    TopTools_IndexedDataMapOfShapeListOfShape edgesOf, facesOf;
    TopExp::MapShapesAndAncestors(shape, TopAbs_VERTEX, TopAbs_EDGE, edgesOf);
    TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, facesOf);
    std::optional<GeometryRef> best;
    double bestDistance = tolerance;
    for (int i = 1; i <= edgesOf.Extent(); ++i) {
        const TopoDS_Vertex vertex = TopoDS::Vertex(edgesOf.FindKey(i));
        const Vec3 v = ToVec(BRep_Tool::Pnt(vertex).XYZ());
        if ((v - p).Length() >= bestDistance || !IsCorner(vertex, edgesOf, facesOf)) continue;
        const double d = (v - p).Length();
        if (d >= bestDistance) continue;
        bestDistance = d;
        GeometryRef g;
        g.kind = GeometryKind::Point;
        g.source = assembly::PickSource::Vertex;
        g.origin = v;
        g.pick = v;
        best = g;
    }
    return best;
}

/// The edge of `shape` nearest `p` within `tolerance`: a straight one as a
/// line (its middle as the point), a round one as a circle. `straightOnly`:
/// lines only. `along`: when set, only lines parallel to it (finding a
/// picked edge again).
std::optional<GeometryRef> NearestEdge(const TopoDS_Shape& shape, Vec3 p, double tolerance, bool straightOnly,
                                       std::optional<Vec3> along = std::nullopt) {
    TopTools_IndexedDataMapOfShapeListOfShape faces;
    TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, faces);
    std::optional<GeometryRef> best;
    double bestDistance = tolerance;
    const gp_Pnt at = ToPnt(p);
    for (int i = 1; i <= faces.Extent(); ++i) {
        const TopoDS_Edge edge = TopoDS::Edge(faces.FindKey(i));
        if (IsSeam(edge, faces.FindFromIndex(i))) continue;
        const BRepAdaptor_Curve curve(edge);
        const GeomAbs_CurveType type = curve.GetType();
        if (type != GeomAbs_Line && (straightOnly || type != GeomAbs_Circle)) continue;
        if (along && (type != GeomAbs_Line || std::abs(ToVec(curve.Line().Direction().XYZ()).Dot(*along)) < 0.999)) continue;
        Bnd_Box box;
        BRepBndLib::Add(edge, box);
        box.Enlarge(bestDistance);
        if (box.IsOut(at)) continue;
        const double d = DistanceTo(edge, at);
        if (d >= bestDistance) continue;
        bestDistance = d;
        if (type == GeomAbs_Circle) {
            GeometryRef g = CircleOf(curve, faces.FindFromIndex(i), p);
            g.source = assembly::PickSource::Edge;
            best = g;
            continue;
        }
        GeometryRef g;
        g.kind = GeometryKind::Axis;
        g.source = assembly::PickSource::Edge;
        g.direction = ToVec(curve.Line().Direction().XYZ()).Normalized();
        const double mid = (curve.FirstParameter() + curve.LastParameter()) / 2;
        g.origin = ToVec(curve.Value(mid).XYZ());   // the middle: found again by it
        g.pick = g.origin;
        best = g;
    }
    return best;
}

std::optional<GeometryRef> NearestCircle(const TopoDS_Shape& shape, Vec3 pick, double tolerance) {
    TopTools_IndexedDataMapOfShapeListOfShape faces;
    TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, faces);
    const gp_Pnt p = ToPnt(pick);
    std::optional<GeometryRef> best;
    double bestDistance = tolerance;
    for (int i = 1; i <= faces.Extent(); ++i) {
        const TopoDS_Edge edge = TopoDS::Edge(faces.FindKey(i));
        const BRepAdaptor_Curve curve(edge);
        if (curve.GetType() != GeomAbs_Circle) continue;
        const double d = DistanceTo(edge, p);
        if (d >= bestDistance) continue;
        bestDistance = d;
        best = CircleOf(curve, faces.FindFromIndex(i), pick);
    }
    return best;
}

/// Every full circular edge of `shape`, oriented as NearestCircle does.
std::vector<GeometryRef> AllCircles(const TopoDS_Shape& shape) {
    TopTools_IndexedDataMapOfShapeListOfShape faces;
    TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, faces);
    std::vector<GeometryRef> out;
    for (int i = 1; i <= faces.Extent(); ++i) {
        const TopoDS_Edge edge = TopoDS::Edge(faces.FindKey(i));
        const BRepAdaptor_Curve curve(edge);
        if (curve.GetType() != GeomAbs_Circle) continue;
        // Arcs (a slot's end, a rounded corner) are not holes.
        if (std::abs(curve.LastParameter() - curve.FirstParameter() - 2 * kPi) > 1e-6) continue;
        GeometryRef g = CircleOf(curve, faces.FindFromIndex(i), {});
        const Vec3 side{g.direction.y, -g.direction.x, 0};   // in the circle's plane
        g.pick = g.origin + (side.Length() > 1e-9 ? side.Normalized() : Vec3{1, 0, 0}) * g.radius;
        out.push_back(g);
    }
    return out;
}

} // namespace

// --- loading parts -----------------------------------------------------------

Expected<LoadedPart> LoadPart(const std::filesystem::path& file) {
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return Error{std::format("Файл не найден: {}", ToUtf8(file))};
    Expected<doc::Document> d = doc::LoadBzc(file);
    if (!d) return Error{std::format("{}: {}", ToUtf8(file.filename()), d.GetError().message)};
    LoadedPart part{std::move(*d), Model{}};
    part.model.Rebuild(part.doc);
    return part;
}

AssemblyModel::AssemblyModel() : myImpl(std::make_unique<Impl>()) {}
AssemblyModel::~AssemblyModel() = default;
AssemblyModel::AssemblyModel(AssemblyModel&&) noexcept = default;
AssemblyModel& AssemblyModel::operator=(AssemblyModel&&) noexcept = default;

void AssemblyModel::ReloadParts() {
    myImpl->parts.clear();
    ++myImpl->partsGeneration;
}

void AssemblyModel::UsePartInMemory(const std::filesystem::path& file, const doc::Document* doc, const Model* model) {
    myImpl->inMemory[ToUtf8(file)] = {doc, model};
}

void AssemblyModel::ForgetPartInMemory(const std::filesystem::path& file) {
    const std::string key = ToUtf8(file);
    myImpl->inMemory.erase(key);
    myImpl->parts.erase(key);   // read the file again: it may have been saved meanwhile
    ++myImpl->partsGeneration;
}

bool AssemblyModel::IsPartInMemory(const std::filesystem::path& file) const { return myImpl->inMemory.contains(ToUtf8(file)); }

void AssemblyModel::SetDiskCheckInterval(double seconds) { myImpl->diskInterval = seconds; }

void AssemblyModel::Update(assembly::Assembly& a, const std::filesystem::path& assemblyFile,
                           std::vector<std::string>* problems) {
    Impl& m = *myImpl;
    std::vector<std::string> reloaded;
    m.partOf.clear();
    // Files on disk change rarely: looking at every one each frame costs a
    // system call per component per frame. They are looked at every so
    // often; a component whose file is new to us, at once.
    const auto now = std::chrono::steady_clock::now();
    const bool lookAtDisk = m.diskInterval <= 0 ||
                            std::chrono::duration<double>(now - m.lastDiskCheck).count() >= m.diskInterval;
    if (lookAtDisk) m.lastDiskCheck = now;
    for (const assembly::Component& c : a.components) {
        const std::filesystem::path file = assembly::ResolvePartPath(assemblyFile, c.path);
        const std::string key = ToUtf8(file);
        m.partOf[c.id] = key;
        auto it = m.parts.find(key);

        // A part edited in memory: shown from its model whenever that changed.
        if (const auto mem = m.inMemory.find(key); mem != m.inMemory.end()) {
            const auto [doc, model] = mem->second;
            const std::uint64_t revision = model->Revision();
            if (it != m.parts.end() && it->second.memoryModel == model && it->second.memoryRevision == revision) continue;
            Impl::Part part;
            part.generation = ++m.partsGeneration;
            part.memoryDoc = doc;
            part.memoryModel = model;
            part.memoryRevision = revision;
            part.TakeShape(*model);
            if (it != m.parts.end()) reloaded.push_back(key);
            m.parts[key] = std::move(part);
            continue;
        }

        if (it != m.parts.end() && !lookAtDisk) continue;
        std::error_code ec;
        const auto time = std::filesystem::last_write_time(file, ec);
        if (it != m.parts.end() && (ec || it->second.time == time)) continue;   // loaded and unchanged

        Impl::Part part;
        part.time = ec ? std::filesystem::file_time_type{} : time;
        part.generation = ++m.partsGeneration;
        if (Expected<LoadedPart> loaded = LoadPart(file)) {
            part.loaded = std::make_unique<LoadedPart>(std::move(*loaded));
            part.TakeShape(part.loaded->model);
        } else {
            part.problem = loaded.GetError().message;
        }
        if (it != m.parts.end()) reloaded.push_back(key);
        m.parts[key] = std::move(part);
    }
    for (auto& [key, part] : m.parts)
        if (!part.problem.empty() && !part.reported) {
            if (problems) problems->push_back(part.problem);
            part.reported = true;
        }

    // A part changed (on disk, or rebuilt in memory): its faces may have
    // moved; find the constraint geometry again by where it was picked. The
    // assembly changes (its constraints are solved again, it needs saving)
    // only when some of it really moved — a part merely taken into memory
    // for editing, or rebuilt the same, leaves it as it was.
    bool moved = false;
    if (!reloaded.empty())
        for (assembly::Constraint& c : a.constraints)
            for (GeometryRef* g : {&c.first, &c.second, &c.third}) {
                if (g == &c.third && c.type != assembly::ConstraintType::Symmetry) continue;
                const auto k = m.partOf.find(g->component);
                if (k == m.partOf.end() || std::find(reloaded.begin(), reloaded.end(), k->second) == reloaded.end()) continue;
                const GeometryRef was = *g;
                if (!Refresh(a, *g)) continue;
                const auto differs = [](Vec3 p, Vec3 q) { return (p - q).Length() > 1e-9; };
                if (differs(was.origin, g->origin) || differs(was.direction, g->direction) || std::abs(was.radius - g->radius) > 1e-9)
                    moved = true;
            }
    if (moved) a.Touch();   // constraints are to be solved again

    if (a.Revision() == m.builtRevision && m.partsGeneration == m.builtGeneration) return;
    m.builtRevision = a.Revision();
    m.builtGeneration = m.partsGeneration;

    Model::Impl& shown = m.shown->EditImpl();
    // A component that neither moved nor changed keeps the very shape it
    // was shown with: the view then keeps its presentation (Viewport::Sync
    // matches shapes exactly, location included). Made anew, every body of
    // the assembly was presented again — meshes, selection — at every
    // change anywhere (a constraint added, a drag step): O(all parts).
    struct Shown {
        TopoDS_Shape body, decorations, source;
    };
    std::unordered_map<ObjectId, Shown> before;
    for (const Body& b : shown.bodies) before[b.id].body = b.shape;
    for (const auto& [id, d] : shown.decorations) before[id].decorations = d;
    for (auto& [id, was] : before)
        if (const auto it = m.sourceShown.find(id); it != m.sourceShown.end()) was.source = it->second;
    const std::unordered_map<ObjectId, assembly::Placement> placedBefore = std::move(m.placed);
    shown.bodies.clear();
    shown.decorations.clear();
    m.placed.clear();
    m.sourceShown.clear();
    const auto same = [](const assembly::Placement& p, const assembly::Placement& q) {
        return p.translation.x == q.translation.x && p.translation.y == q.translation.y && p.translation.z == q.translation.z &&
               p.rotation.col[0].x == q.rotation.col[0].x && p.rotation.col[0].y == q.rotation.col[0].y &&
               p.rotation.col[0].z == q.rotation.col[0].z && p.rotation.col[1].x == q.rotation.col[1].x &&
               p.rotation.col[1].y == q.rotation.col[1].y && p.rotation.col[1].z == q.rotation.col[1].z &&
               p.rotation.col[2].x == q.rotation.col[2].x && p.rotation.col[2].y == q.rotation.col[2].y &&
               p.rotation.col[2].z == q.rotation.col[2].z;
    };
    for (const assembly::Component& c : a.components) {
        const Impl::Part* part = m.PartFor(c.id);
        m.placed[c.id] = c.placement;
        if (!c.visible || !part || part->shape.IsNull()) continue;
        m.sourceShown[c.id] = part->shape;
        const auto was = before.find(c.id);
        const auto wasAt = placedBefore.find(c.id);
        if (was != before.end() && !was->second.body.IsNull() && wasAt != placedBefore.end() && same(wasAt->second, c.placement) &&
            was->second.source.IsEqual(part->shape)) {
            shown.bodies.push_back({c.id, c.name, was->second.body});
            if (!was->second.decorations.IsNull()) shown.decorations[c.id] = was->second.decorations;
            continue;
        }
        const TopLoc_Location where(ToTrsf(c.placement));
        shown.bodies.push_back({c.id, c.name, part->shape.Moved(where)});
        if (!part->decorations.IsNull()) shown.decorations[c.id] = part->decorations.Moved(where);
    }
    shown.bodiesSet = true;
    shown.infos.clear();
    for (const Body& b : shown.bodies) shown.infos.push_back({b.id, b.name});
    ++shown.revision;
}

const Model& AssemblyModel::Shown() const { return *myImpl->shown; }

void AssemblyModel::ShowIn(Model& target) {
    myImpl->shown = &target;
    myImpl->builtRevision = ~std::uint64_t{0};   // fill it at the next Update
}

const doc::Document* AssemblyModel::PartOf(ObjectId component) const {
    const Impl::Part* p = myImpl->PartFor(component);
    return p ? p->Doc() : nullptr;
}

const Model* AssemblyModel::PartModelOf(ObjectId component) const {
    const Impl::Part* p = myImpl->PartFor(component);
    return p ? p->PartModel() : nullptr;
}

std::string AssemblyModel::ProblemOf(ObjectId component) const {
    const Impl::Part* p = myImpl->PartFor(component);
    return p ? p->problem : std::string{};
}

std::optional<GeometryRef> AssemblyModel::GeometryAt(const assembly::Assembly& a, ObjectId component, Vec3 worldPoint,
                                                     AssemblyPick want, double tolerance) const {
    const assembly::Component* c = a.FindComponent(component);
    const Impl::Part* part = myImpl->PartFor(component);
    if (!c || !part || part->shape.IsNull()) return std::nullopt;
    const Vec3 local = c->placement.Inverse().Point(worldPoint);
    std::optional<GeometryRef> g;
    try {
        if (want == AssemblyPick::Circle) {
            g = NearestCircle(part->shape, local, 1e300);
            if (g) g->source = assembly::PickSource::Edge;
        } else {
            // Inventor picks what is nearest the cursor: a corner, then an
            // edge, then the face itself.
            if (want == AssemblyPick::Any && tolerance > 0) g = NearestVertex(part->shape, local, tolerance);
            if (!g && (want == AssemblyPick::Any || want == AssemblyPick::Line) && tolerance > 0)
                g = NearestEdge(part->shape, local, tolerance, want == AssemblyPick::Line);
            if (!g)
                if (auto face = NearestFace(part->shape, ToPnt(local), 0.5))
                    g = FromFace(*face, local, want == AssemblyPick::Axis || want == AssemblyPick::Line);
        }
    } catch (const Standard_Failure&) {
        return std::nullopt;
    }
    if (g) g->component = component;
    return g;
}

std::optional<GeometryRef> AssemblyModel::ElementGeometry(ObjectId component, ObjectId element) const {
    if (component == assembly::kAssemblyOrigin) return assembly::OriginGeometry(component, element);
    const doc::Document* part = PartOf(component);
    if (!part) return std::nullopt;
    if (doc::origin::IsOriginElement(element)) return assembly::OriginGeometry(component, element);
    GeometryRef g;
    g.component = component;
    g.source = assembly::PickSource::Element;
    g.element = element;
    if (const auto* wp = part->FindAs<doc::WorkPlaneFeature>(element); wp && wp->frame && !wp->suppressed) {
        g.kind = GeometryKind::Plane;
        g.origin = wp->frame->origin;
        g.direction = wp->frame->normal.Normalized();
    } else if (const auto* wa = part->FindAs<doc::WorkAxisFeature>(element); wa && wa->line && !wa->suppressed) {
        g.kind = GeometryKind::Axis;
        g.origin = wa->line->first;
        g.direction = wa->line->second.Normalized();
    } else if (const auto* pt = part->FindAs<doc::WorkPointFeature>(element); pt && pt->position && !pt->suppressed) {
        g.kind = GeometryKind::Point;
        g.origin = *pt->position;
    } else {
        return std::nullopt;
    }
    g.pick = g.origin;
    return g;
}

std::vector<AssemblyModel::NamedElement> AssemblyModel::ElementsOf(ObjectId component) const {
    namespace o = doc::origin;
    std::vector<NamedElement> out;
    for (ObjectId id : {o::kPlaneXY, o::kPlaneXZ, o::kPlaneYZ, o::kAxisX, o::kAxisY, o::kAxisZ, o::kPoint})
        out.push_back({id, std::string(assembly::OriginElementTitle(id)), assembly::OriginGeometry(component, id)->kind});
    if (component == assembly::kAssemblyOrigin) return out;
    const doc::Document* part = PartOf(component);
    if (!part) return out;
    for (const auto& f : part->Features())
        if (const auto g = ElementGeometry(component, f->id))
            out.push_back({f->id, f->name, g->kind});
    return out;
}

bool AssemblyModel::Refresh(const assembly::Assembly& a, GeometryRef& ref) const {
    if (ref.OnOrigin()) return true;   // never changes
    if (ref.source == assembly::PickSource::Element) {
        // Named: taken again from the part as it is now.
        std::optional<GeometryRef> found = ElementGeometry(ref.component, ref.element);
        if (!found || found->kind != ref.kind) return false;
        found->reference = ref.reference;
        if (ref.direction.Length() > 0 && found->direction.Dot(ref.direction) < 0 && ref.kind != GeometryKind::Plane)
            found->direction = -found->direction;
        ref = *found;
        return true;
    }
    const Impl::Part* part = myImpl->PartFor(ref.component);
    if (!a.FindComponent(ref.component) || !part || part->shape.IsNull()) return false;
    std::optional<GeometryRef> found;
    try {
        if (ref.source == assembly::PickSource::Vertex) {
            found = NearestVertex(part->shape, ref.origin, 1e300);
        } else if (ref.source == assembly::PickSource::Edge && ref.kind == GeometryKind::Axis) {
            // The straight edge along the same direction nearest where it was.
            found = NearestEdge(part->shape, ref.pick, 1e300, true, ref.direction.Normalized());
        } else if (ref.kind == GeometryKind::Circle) {
            // The circle of the same size nearest where it was.
            found = NearestCircle(part->shape, ref.origin, 1e300);
            if (found && std::abs(found->radius - ref.radius) > 1e-6) found.reset();
        } else if (ref.kind == GeometryKind::Plane) {
            // As FaceReference: the flat face with the same normal whose plane
            // is nearest the old one, so a face follows the part changing size.
            double best = 1e300;
            for (TopExp_Explorer e(part->shape, TopAbs_FACE); e.More(); e.Next()) {
                const TopoDS_Face face = TopoDS::Face(e.Current());
                const auto n = PlaneNormal(face);
                if (!n || n->Dot(ref.direction) < 0.999) continue;
                const BRepAdaptor_Surface s(face);
                const double offset = std::abs((ToVec(s.Plane().Location().XYZ()) - ref.origin).Dot(*n));
                const double d = offset + 1e-6 * DistanceTo(face, ToPnt(ref.pick));   // ties: the face nearest the pick
                if (d < best) {
                    best = d;
                    const Vec3 o = ToVec(s.Plane().Location().XYZ());
                    GeometryRef g;
                    g.component = ref.component;
                    g.kind = GeometryKind::Plane;
                    g.origin = ref.pick - *n * (ref.pick - o).Dot(*n);
                    g.direction = *n;
                    g.pick = g.origin;
                    found = g;
                }
            }
        } else if (auto face = NearestFace(part->shape, ToPnt(ref.pick), 1e300)) {
            found = FromFace(*face, ref.pick, ref.kind == GeometryKind::Axis);
        }
    } catch (const Standard_Failure&) {
        return false;
    }
    if (!found || found->kind != ref.kind) return false;
    found->component = ref.component;
    found->source = ref.source;
    found->reference = ref.reference;
    if (ref.kind != GeometryKind::Plane && ref.direction.Length() > 0 && found->direction.Dot(ref.direction) < 0)
        found->direction = -found->direction;   // an axis keeps the sense it had
    ref = *found;
    return true;
}

std::vector<GeometryRef> AssemblyModel::SameHoles(const assembly::Assembly& a, const GeometryRef& rim) const {
    std::vector<GeometryRef> out;
    const Impl::Part* part = myImpl->PartFor(rim.component);
    if (!a.FindComponent(rim.component) || !part || part->shape.IsNull() || rim.kind != GeometryKind::Circle) return out;
    const Vec3 n = rim.direction.Normalized();
    try {
        for (GeometryRef g : AllCircles(part->shape)) {
            if (std::abs(g.radius - rim.radius) > 1e-6 || g.direction.Dot(n) < 0.999 ||
                std::abs((g.origin - rim.origin).Dot(n)) > 1e-4)
                continue;
            if (std::any_of(out.begin(), out.end(), [&](const GeometryRef& o) { return (o.origin - g.origin).Length() < 1e-4; }))
                continue;
            g.component = rim.component;
            g.direction = rim.direction;
            out.push_back(g);
        }
    } catch (const Standard_Failure&) {
        return {};
    }
    // The picked one first, the rest by distance from it (a stable order).
    std::sort(out.begin(), out.end(), [&](const GeometryRef& x, const GeometryRef& y) {
        const double dx = (x.origin - rim.origin).Length(), dy = (y.origin - rim.origin).Length();
        if (std::abs(dx - dy) > 1e-6) return dx < dy;
        if (std::abs(x.origin.x - y.origin.x) > 1e-6) return x.origin.x < y.origin.x;
        if (std::abs(x.origin.y - y.origin.y) > 1e-6) return x.origin.y < y.origin.y;
        return x.origin.z < y.origin.z;
    });
    return out;
}

std::optional<Bounds> AssemblyModel::ComponentBounds(ObjectId component) const {
    for (const Body& b : myImpl->shown->GetImpl().bodies)
        if (b.id == component) {
            Bnd_Box box;
            BRepBndLib::Add(b.shape, box);
            if (box.IsVoid()) return std::nullopt;
            double x0, y0, z0, x1, y1, z1;
            box.Get(x0, y0, z0, x1, y1, z1);
            return Bounds{{x0, y0, z0}, {x1, y1, z1}};
        }
    return std::nullopt;
}

std::optional<HoleStack> AssemblyModel::StackAt(const assembly::Assembly& a, const GeometryRef& rim) const {
    const assembly::Component* c = a.FindComponent(rim.component);
    if (!c || rim.kind != GeometryKind::Circle) return std::nullopt;
    const Vec3 o = c->placement.Point(rim.origin);
    const Vec3 n = c->placement.Direction(rim.direction).Normalized();
    const Vec3 u = (std::abs(n.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0}).Cross(n).Normalized();
    const Vec3 v = n.Cross(u);
    const auto& bodies = myImpl->shown->GetImpl().bodies;
    constexpr double kStart = 1.0, kReach = 5000.0, kGap = 0.05;

    struct Run {
        double   end = 0;
        ObjectId last;
    };
    // Material along one line parallel to the axis, from kStart outside the rim inwards.
    const auto along = [&](Vec3 side) -> std::optional<Run> {
        const Vec3 p0 = o + side + n * kStart;
        const gp_Lin line(ToPnt(p0), gp_Dir(-n.x, -n.y, -n.z));
        struct Span {
            double t0, t1;
            ObjectId id;
        };
        std::vector<Span> spans;
        for (const Body& b : bodies) {
            IntCurvesFace_ShapeIntersector hits;
            hits.Load(b.shape, 1e-6);
            hits.Perform(line, 0, kReach);
            std::vector<double> ts;
            for (int i = 1; i <= hits.NbPnt(); ++i) ts.push_back(hits.WParameter(i));
            std::ranges::sort(ts);
            for (std::size_t i = 0; i + 1 < ts.size(); ++i) {
                if (ts[i + 1] - ts[i] < 1e-6) continue;
                const double mid = (ts[i] + ts[i + 1]) / 2;
                BRepClass3d_SolidClassifier inside(b.shape, ToPnt(p0 - n * mid), 1e-6);
                if (inside.State() == TopAbs_IN) spans.push_back({ts[i], ts[i + 1], b.id});
            }
        }
        std::ranges::sort(spans, {}, &Span::t0);
        Run run{kStart, {}};
        for (const Span& s : spans) {
            if (s.t0 > run.end + kGap) break;
            if (s.t1 > run.end) run = {s.t1, s.id};
        }
        if (!run.last) return std::nullopt;
        run.end -= kStart;
        return run;
    };

    // Four lines around the hole; the stack is what most of them agree on
    // (one may run off the edge of a part or into a neighbouring feature).
    std::vector<Run> runs;
    try {
        const double rho = rim.radius + 0.4;
        for (const Vec3& side : {u * rho, v * rho, u * -rho, v * -rho})
            if (auto r = along(side)) runs.push_back(*r);
    } catch (const Standard_Failure&) {
        return std::nullopt;
    }
    if (runs.empty()) return std::nullopt;
    const Run* best = &runs.front();
    int bestVotes = 0;
    for (const Run& r : runs) {
        const int votes = int(std::ranges::count_if(runs, [&](const Run& q) { return std::abs(q.end - r.end) < 0.01; }));
        if (votes > bestVotes || (votes == bestVotes && r.end > best->end)) best = &r, bestVotes = votes;
    }
    HoleStack out{best->end, best->last, {}};
    // The rim on the far side: a circle of the last component there, facing away.
    const Vec3 farPoint = o - n * out.thickness + u * rim.radius;
    if (auto g = GeometryAt(a, out.last, farPoint, AssemblyPick::Circle)) {
        const assembly::Component* fc = a.FindComponent(out.last);
        const Vec3 centre = fc->placement.Point(g->origin), dir = fc->placement.Direction(g->direction);
        if (dir.Dot(n) < -0.999 && (centre - (o - n * out.thickness)).Length() < 0.05) out.farRim = *g;
    }
    return out;
}

std::optional<Model> AssemblyModel::PlacedModelOf(ObjectId component, const Placement& at) const {
    const Model* part = PartModelOf(component);
    if (!part || part->GetImpl().bodies.empty()) return std::nullopt;
    const TopLoc_Location where(ToTrsf(at));
    Model out;
    Model::Impl& impl = out.EditImpl();
    for (const Body& b : part->GetImpl().bodies) {
        impl.bodies.push_back({b.id, b.name, b.shape.Moved(where)});
        impl.infos.push_back({b.id, b.name});
    }
    impl.bodiesSet = true;
    ++impl.revision;
    return out;
}

Model AssemblyModel::ContextOf(const assembly::Assembly& a, ObjectId edited) const {
    Model out;
    Model::Impl& impl = out.EditImpl();
    const assembly::Component* self = a.FindComponent(edited);
    const Placement toEdited = self ? self->placement.Inverse() : Placement{};
    for (const assembly::Component& c : a.components) {
        if (c.id == edited || !c.visible) continue;
        const Impl::Part* part = myImpl->PartFor(c.id);
        if (!part || part->shape.IsNull()) continue;
        const TopLoc_Location where(ToTrsf(toEdited * c.placement));
        impl.bodies.push_back({c.id, c.name, part->shape.Moved(where)});
        impl.infos.push_back({c.id, c.name});
    }
    impl.bodiesSet = true;
    ++impl.revision;
    return out;
}

std::uint64_t AssemblyModel::PartsGeneration() const { return myImpl->partsGeneration; }

std::vector<ComponentClash> AssemblyModel::Clashes() const {
    const Model& shown = *myImpl->shown;
    const auto& bodies = shown.GetImpl().bodies;
    std::vector<ComponentClash> out;
    for (const Interference& i : shown.Interferences())
        out.push_back({bodies[i.first].id, bodies[i.second].id, i.volume, i.centre});
    return out;
}

} // namespace bz::kernel
