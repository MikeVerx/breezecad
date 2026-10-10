#include "kernel/occt/EdgeMatch.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepTools.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Circ.hxx>
#include <gp_Lin.hxx>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace bz::kernel::occt {
namespace {

using doc::CurveKind;
using doc::EdgeReference;
using doc::FaceSignature;
using doc::SurfaceKind;

constexpr double kParallel = 0.999;   // |cos| for "same direction"

Vec3 ToVec(const gp_XYZ& v) { return {v.X(), v.Y(), v.Z()}; }

/// A direction without sign: the first noticeable component is positive.
Vec3 Canonical(Vec3 d) {
    d = d.Normalized();
    const double first = std::abs(d.x) > 1e-9 ? d.x : std::abs(d.y) > 1e-9 ? d.y : d.z;
    return first < 0 ? -d : d;
}

FaceSignature DescribeFace(const TopoDS_Face& face) {
    FaceSignature sig;
    const BRepAdaptor_Surface surface(face);
    switch (surface.GetType()) {
        case GeomAbs_Plane: {
            sig.kind = SurfaceKind::Plane;
            // Outward normal: BRepGProp_Face accounts for the face orientation.
            double u1, u2, v1, v2;
            BRepTools::UVBounds(face, u1, u2, v1, v2);
            gp_Pnt p;
            gp_Vec n;
            BRepGProp_Face(face).Normal((u1 + u2) / 2, (v1 + v2) / 2, p, n);
            sig.direction = ToVec(n.XYZ()).Normalized();
            break;
        }
        case GeomAbs_Cylinder:
            sig.kind = SurfaceKind::Cylinder;
            sig.direction = Canonical(ToVec(surface.Cylinder().Axis().Direction().XYZ()));
            break;
        case GeomAbs_Cone:
            sig.kind = SurfaceKind::Cone;
            sig.direction = Canonical(ToVec(surface.Cone().Axis().Direction().XYZ()));
            break;
        case GeomAbs_Sphere: sig.kind = SurfaceKind::Sphere; break;
        case GeomAbs_Torus:
            sig.kind = SurfaceKind::Torus;
            sig.direction = Canonical(ToVec(surface.Torus().Axis().Direction().XYZ()));
            break;
        default: break;
    }
    return sig;
}

bool SameFace(const FaceSignature& a, const FaceSignature& b) {
    if (a.kind != b.kind) return false;
    if (a.direction.Length() < 0.5 || b.direction.Length() < 0.5) return true;   // no direction to compare
    const double cos = a.direction.Dot(b.direction);
    // A plane's outward normal has a meaningful sign (top vs bottom); axes do not.
    return a.kind == SurfaceKind::Plane ? cos > kParallel : std::abs(cos) > kParallel;
}

/// Face signatures computed once per face (a face borders many edges).
using FaceCache = std::vector<std::optional<FaceSignature>>;

EdgeReference Describe(const TopoDS_Edge& edge, const TopTools_IndexedDataMapOfShapeListOfShape& edgeFaces,
                       const TopTools_IndexedMapOfShape* faceIndex = nullptr, FaceCache* cache = nullptr) {
    EdgeReference ref;
    const BRepAdaptor_Curve curve(edge);
    switch (curve.GetType()) {
        case GeomAbs_Line:
            ref.curve = CurveKind::Line;
            ref.direction = Canonical(ToVec(curve.Line().Direction().XYZ()));
            break;
        case GeomAbs_Circle:
            ref.curve = CurveKind::Circle;
            ref.direction = Canonical(ToVec(curve.Circle().Axis().Direction().XYZ()));
            ref.radius = curve.Circle().Radius();
            break;
        default: break;
    }
    ref.middle = ToVec(curve.Value((curve.FirstParameter() + curve.LastParameter()) / 2).XYZ());

    // The faces on both sides, in a canonical order.
    std::vector<FaceSignature> faces;
    if (const TopTools_ListOfShape* list = edgeFaces.Seek(edge))
        for (const TopoDS_Shape& f : *list) {
            const int i = faceIndex && cache ? faceIndex->FindIndex(f) : 0;
            if (i <= 0) {
                faces.push_back(DescribeFace(TopoDS::Face(f)));
                continue;
            }
            auto& slot = (*cache)[static_cast<std::size_t>(i)];
            if (!slot) slot = DescribeFace(TopoDS::Face(f));
            faces.push_back(*slot);
        }
    while (faces.size() < 2) faces.emplace_back();
    std::sort(faces.begin(), faces.end(), [](const FaceSignature& a, const FaceSignature& b) {
        return std::tie(a.kind, a.direction.x, a.direction.y, a.direction.z) <
               std::tie(b.kind, b.direction.x, b.direction.y, b.direction.z);
    });
    ref.faces = {faces[0], faces[1]};
    return ref;
}

/// How well `candidate` fits `ref`; negative means "not this edge".
double Score(const EdgeReference& ref, const EdgeReference& candidate, double size) {
    if (ref.curve != candidate.curve) return -1;
    if (ref.curve != CurveKind::Other && std::abs(ref.direction.Dot(candidate.direction)) < kParallel) return -1;
    int faces = 0;
    bool used[2] = {false, false};
    for (const FaceSignature& want : ref.faces)
        for (int i = 0; i < 2; ++i)
            if (!used[i] && SameFace(want, candidate.faces[static_cast<std::size_t>(i)])) {
                used[i] = true;
                ++faces;
                break;
            }
    if (faces == 0) return -1;
    double radius = 0;
    if (ref.curve == CurveKind::Circle)
        radius = 1.0 - std::min(1.0, std::abs(ref.radius - candidate.radius) / std::max(ref.radius, 1e-6));
    // Faces decide, the radius next, the position only breaks ties.
    const double nearness = 1.0 - std::min(1.0, (ref.middle - candidate.middle).Length() / size);
    return faces * 100.0 + radius * 10.0 + nearness;
}

} // namespace

doc::EdgeReference DescribeEdge(const TopoDS_Edge& edge, const TopoDS_Shape& body) {
    TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
    TopExp::MapShapesAndAncestors(body, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
    return Describe(edge, edgeFaces);
}

std::vector<std::optional<TopoDS_Edge>> ResolveEdges(const std::vector<doc::EdgeReference>& refs, const TopoDS_Shape& body) {
    TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
    TopExp::MapShapesAndAncestors(body, TopAbs_EDGE, TopAbs_FACE, edgeFaces);

    TopTools_IndexedMapOfShape faceIndex;
    TopExp::MapShapes(body, TopAbs_FACE, faceIndex);
    FaceCache cache(static_cast<std::size_t>(faceIndex.Extent()) + 1);

    std::vector<std::pair<TopoDS_Edge, EdgeReference>> candidates;
    Box3 box;
    for (int i = 1; i <= edgeFaces.Extent(); ++i) {
        const TopoDS_Edge& e = TopoDS::Edge(edgeFaces.FindKey(i));
        if (BRep_Tool::Degenerated(e)) continue;   // the pole of a sphere or cone
        candidates.emplace_back(e, Describe(e, edgeFaces, &faceIndex, &cache));
        box.Include(candidates.back().second.middle);
    }
    const double size = box.IsEmpty() ? 1.0 : std::max((box.max - box.min).Length(), 1.0);

    std::vector<std::optional<TopoDS_Edge>> found;
    for (const EdgeReference& ref : refs) {
        double best = -1;
        std::optional<TopoDS_Edge> match;
        for (const auto& [edge, sig] : candidates)
            if (const double s = Score(ref, sig, size); s > best) {
                best = s;
                match = edge;
            }
        found.push_back(match);
    }
    return found;
}

std::optional<std::pair<Vec3, Vec3>> ResolveLine(const doc::EdgeReference& ref, const TopoDS_Shape& body) {
    const auto found = ResolveEdges({ref}, body);
    if (found.empty() || !found.front()) return std::nullopt;
    const BRepAdaptor_Curve curve(*found.front());
    if (curve.GetType() != GeomAbs_Line) return std::nullopt;
    const gp_Pnt mid = curve.Value((curve.FirstParameter() + curve.LastParameter()) / 2);
    const gp_Dir d = curve.Line().Direction();
    return std::make_pair(Vec3{mid.X(), mid.Y(), mid.Z()}, Vec3{d.X(), d.Y(), d.Z()});
}

std::optional<Vec3> ResolveVertex(const doc::EdgeReference& ref, const Vec3& was, const TopoDS_Shape& body) {
    const std::optional<TopoDS_Edge> edge = ResolveEdges({ref}, body).front();
    if (!edge) return std::nullopt;
    TopoDS_Vertex a, b;
    TopExp::Vertices(*edge, a, b);
    if (a.IsNull() || b.IsNull()) return std::nullopt;
    const gp_Pnt pa = BRep_Tool::Pnt(a), pb = BRep_Tool::Pnt(b);
    const Vec3 va{pa.X(), pa.Y(), pa.Z()}, vb{pb.X(), pb.Y(), pb.Z()};
    return (va - was).Length() <= (vb - was).Length() ? va : vb;
}

std::optional<doc::EdgeReference> DescribeVertexEdge(const TopoDS_Vertex& vertex, const TopoDS_Shape& body) {
    TopTools_IndexedDataMapOfShapeListOfShape edgesOf;
    TopExp::MapShapesAndAncestors(body, TopAbs_VERTEX, TopAbs_EDGE, edgesOf);
    const int index = edgesOf.FindIndex(vertex);
    if (index == 0) return std::nullopt;
    // A straight edge first: its description is the steadiest.
    std::optional<TopoDS_Edge> chosen;
    for (const TopoDS_Shape& e : edgesOf.FindFromIndex(index)) {
        const TopoDS_Edge& edge = TopoDS::Edge(e);
        if (BRep_Tool::Degenerated(edge)) continue;
        if (!chosen || BRepAdaptor_Curve(edge).GetType() == GeomAbs_Line) chosen = edge;
        if (BRepAdaptor_Curve(*chosen).GetType() == GeomAbs_Line) break;
    }
    if (!chosen) return std::nullopt;
    return DescribeEdge(*chosen, body);
}

} // namespace bz::kernel::occt
