#include "kernel/occt/FaceMatch.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>

#include <cmath>

namespace bz::kernel::occt {
namespace {

constexpr double kSameDirection = 0.999;   // cos of the largest angle still counted as "same normal"
constexpr double kSamePlane = 1e-6;        // mm: offsets closer than this tie, distance decides

Vec3 ToVec(const gp_XYZ& v) { return {v.X(), v.Y(), v.Z()}; }

} // namespace

std::optional<Vec3> PlanarNormal(const TopoDS_Face& face) {
    const BRepAdaptor_Surface surface(face);
    if (surface.GetType() != GeomAbs_Plane) return std::nullopt;
    Vec3 normal = ToVec(surface.Plane().Axis().Direction().XYZ()).Normalized();
    if (face.Orientation() == TopAbs_REVERSED) normal = -normal;
    return normal;
}

std::optional<TopoDS_Face> ResolveFace(const TopoDS_Shape& body, const doc::FaceReference& ref) {
    if (!ref.IsSet()) return std::nullopt;
    const Vec3 n = ref.normal.Normalized();
    const TopoDS_Vertex probe = BRepBuilderAPI_MakeVertex(gp_Pnt(ref.point.x, ref.point.y, ref.point.z));

    std::optional<TopoDS_Face> best;
    double bestOffset = 1e300, bestDistance = 1e300;
    for (TopExp_Explorer e(body, TopAbs_FACE); e.More(); e.Next()) {
        const TopoDS_Face& face = TopoDS::Face(e.Current());
        const std::optional<Vec3> normal = PlanarNormal(face);
        if (!normal || normal->Dot(n) < kSameDirection) continue;
        const Vec3 location = ToVec(BRepAdaptor_Surface(face).Plane().Location().XYZ());
        const double offset = std::abs((location - ref.point).Dot(n));
        if (offset > bestOffset + kSamePlane) continue;
        // Same plane as the best so far (or nearer): the face nearest the point wins.
        BRepExtrema_DistShapeShape distance(probe, face);
        const double d = distance.IsDone() ? distance.Value() : 1e300;
        if (offset < bestOffset - kSamePlane || d < bestDistance) {
            best = face;
            bestOffset = offset;
            bestDistance = d;
        }
    }
    return best;
}

} // namespace bz::kernel::occt
