#include "kernel/occt/Project.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopoDS_Vertex.hxx>

#include <cmath>

namespace bz::kernel::occt {
namespace {

Vec3 ToVec(const gp_Pnt& p) { return {p.X(), p.Y(), p.Z()}; }
Vec3 ToVec(const gp_Dir& d) { return {d.X(), d.Y(), d.Z()}; }

} // namespace

Expected<ProjectedCurve> ProjectEdge(const TopoDS_Edge& edge, const Frame& frame) {
    const BRepAdaptor_Curve curve(edge);
    // Ends in the edge's own direction (vertex order follows orientation).
    TopoDS_Vertex first, last;
    TopExp::Vertices(edge, first, last, true);
    if (first.IsNull() && curve.GetType() != GeomAbs_Circle) return Error{"Ребро без концов не проецируется"};

    ProjectedCurve out;
    switch (curve.GetType()) {
        case GeomAbs_Line: {
            out.kind = ProjectedCurve::Kind::Line;
            out.start = frame.ToLocal(ToVec(BRep_Tool::Pnt(first)));
            out.end = frame.ToLocal(ToVec(BRep_Tool::Pnt(last)));
            if ((out.end - out.start).Length() < 1e-6) return Error{"Ребро смотрит прямо на плоскость эскиза — в проекции точка"};
            return out;
        }
        case GeomAbs_Circle: {
            const gp_Circ circle = curve.Circle();
            const double facing = ToVec(circle.Axis().Direction()).Dot(frame.normal);
            if (std::abs(facing) < 0.9999)
                return Error{"Окружность не параллельна плоскости эскиза — в проекции эллипс (пока не поддерживается)"};
            out.centre = frame.ToLocal(ToVec(circle.Location()));
            out.radius = circle.Radius();
            const bool closed = first.IsNull() || BRep_Tool::Pnt(first).Distance(BRep_Tool::Pnt(last)) < 1e-7;
            if (closed) {
                out.kind = ProjectedCurve::Kind::Circle;
                return out;
            }
            out.kind = ProjectedCurve::Kind::Arc;
            Vec2 a = frame.ToLocal(ToVec(BRep_Tool::Pnt(first)));
            Vec2 b = frame.ToLocal(ToVec(BRep_Tool::Pnt(last)));
            // Which way the arc runs, seen from the sketch's side: by its middle point.
            const Vec2 mid = frame.ToLocal(ToVec(curve.Value((curve.FirstParameter() + curve.LastParameter()) / 2)));
            const auto angle = [&](Vec2 p) { return std::atan2(p.y - out.centre.y, p.x - out.centre.x); };
            const auto ccwSpan = [](double from, double to) {
                double d = to - from;
                while (d < 0) d += 2 * kPi;
                return d;
            };
            // Counter-clockwise from a to b must pass the middle; otherwise swap.
            if (ccwSpan(angle(a), angle(mid)) > ccwSpan(angle(a), angle(b))) std::swap(a, b);
            out.start = a;
            out.end = b;
            return out;
        }
        default: break;
    }
    return Error{"Такое ребро пока не проецируется: только отрезки, окружности и дуги"};
}

} // namespace bz::kernel::occt
