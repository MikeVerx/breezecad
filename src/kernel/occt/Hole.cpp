#include "kernel/occt/Hole.h"

#include "kernel/occt/Extrude.h"   // ApplyBoolean
#include "kernel/occt/FaceMatch.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <Bnd_Box.hxx>
#include <Standard_Failure.hxx>
#include <gp_Ax2.hxx>

#include <cmath>
#include <optional>

namespace bz::kernel::occt {
namespace {

constexpr double kOvershoot = 0.05;   // tools start this far above the face, so the cut is clean

gp_Pnt ToPnt(Vec3 v) { return {v.x, v.y, v.z}; }
gp_Dir ToDir(Vec3 v) { return {v.x, v.y, v.z}; }

/// Longer than any straight line through the body.
double Diagonal(const TopoDS_Shape& body) {
    Bnd_Box box;
    BRepBndLib::Add(body, box);
    return box.IsVoid() ? 1000.0 : std::sqrt(box.SquareExtent()) * 2 + 10;
}

/// The cutting tool of one hole at `top`, pointing into the body along `down`.
TopoDS_Shape HoleTool(const doc::HoleFeature& f, Vec3 top, Vec3 down, double depth) {
    const double r = f.diameter.value / 2;
    const gp_Ax2 start(ToPnt(top - down * kOvershoot), ToDir(down));
    TopoDS_Shape tool = BRepPrimAPI_MakeCylinder(start, r, depth + kOvershoot).Shape();
    if (f.type == doc::HoleType::Counterbore) {
        const TopoDS_Shape head = BRepPrimAPI_MakeCylinder(start, f.headDiameter.value / 2, f.headDepth.value + kOvershoot).Shape();
        tool = BRepAlgoAPI_Fuse(tool, head).Shape();
    } else if (f.type == doc::HoleType::Countersink) {
        // 90° cone: it narrows by its height on each side.
        const double rTop = f.sinkDiameter.value / 2 + kOvershoot;
        const TopoDS_Shape cone = BRepPrimAPI_MakeCone(start, rTop, r, rTop - r).Shape();
        tool = BRepAlgoAPI_Fuse(tool, cone).Shape();
    }
    return tool;
}

Status CheckSizes(const doc::HoleFeature& f) {
    for (const Parameter* p : f.Parameters())
        if (p->HasError()) return Error{p->error};
    if (f.diameter.value <= 1e-6) return Error{"Диаметр должен быть больше нуля"};
    if (!f.throughAll && f.depth.value <= 1e-6) return Error{"Глубина должна быть больше нуля"};
    if (f.type == doc::HoleType::Counterbore && (f.headDiameter.value <= f.diameter.value || f.headDepth.value <= 1e-6))
        return Error{"Цековка должна быть шире отверстия и иметь глубину"};
    if (f.type == doc::HoleType::Countersink && f.sinkDiameter.value <= f.diameter.value)
        return Error{"Зенковка должна быть шире отверстия"};
    return Status::Ok();
}

} // namespace

Expected<TopoDS_Shape> DrillHoles(const TopoDS_Shape& body, const doc::HoleFeature& f) {
    if (Status s = CheckSizes(f); !s) return s.GetError();
    if (f.centres.empty()) return Error{"Укажите центр отверстия на грани"};
    const std::optional<TopoDS_Face> face = ResolveFace(body, f.face);
    if (!face) return Error{"Грань отверстия не найдена: модель изменилась слишком сильно"};
    // The stored point moved onto the face's plane as it is now.
    const Vec3 n = f.face.normal.Normalized();
    const gp_XYZ location = BRepAdaptor_Surface(*face).Plane().Location().XYZ();
    const Vec3 onFace = f.face.point + n * (Vec3{location.X(), location.Y(), location.Z()} - f.face.point).Dot(n);

    const Frame frame = doc::HolePlaneFrame({f.face.normal, onFace});
    const Vec3 down = -frame.normal;
    const double depth = f.throughAll ? Diagonal(body) : f.depth.value;
    try {
        TopoDS_Shape tools;
        for (Vec2 c : f.centres) {
            const TopoDS_Shape tool = HoleTool(f, frame.ToWorld(c), down, depth);
            tools = tools.IsNull() ? tool : BRepAlgoAPI_Fuse(tools, tool).Shape();
        }
        return ApplyBoolean(body, tools, doc::BooleanOp::Cut);
    } catch (const Standard_Failure& failure) {
        return Error{std::string("Ошибка отверстия: ") + failure.GetMessageString()};
    }
}

} // namespace bz::kernel::occt
