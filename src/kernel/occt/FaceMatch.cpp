#include "kernel/occt/FaceMatch.h"

#include <gp_Cylinder.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp_Face.hxx>
#include <BRep_Tool.hxx>
#include <Geom2d_Curve.hxx>
#include <Standard_Failure.hxx>
#include <BRepExtrema_SupportType.hxx>
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

/// The face's (u, v) at the nearest point `distance` found on its edge or
/// vertex. A body of revolution has its seam where the profile was, so a
/// point picked there lands on an edge, not inside the face.
std::optional<gp_Pnt2d> ParametersOnBoundary(const BRepExtrema_DistShapeShape& distance, const TopoDS_Face& face) {
    try {
        if (distance.SupportTypeShape2(1) == BRepExtrema_IsOnEdge) {
            const TopoDS_Edge edge = TopoDS::Edge(distance.SupportOnShape2(1));
            double t = 0, first = 0, last = 0;
            distance.ParOnEdgeS2(1, t);
            const Handle(Geom2d_Curve) onFace = BRep_Tool::CurveOnSurface(edge, face, first, last);
            if (onFace.IsNull()) return std::nullopt;
            return onFace->Value(t);
        }
        if (distance.SupportTypeShape2(1) == BRepExtrema_IsVertex)
            return BRep_Tool::Parameters(TopoDS::Vertex(distance.SupportOnShape2(1)), face);
    } catch (const Standard_Failure&) {
    }
    return std::nullopt;
}

} // namespace

std::optional<Vec3> PlanarNormal(const TopoDS_Face& face) {
    const BRepAdaptor_Surface surface(face);
    if (surface.GetType() != GeomAbs_Plane) return std::nullopt;
    Vec3 normal = ToVec(surface.Plane().Axis().Direction().XYZ()).Normalized();
    if (face.Orientation() == TopAbs_REVERSED) normal = -normal;
    return normal;
}

std::optional<Vec3> NormalAt(const TopoDS_Face& face, const Vec3& around) {
    const TopoDS_Vertex probe = BRepBuilderAPI_MakeVertex(gp_Pnt(around.x, around.y, around.z));
    BRepExtrema_DistShapeShape distance(probe, face);   // to a face, never a solid (OCCT 8)
    if (distance.IsDone() && distance.NbSolution() >= 1 && distance.SupportTypeShape2(1) != BRepExtrema_IsInFace) {
        // On an edge or vertex of the face (a seam): the normal right there.
        if (const std::optional<gp_Pnt2d> uv = ParametersOnBoundary(distance, face)) {
            BRepGProp_Face props(face);
            gp_Pnt p;
            gp_Vec n;
            props.Normal(uv->X(), uv->Y(), p, n);
            if (n.Magnitude() >= 1e-12) return ToVec(n.XYZ()).Normalized();
        }
    }
    if (!distance.IsDone() || distance.NbSolution() < 1 || distance.SupportTypeShape2(1) != BRepExtrema_IsInFace) {
        // Nothing better (a cone's apex): the normal at the middle of the
        // face's parameter range, near enough for direction checks.
        BRepGProp_Face props(face);
        double u1, u2, v1, v2;
        props.Bounds(u1, u2, v1, v2);
        gp_Pnt p;
        gp_Vec n;
        props.Normal((u1 + u2) / 2, (v1 + v2) / 2, p, n);
        if (n.Magnitude() < 1e-12) return std::nullopt;
        return ToVec(n.XYZ()).Normalized();
    }
    double u = 0, v = 0;
    distance.ParOnFaceS2(1, u, v);
    BRepGProp_Face props(face);   // takes the face's orientation into account
    gp_Pnt p;
    gp_Vec n;
    props.Normal(u, v, p, n);
    if (n.Magnitude() < 1e-12) return std::nullopt;
    return ToVec(n.XYZ()).Normalized();
}

std::optional<TopoDS_Face> ResolveAnyFace(const TopoDS_Shape& body, const doc::FaceReference& ref) {
    if (!ref.IsSet()) return std::nullopt;
    const TopoDS_Vertex probe = BRepBuilderAPI_MakeVertex(gp_Pnt(ref.point.x, ref.point.y, ref.point.z));
    std::optional<TopoDS_Face> best;
    double bestDistance = 1e300;
    for (TopExp_Explorer e(body, TopAbs_FACE); e.More(); e.Next()) {
        const TopoDS_Face& face = TopoDS::Face(e.Current());
        BRepExtrema_DistShapeShape distance(probe, face);
        if (!distance.IsDone() || distance.Value() >= bestDistance) continue;
        const std::optional<Vec3> normal = NormalAt(face, ref.point);
        if (!normal || normal->Dot(ref.normal.Normalized()) < 0.3) continue;   // a face turned away (the other side of a wall)
        best = face;
        bestDistance = distance.Value();
    }
    return best;
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

std::optional<Frame> ResolvePlaneFrame(const TopoDS_Shape& body, const doc::FaceReference& ref) {
    const std::optional<TopoDS_Face> face = ResolveFace(body, ref);
    if (!face) return std::nullopt;
    const Vec3 n = ref.normal.Normalized();
    const Vec3 location = ToVec(BRepAdaptor_Surface(*face).Plane().Location().XYZ());
    const Vec3 onFace = ref.point + n * (location - ref.point).Dot(n);
    return doc::PlaneFrame({ref.normal, onFace});
}

std::optional<doc::CylinderTouch> ResolveCylinder(const TopoDS_Shape& body, const doc::CylinderTouch& ref) {
    std::optional<doc::CylinderTouch> best;
    double bestScore = 0;
    for (TopExp_Explorer e(body, TopAbs_FACE); e.More(); e.Next()) {
        const BRepAdaptor_Surface surface(TopoDS::Face(e.Current()));
        if (surface.GetType() != GeomAbs_Cylinder) continue;
        const gp_Cylinder cyl = surface.Cylinder();
        const gp_Pnt o = cyl.Axis().Location();
        const gp_Dir d = cyl.Axis().Direction();
        const Vec3 dir{d.X(), d.Y(), d.Z()}, base{o.X(), o.Y(), o.Z()};
        if (dir.Cross(ref.axisDir).Length() > 1e-6) continue;   // not parallel
        const Vec3 off = base - ref.axisPoint;
        const double axisGap = (off - ref.axisDir * off.Dot(ref.axisDir)).Length();
        const double score = axisGap + std::abs(cyl.Radius() - ref.radius);
        if (axisGap > std::max(ref.radius, 1.0)) continue;   // another cylinder, not this one moved
        if (!best || score < bestScore) {
            best = doc::CylinderTouch{base, ref.axisDir, cyl.Radius(), ref.radial};
            bestScore = score;
        }
    }
    return best;
}

} // namespace bz::kernel::occt
