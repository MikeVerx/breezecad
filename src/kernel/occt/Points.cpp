#include "kernel/occt/Points.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <gp_Pnt.hxx>

namespace bz::kernel::occt {
namespace {

Vec3 ToVec(const gp_Pnt& p) { return {p.X(), p.Y(), p.Z()}; }

} // namespace

std::optional<Vec3> EdgeCenter(const TopoDS_Edge& edge) {
    const BRepAdaptor_Curve c(edge);
    switch (c.GetType()) {
        case GeomAbs_Circle:  return ToVec(c.Circle().Location());
        case GeomAbs_Ellipse: return ToVec(c.Ellipse().Location());
        default:              return std::nullopt;
    }
}

Vec3 EdgeMiddle(const TopoDS_Edge& edge) {
    const BRepAdaptor_Curve c(edge);
    const double first = c.FirstParameter(), last = c.LastParameter();
    // Half the length, not half the parameter range (they differ on splines).
    const double length = GCPnts_AbscissaPoint::Length(c, first, last);
    const GCPnts_AbscissaPoint half(c, length / 2, first);
    return ToVec(c.Value(half.IsDone() ? half.Parameter() : (first + last) / 2));
}

std::optional<Vec3> SphereCenter(const TopoDS_Face& face) {
    const BRepAdaptor_Surface s(face);
    if (s.GetType() != GeomAbs_Sphere) return std::nullopt;
    return ToVec(s.Sphere().Location());
}

} // namespace bz::kernel::occt
