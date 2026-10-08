#include "kernel/occt/Revolve.h"

#include "kernel/occt/Extrude.h"   // Clean

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Ax1.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>
#include <optional>

namespace bz::kernel::occt {
namespace {

gp_Pnt ToPnt(Vec3 v) { return {v.x, v.y, v.z}; }

/// The rotation axis in model space, or nullopt with an explanation.
Expected<gp_Ax1> AxisOf(const doc::RevolveFeature& f, const sketch::Sketch& sk, const Frame& frame) {
    Vec2 a, b;
    switch (f.axis.kind) {
        case doc::RevolveAxisKind::SketchX: a = {0, 0}; b = {1, 0}; break;
        case doc::RevolveAxisKind::SketchY: a = {0, 0}; b = {0, 1}; break;
        case doc::RevolveAxisKind::SketchLine: {
            const sketch::Line* line = sk.FindLine(f.axis.line);
            if (!line) return Error{"Ось вращения удалена из эскиза"};
            a = sk.PositionOf(line->start);
            b = sk.PositionOf(line->end);
            break;
        }
    }
    const Vec3 p = frame.ToWorld(a), q = frame.ToWorld(b);
    const Vec3 d = q - p;
    if (d.Length() < 1e-9) return Error{"Ось вращения нулевой длины"};
    return gp_Ax1(ToPnt(p), gp_Dir(d.x, d.y, d.z));
}

/// True when the profile lies on both sides of the axis: turning it would
/// make the solid pass through itself.
bool CrossesAxis(const std::vector<TopoDS_Face>& faces, const gp_Ax1& axis, const Frame& frame) {
    const gp_Vec dir(axis.Direction());
    const gp_Vec normal(frame.normal.x, frame.normal.y, frame.normal.z);
    double lo = 0, hi = 0;
    for (const TopoDS_Face& face : faces)
        for (TopExp_Explorer e(face, TopAbs_EDGE); e.More(); e.Next()) {
            const BRepAdaptor_Curve curve(TopoDS::Edge(e.Current()));
            for (int i = 0; i <= 16; ++i) {
                const double t = curve.FirstParameter() + (curve.LastParameter() - curve.FirstParameter()) * i / 16.0;
                const gp_Vec rel(axis.Location(), curve.Value(t));
                const double side = dir.Crossed(rel).Dot(normal);
                lo = std::min(lo, side);
                hi = std::max(hi, side);
            }
        }
    const double tolerance = 1e-6;
    return lo < -tolerance && hi > tolerance;
}

} // namespace

Expected<TopoDS_Shape> MakeRevolution(const std::vector<TopoDS_Face>& faces, const doc::RevolveFeature& f,
                                      const sketch::Sketch& sk, const Frame& frame) {
    if (faces.empty()) return Error{"Нет замкнутого контура для вращения"};
    Expected<gp_Ax1> axis = AxisOf(f, sk, frame);
    if (!axis) return axis.GetError();
    if (CrossesAxis(faces, *axis, frame)) return Error{"Ось вращения пересекает контур — контур должен лежать по одну сторону от оси"};

    double angle = 2 * kPi;
    if (f.extent != doc::RevolveExtent::Full) {
        angle = f.angle.value;
        if (angle <= 1e-9) return Error{"Угол должен быть больше нуля"};
        if (angle > 2 * kPi + 1e-9) return Error{"Угол не может быть больше 360°"};
    }
    gp_Ax1 turnAxis = *axis;
    if (f.reversed && f.extent == doc::RevolveExtent::Angle) turnAxis.Reverse();

    // Symmetric: start half the angle back.
    gp_Trsf back;
    const bool symmetric = f.extent == doc::RevolveExtent::Symmetric;
    if (symmetric) back.SetRotation(turnAxis, -angle / 2);

    try {
        TopoDS_Shape result;
        for (const TopoDS_Face& face : faces) {
            const TopoDS_Shape start = symmetric ? BRepBuilderAPI_Transform(face, back, true).Shape() : TopoDS_Shape(face);
            BRepPrimAPI_MakeRevol revol(start, turnAxis, angle, true);
            if (!revol.IsDone()) return Error{"Не удалось построить вращение"};
            result = result.IsNull() ? revol.Shape() : BRepAlgoAPI_Fuse(result, revol.Shape()).Shape();
        }
        result = Clean(result);
        if (!BRepCheck_Analyzer(result).IsValid()) return Error{"Вращение дало некорректное тело"};
        return result;
    } catch (const Standard_Failure& failure) {
        return Error{std::string("Ошибка вращения: ") + failure.GetMessageString()};
    }
}

} // namespace bz::kernel::occt
