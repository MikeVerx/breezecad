#include "kernel/occt/Measure.h"

#include "kernel/occt/FaceMatch.h"   // PlanarNormal

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS.hxx>

#include <cmath>
#include <numbers>

namespace bz::kernel::occt {
namespace {

Vec3 ToVec(const gp_Pnt& p) { return {p.X(), p.Y(), p.Z()}; }
Vec3 ToVec(const gp_Dir& d) { return {d.X(), d.Y(), d.Z()}; }

/// Direction of a straight edge, or of a flat face's normal; nullopt otherwise.
std::optional<Vec3> Direction(const TopoDS_Shape& s) {
    if (s.ShapeType() == TopAbs_EDGE) {
        const BRepAdaptor_Curve curve(TopoDS::Edge(s));
        if (curve.GetType() == GeomAbs_Line) return ToVec(curve.Line().Direction());
    } else if (s.ShapeType() == TopAbs_FACE) {
        return PlanarNormal(TopoDS::Face(s));
    }
    return std::nullopt;
}

} // namespace

EntityInfo Describe(const TopoDS_Shape& entity, Vec3 picked) {
    EntityInfo info;
    info.point = picked;
    switch (entity.ShapeType()) {
        case TopAbs_VERTEX:
            info.kind = EntityKind::Vertex;
            info.point = ToVec(BRep_Tool::Pnt(TopoDS::Vertex(entity)));
            break;
        case TopAbs_EDGE: {
            info.kind = EntityKind::Edge;
            GProp_GProps props;
            BRepGProp::LinearProperties(entity, props);
            info.length = props.Mass();
            const BRepAdaptor_Curve curve(TopoDS::Edge(entity));
            if (curve.GetType() == GeomAbs_Circle) info.radius = curve.Circle().Radius();
            info.straight = curve.GetType() == GeomAbs_Line;
            break;
        }
        case TopAbs_FACE: {
            info.kind = EntityKind::Face;
            GProp_GProps props;
            BRepGProp::SurfaceProperties(entity, props);
            info.area = props.Mass();
            const BRepAdaptor_Surface surface(TopoDS::Face(entity));
            if (surface.GetType() == GeomAbs_Cylinder) info.radius = surface.Cylinder().Radius();
            else if (surface.GetType() == GeomAbs_Sphere) info.radius = surface.Sphere().Radius();
            info.flat = surface.GetType() == GeomAbs_Plane;
            break;
        }
        default: break;
    }
    return info;
}

Measurement Measure(const TopoDS_Shape& a, const TopoDS_Shape& b) {
    Measurement m;
    BRepExtrema_DistShapeShape distance(a, b);
    if (distance.IsDone() && distance.NbSolution() > 0) {
        m.distance = distance.Value();
        m.from = ToVec(distance.PointOnShape1(1));
        m.to = ToVec(distance.PointOnShape2(1));
    }
    const auto da = Direction(a), db = Direction(b);
    if (da && db) {
        const double between = std::acos(std::min(1.0, std::abs(da->Normalized().Dot(db->Normalized()))));
        // A face gives its normal: between an edge and a face the angle is
        // the one to the face's plane, the complement (0 for an edge lying along it).
        const bool edgeAndFace = (a.ShapeType() == TopAbs_FACE) != (b.ShapeType() == TopAbs_FACE);
        m.angle = edgeAndFace ? kPi / 2 - between : between;
    }
    return m;
}

} // namespace bz::kernel::occt
