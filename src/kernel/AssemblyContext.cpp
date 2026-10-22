// Projections across the parts of an assembly and their adaptivity (see
// kernel/ContextProjection.h), and the AssemblyModel calls they rest on.
#include "kernel/ContextProjection.h"

#include "document/Recompute.h"
#include "document/SketchFeature.h"
#include "kernel/occt/AssemblyModelImpl.h"
#include "kernel/occt/EdgeMatch.h"
#include "kernel/occt/Project.h"

#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Tool.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <bit>
#include <cmath>
#include <unordered_set>
#include <vector>

namespace bz::kernel {
namespace {

/// Where the part of `source` stands seen from the part of `target`.
assembly::Placement Relative(const assembly::Assembly& a, ObjectId target, ObjectId source) {
    return a.PlacementOf(target).Inverse() * a.PlacementOf(source);
}

/// Mixes a number into a hash (FNV-like, good enough to tell changes).
void Mix(std::uint64_t& h, std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
}
void Mix(std::uint64_t& h, double v) {
    // Rounded to a nanometre: noise below that is not a change.
    Mix(h, static_cast<std::uint64_t>(static_cast<std::int64_t>(std::llround(v * 1e6))));
}
void Mix(std::uint64_t& h, Vec3 v) {
    Mix(h, v.x);
    Mix(h, v.y);
    Mix(h, v.z);
}

bool Matches(const doc::ContextProjection& p, std::string_view assembly) { return p.adaptive && p.assembly == assembly; }

} // namespace

// --- AssemblyModel ----------------------------------------------------------------

std::uint64_t AssemblyModel::PartGenerationOf(ObjectId component) const {
    const Impl::Part* p = myImpl->PartFor(component);
    return p ? p->generation : 0;
}

std::optional<doc::EdgeReference> AssemblyModel::SourceEdge(const assembly::Assembly& a, ObjectId target, ObjectId source,
                                                             const doc::EdgeReference& inTarget) const {
    const Impl::Part* part = myImpl->PartFor(source);
    if (!a.FindComponent(source) || !a.FindComponent(target) || !part || part->shape.IsNull()) return std::nullopt;
    try {
        // Found on the source as it stands around the target, then described
        // on the source in its own coordinates.
        const TopLoc_Location where(ToTrsf(Relative(a, target, source)));
        const TopoDS_Shape placed = part->shape.Moved(where);
        const std::optional<TopoDS_Edge> edge = occt::ResolveEdges({inTarget}, placed).front();
        if (!edge) return std::nullopt;
        const TopoDS_Edge own = TopoDS::Edge(edge->Moved(where.Inverted()));
        return occt::DescribeEdge(own, part->shape);
    } catch (const Standard_Failure&) {
        return std::nullopt;
    }
}

std::optional<doc::EdgeReference> AssemblyModel::EdgeNear(const assembly::Assembly& a, ObjectId component, Vec3 worldPoint,
                                                           double tolerance) const {
    const Impl::Part* part = myImpl->PartFor(component);
    const assembly::Component* c = a.FindComponent(component);
    if (!c || !part || part->shape.IsNull()) return std::nullopt;
    const Vec3 local = c->placement.Inverse().Point(worldPoint);
    try {
        const TopoDS_Vertex probe = BRepBuilderAPI_MakeVertex(gp_Pnt(local.x, local.y, local.z));
        std::optional<TopoDS_Edge> best;
        double bestDistance = tolerance;
        TopTools_IndexedDataMapOfShapeListOfShape facesOf;
        TopExp::MapShapesAndAncestors(part->shape, TopAbs_EDGE, TopAbs_FACE, facesOf);
        for (int i = 1; i <= facesOf.Extent(); ++i) {
            const TopoDS_Edge edge = TopoDS::Edge(facesOf.FindKey(i));
            if (IsSeam(edge, facesOf.FindFromIndex(i))) continue;   // not an edge anyone sees
            BRepExtrema_DistShapeShape d(probe, edge);
            if (!d.IsDone() || d.NbSolution() == 0 || d.Value() >= bestDistance) continue;
            bestDistance = d.Value();
            best = edge;
        }
        if (!best) return std::nullopt;
        return occt::DescribeEdge(*best, part->shape);
    } catch (const Standard_Failure&) {
        return std::nullopt;
    }
}

Expected<ProjectedCurve> AssemblyModel::ProjectSourceEdge(const assembly::Assembly& a, ObjectId target,
                                                          const doc::ContextProjection& link, const Frame& plane) const {
    const Impl::Part* part = myImpl->PartFor(link.component);
    if (!a.FindComponent(link.component)) return Error{"Компонент-источник удалён из сборки"};
    if (!part || part->shape.IsNull()) return Error{"У компонента-источника нет тела"};
    try {
        const std::optional<TopoDS_Edge> edge = occt::ResolveEdges({link.edge}, part->shape).front();
        if (!edge) return Error{"Ребро детали-источника не найдено"};
        const TopLoc_Location where(ToTrsf(Relative(a, target, link.component)));
        return occt::ProjectEdge(TopoDS::Edge(edge->Moved(where)), plane);
    } catch (const Standard_Failure& f) {
        return Error{std::string("Ошибка проекции: ") + f.GetMessageString()};
    }
}

// --- adaptivity --------------------------------------------------------------------

bool HasContextProjections(const doc::Document& doc, std::string_view assembly) {
    for (const auto& f : doc.Features())
        if (const auto* sk = dynamic_cast<const doc::SketchFeature*>(f.get()))
            for (const doc::ContextProjection& p : sk->contextProjections)
                if (Matches(p, assembly)) return true;
    return false;
}

std::uint64_t ContextKey(const AssemblyModel& geometry, const assembly::Assembly& a, ObjectId target, const doc::Document& doc,
                         std::string_view assembly) {
    std::uint64_t h = 1469598103934665603ull;
    std::unordered_set<ObjectId> sources;
    for (const auto& f : doc.Features()) {
        const auto* sk = dynamic_cast<const doc::SketchFeature*>(f.get());
        if (!sk) continue;
        bool any = false;
        for (const doc::ContextProjection& p : sk->contextProjections)
            if (Matches(p, assembly)) {
                any = true;
                sources.insert(p.component);
            }
        if (!any) continue;
        // The plane the curves are projected onto (a sketch on a face moves with it).
        Mix(h, f->id.value);
        Mix(h, sk->placement.frame.origin);
        Mix(h, sk->placement.frame.normal);
        Mix(h, sk->placement.frame.xDir);
    }
    std::vector<ObjectId> ordered(sources.begin(), sources.end());
    std::sort(ordered.begin(), ordered.end());
    for (ObjectId s : ordered) {
        Mix(h, s.value);
        Mix(h, geometry.PartGenerationOf(s));
        if (!a.FindComponent(s)) continue;
        const assembly::Placement r = Relative(a, target, s);
        Mix(h, r.translation);
        Mix(h, r.rotation.col[0]);
        Mix(h, r.rotation.col[2]);
    }
    return h;
}

AdaptResult AdaptContextProjections(const AssemblyModel& geometry, const assembly::Assembly& a, ObjectId target, doc::Document& doc,
                                    std::string_view assembly) {
    AdaptResult result;
    std::size_t first = static_cast<std::size_t>(-1);
    for (std::size_t i = 0; i < doc.FeatureCount(); ++i) {
        auto* sk = dynamic_cast<doc::SketchFeature*>(doc.Features()[i].get());
        if (!sk) continue;
        bool moved = false;
        for (const doc::ContextProjection& p : sk->contextProjections) {
            if (!Matches(p, assembly)) continue;
            const Expected<ProjectedCurve> now = geometry.ProjectSourceEdge(a, target, p, sk->placement.frame);
            if (!now) {
                ++result.lost;   // the curve stays where it was
                continue;
            }
            if (CurveMatches(sk->sketch, p.curve, *now, 1e-7)) continue;
            if (PlaceProjectedCurve(sk->sketch, p.curve, *now)) {
                moved = true;
                ++result.moved;
            } else {
                ++result.lost;   // projects as another kind of curve now
            }
        }
        if (!moved) continue;
        doc::SolveSketch(*sk);   // what is tied to the curves follows them
        first = std::min(first, i);
    }
    if (first != static_cast<std::size_t>(-1)) doc.MarkChanged(first);
    return result;
}

bool WouldCycle(const AssemblyModel& geometry, const assembly::Assembly& a, ObjectId source, ObjectId target,
                std::string_view assembly) {
    // The parts, not the components: two components of one part are one part.
    const doc::Document* targetPart = geometry.PartOf(target);
    if (!targetPart) return false;
    std::vector<ObjectId> stack{source};
    std::unordered_set<ObjectId> seen;
    while (!stack.empty()) {
        const ObjectId c = stack.back();
        stack.pop_back();
        if (!seen.insert(c).second) continue;
        const doc::Document* part = geometry.PartOf(c);
        if (!part) continue;
        if (part == targetPart) return true;
        for (const auto& f : part->Features())
            if (const auto* sk = dynamic_cast<const doc::SketchFeature*>(f.get()))
                for (const doc::ContextProjection& p : sk->contextProjections)
                    if (p.assembly == assembly && a.FindComponent(p.component)) stack.push_back(p.component);
    }
    return false;
}

} // namespace bz::kernel
