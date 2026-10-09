#include "kernel/occt/Hole.h"

#include "kernel/occt/Extrude.h"   // ApplyBoolean

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepTools.hxx>
#include <Bnd_Box.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
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

std::optional<Vec3> FindPlanarFace(const TopoDS_Shape& body, const doc::FaceReference& ref) {
    const Vec3 n = ref.normal.Normalized();
    std::optional<Vec3> best;
    double bestOffset = 1e300;
    for (TopExp_Explorer e(body, TopAbs_FACE); e.More(); e.Next()) {
        const TopoDS_Face& face = TopoDS::Face(e.Current());
        if (BRepAdaptor_Surface(face).GetType() != GeomAbs_Plane) continue;
        double u1, u2, v1, v2;
        BRepTools::UVBounds(face, u1, u2, v1, v2);
        gp_Pnt p;
        gp_Vec normal;
        BRepGProp_Face(face).Normal((u1 + u2) / 2, (v1 + v2) / 2, p, normal);
        const Vec3 fn = Vec3{normal.X(), normal.Y(), normal.Z()}.Normalized();
        if (fn.Dot(n) < 0.999) continue;
        // Distance of the stored point from this face's plane.
        const Vec3 onFace{p.X(), p.Y(), p.Z()};
        const double offset = std::abs((onFace - ref.point).Dot(n));
        if (offset < bestOffset) {
            bestOffset = offset;
            best = ref.point + n * (onFace - ref.point).Dot(n);   // the stored point moved onto this plane
        }
    }
    return best;
}

Expected<TopoDS_Shape> DrillHoles(const TopoDS_Shape& body, const doc::HoleFeature& f) {
    if (Status s = CheckSizes(f); !s) return s.GetError();
    if (f.centres.empty()) return Error{"Укажите центр отверстия на грани"};
    const std::optional<Vec3> onFace = FindPlanarFace(body, f.face);
    if (!onFace) return Error{"Грань отверстия не найдена: модель изменилась слишком сильно"};

    const Frame frame = doc::HolePlaneFrame({f.face.normal, *onFace});
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
