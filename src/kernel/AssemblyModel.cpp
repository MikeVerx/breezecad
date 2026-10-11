#include "kernel/AssemblyModel.h"

#include "assembly/AssemblyFile.h"
#include "core/PathText.h"
#include "document/BzcFile.h"
#include "kernel/occt/ModelImpl.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Trsf.hxx>

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

gp_Trsf ToTrsf(const Placement& p) {
    const auto& c = p.rotation.col;
    gp_Trsf t;
    t.SetValues(c[0].x, c[1].x, c[2].x, p.translation.x,   //
                c[0].y, c[1].y, c[2].y, p.translation.y,   //
                c[0].z, c[1].z, c[2].z, p.translation.z);
    return t;
}

double DistanceTo(const TopoDS_Shape& shape, const gp_Pnt& p) {
    const TopoDS_Vertex probe = BRepBuilderAPI_MakeVertex(p);
    BRepExtrema_DistShapeShape d(probe, shape);
    return d.IsDone() && d.NbSolution() > 0 ? d.Value() : 1e300;
}

/// Outward normal of a planar face.
std::optional<Vec3> PlaneNormal(const TopoDS_Face& face) {
    const BRepAdaptor_Surface s(face);
    if (s.GetType() != GeomAbs_Plane) return std::nullopt;
    Vec3 n = ToVec(s.Plane().Axis().Direction().XYZ()).Normalized();
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
        const gp_Circ circle = curve.Circle();
        GeometryRef g;
        g.kind = GeometryKind::Circle;
        g.origin = ToVec(circle.Location().XYZ());
        g.direction = ToVec(circle.Axis().Direction().XYZ()).Normalized();
        g.radius = circle.Radius();
        g.pick = pick;
        for (const TopoDS_Shape& f : faces.FindFromIndex(i))
            if (auto n = PlaneNormal(TopoDS::Face(f)); n && std::abs(n->Dot(g.direction)) > 0.999) {
                g.direction = *n;
                break;
            }
        bestDistance = d;
        best = g;
    }
    return best;
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

struct AssemblyModel::Impl {
    struct Part {
        std::filesystem::file_time_type time{};
        std::unique_ptr<LoadedPart>     loaded;
        std::string                     problem;
        bool                            reported = false;
        TopoDS_Shape                    shape;         ///< all bodies, in the part's coordinates
        TopoDS_Shape                    decorations;   ///< cosmetic threads
        std::uint64_t                   generation = 0;
    };
    std::map<std::string, Part> parts;   ///< by absolute path
    std::unordered_map<ObjectId, std::string> partOf;   ///< component -> key in `parts`
    std::unordered_map<ObjectId, Placement>   placed;   ///< where each was shown

    Model         own;
    Model*        shown = &own;
    std::uint64_t builtRevision = ~std::uint64_t{0};
    std::uint64_t partsGeneration = 0, builtGeneration = ~std::uint64_t{0};

    const Part* PartFor(ObjectId component) const {
        const auto k = partOf.find(component);
        if (k == partOf.end()) return nullptr;
        const auto p = parts.find(k->second);
        return p == parts.end() ? nullptr : &p->second;
    }
};

AssemblyModel::AssemblyModel() : myImpl(std::make_unique<Impl>()) {}
AssemblyModel::~AssemblyModel() = default;
AssemblyModel::AssemblyModel(AssemblyModel&&) noexcept = default;
AssemblyModel& AssemblyModel::operator=(AssemblyModel&&) noexcept = default;

void AssemblyModel::ReloadParts() {
    myImpl->parts.clear();
    ++myImpl->partsGeneration;
}

void AssemblyModel::Update(assembly::Assembly& a, const std::filesystem::path& assemblyFile,
                           std::vector<std::string>* problems) {
    Impl& m = *myImpl;
    std::vector<std::string> reloaded;
    m.partOf.clear();
    for (const assembly::Component& c : a.components) {
        const std::filesystem::path file = assembly::ResolvePartPath(assemblyFile, c.path);
        const std::string key = ToUtf8(file);
        m.partOf[c.id] = key;
        std::error_code ec;
        const auto time = std::filesystem::last_write_time(file, ec);
        auto it = m.parts.find(key);
        if (it != m.parts.end() && (ec || it->second.time == time)) continue;   // loaded and unchanged

        Impl::Part part;
        part.time = ec ? std::filesystem::file_time_type{} : time;
        part.generation = ++m.partsGeneration;
        if (Expected<LoadedPart> loaded = LoadPart(file)) {
            part.loaded = std::make_unique<LoadedPart>(std::move(*loaded));
            const auto& impl = part.loaded->model.GetImpl();
            BRep_Builder builder;
            TopoDS_Compound bodies, decorations;
            builder.MakeCompound(bodies);
            builder.MakeCompound(decorations);
            for (const Body& b : impl.bodies) builder.Add(bodies, b.shape);
            for (const auto& [id, d] : impl.decorations)
                if (!d.IsNull()) builder.Add(decorations, d);
            part.shape = bodies;
            part.decorations = decorations;
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

    // A part changed on disk: its faces may have moved; find the constraint
    // geometry again by where it was picked.
    if (!reloaded.empty())
        for (assembly::Constraint& c : a.constraints)
            for (GeometryRef* g : {&c.first, &c.second}) {
                const auto k = m.partOf.find(g->component);
                if (k != m.partOf.end() && std::find(reloaded.begin(), reloaded.end(), k->second) != reloaded.end())
                    Refresh(a, *g);
            }
    if (!reloaded.empty()) a.Touch();   // constraints are to be solved again

    if (a.Revision() == m.builtRevision && m.partsGeneration == m.builtGeneration) return;
    m.builtRevision = a.Revision();
    m.builtGeneration = m.partsGeneration;

    Model::Impl& shown = m.shown->EditImpl();
    shown.bodies.clear();
    shown.decorations.clear();
    m.placed.clear();
    for (const assembly::Component& c : a.components) {
        const Impl::Part* part = m.PartFor(c.id);
        m.placed[c.id] = c.placement;
        if (!c.visible || !part || part->shape.IsNull()) continue;
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
    return p && p->loaded ? &p->loaded->doc : nullptr;
}

const Model* AssemblyModel::PartModelOf(ObjectId component) const {
    const Impl::Part* p = myImpl->PartFor(component);
    return p && p->loaded ? &p->loaded->model : nullptr;
}

std::string AssemblyModel::ProblemOf(ObjectId component) const {
    const Impl::Part* p = myImpl->PartFor(component);
    return p ? p->problem : std::string{};
}

std::optional<GeometryRef> AssemblyModel::GeometryAt(const assembly::Assembly& a, ObjectId component, Vec3 worldPoint,
                                                     AssemblyPick want) const {
    const assembly::Component* c = a.FindComponent(component);
    const Impl::Part* part = myImpl->PartFor(component);
    if (!c || !part || part->shape.IsNull()) return std::nullopt;
    const Vec3 local = c->placement.Inverse().Point(worldPoint);
    std::optional<GeometryRef> g;
    try {
        if (want == AssemblyPick::Circle) {
            g = NearestCircle(part->shape, local, 1e300);
        } else if (auto face = NearestFace(part->shape, ToPnt(local), 0.5)) {
            g = FromFace(*face, local, want == AssemblyPick::Axis);
        }
    } catch (const Standard_Failure&) {
        return std::nullopt;
    }
    if (g) g->component = component;
    return g;
}

bool AssemblyModel::Refresh(const assembly::Assembly& a, GeometryRef& ref) const {
    const Impl::Part* part = myImpl->PartFor(ref.component);
    if (!a.FindComponent(ref.component) || !part || part->shape.IsNull()) return false;
    std::optional<GeometryRef> found;
    try {
        if (ref.kind == GeometryKind::Circle) {
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
                    found = GeometryRef{ref.component, GeometryKind::Plane, ref.pick - *n * (ref.pick - o).Dot(*n), *n, 0.0,
                                        ref.pick - *n * (ref.pick - o).Dot(*n)};
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
    if (ref.kind != GeometryKind::Plane && ref.direction.Length() > 0 && found->direction.Dot(ref.direction) < 0)
        found->direction = -found->direction;   // an axis keeps the sense it had
    ref = *found;
    return true;
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

std::vector<ComponentClash> AssemblyModel::Clashes() const {
    const Model& shown = *myImpl->shown;
    const auto& bodies = shown.GetImpl().bodies;
    std::vector<ComponentClash> out;
    for (const Interference& i : shown.Interferences())
        out.push_back({bodies[i.first].id, bodies[i.second].id, i.volume, i.centre});
    return out;
}

} // namespace bz::kernel
