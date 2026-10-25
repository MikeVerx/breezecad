#include "kernel/occt/Hole.h"

#include "kernel/occt/EdgeMatch.h"
#include "kernel/occt/Extrude.h"   // ApplyBoolean
#include "kernel/occt/FaceMatch.h"
#include "kernel/occt/Thread.h"

#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <Bnd_Box.hxx>
#include <Standard_Failure.hxx>
#include <format>
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
Expected<TopoDS_Shape> HoleTool(const doc::HoleFeature& f, Vec3 top, Vec3 down, double depth) {
    const double r = f.DrillDiameter() / 2;
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
    if (f.threaded) {
        // The thread from the face down: the whole hole, or as deep as asked.
        const double length = f.threadFull ? depth : std::min(f.threadDepth.value, depth);
        const bool runsOut = f.throughAll && f.threadFull;
        const doc::ThreadCut cut = f.ThreadGeometry();
        // The lead-in chamfer: a 45° countersink of the drill from the
        // thread's root down to the drilled wall, at the mouth. Fused into the
        // drill like any countersink. (A through hole's far end is wherever
        // the body ends: not known here, so not chamfered.)
        if (f.threadSpec.leadChamfer) {
            const double root = cut.anchor + cut.depth, wall = r;
            const double rTop = root + kOvershoot;
            const auto cone = [&](Vec3 at, Vec3 dir) {
                return BRepPrimAPI_MakeCone(gp_Ax2(ToPnt(at - dir * kOvershoot), ToDir(dir)), rTop, wall, rTop - wall).Shape();
            };
            if (root > wall + 1e-6) tool = BRepAlgoAPI_Fuse(tool, cone(top, down)).Shape();
        }
        const Expected<TopoDS_Shape> groove =
            HoleThreadGroove(top, down, cut, f.threadPitch.value, length, f.threadLeft, runsOut);
        if (!groove) return groove.GetError();
        tool = BRepAlgoAPI_Fuse(tool, *groove).Shape();
    }
    return tool;
}

Status CheckSizes(const doc::HoleFeature& f) {
    for (const Parameter* p : f.Parameters())
        if (p->HasError()) return Error{p->error};
    if (f.DrillDiameter() <= 1e-6) return Error{"Диаметр должен быть больше нуля"};
    if (f.threaded && f.threadPitch.value <= 1e-3) return Error{"Шаг резьбы должен быть больше нуля"};
    if (f.threaded && f.threadSpec.fit == doc::ThreadFit::Custom && f.threadSpec.clearance.HasError())
        return Error{f.threadSpec.clearance.error};
    if (f.threaded && !f.threadFull && f.threadDepth.value <= 1e-6) return Error{"Глубина резьбы должна быть больше нуля"};
    if (!f.throughAll && f.depth.value <= 1e-6) return Error{"Глубина должна быть больше нуля"};
    if (f.type == doc::HoleType::Counterbore && (f.headDiameter.value <= f.DrillDiameter() || f.headDepth.value <= 1e-6))
        return Error{"Цековка должна быть шире отверстия и иметь глубину"};
    if (f.type == doc::HoleType::Countersink && f.sinkDiameter.value <= f.DrillDiameter())
        return Error{"Зенковка должна быть шире отверстия"};
    return Status::Ok();
}

} // namespace

Expected<TopoDS_Shape> HoleTools(const TopoDS_Shape& body, const doc::HoleFeature& f) {
    if (Status s = CheckSizes(f); !s) return s.GetError();
    if (f.centres.empty()) return Error{"Укажите центр отверстия на грани"};
    const std::optional<Frame> plane = ResolvePlaneFrame(body, f.face);
    if (!plane) return Error{"Грань отверстия не найдена: модель изменилась слишком сильно"};
    const Frame& frame = *plane;
    const Vec3 down = -frame.normal;
    const double depth = f.throughAll ? Diagonal(body) : f.depth.value;
    try {
        TopoDS_Shape tools;
        for (Vec2 c : f.centres) {
            const Expected<TopoDS_Shape> tool = HoleTool(f, frame.ToWorld(c), down, depth);
            if (!tool) return tool.GetError();
            tools = tools.IsNull() ? *tool : BRepAlgoAPI_Fuse(tools, *tool).Shape();
        }
        return tools;
    } catch (const Standard_Failure& failure) {
        return Error{std::format("Ошибка отверстия: {}", failure.GetMessageString())};
    }
}

Expected<TopoDS_Shape> DrillHoles(const TopoDS_Shape& body, const doc::HoleFeature& f) {
    const Expected<TopoDS_Shape> tools = HoleTools(body, f);
    if (!tools) return tools.GetError();
    return ApplyBoolean(body, *tools, doc::BooleanOp::Cut);
}

std::size_t FollowAxes(const TopoDS_Shape& body, doc::HoleFeature& feature) {
    std::size_t lost = 0;
    const Frame plane = doc::PlaneFrame(feature.face);
    for (doc::HoleAxis& axis : feature.concentric) {
        if (axis.centre >= feature.centres.size()) continue;
        std::optional<std::pair<Vec3, Vec3>> line;
        try {
            if (axis.edge) {
                line = ResolveCircle(*axis.edge, body);
            } else if (axis.cylinder) {
                if (const auto now = ResolveCylinder(body, *axis.cylinder)) {
                    axis.cylinder->axisPoint = now->axisPoint;
                    axis.cylinder->axisDir = now->axisDir;
                    axis.cylinder->radius = now->radius;
                    line = std::make_pair(now->axisPoint, now->axisDir.Normalized());
                }
            }
        } catch (const Standard_Failure&) {
            line.reset();
        }
        const double along = line ? line->second.Dot(plane.normal) : 0.0;
        if (!line || std::abs(along) < 1e-6) {   // gone, or lying in the face's plane: no crossing
            ++lost;
            continue;
        }
        const double t = (plane.origin - line->first).Dot(plane.normal) / along;
        feature.centres[axis.centre] = plane.ToLocal(line->first + line->second * t);
    }
    return lost;
}

} // namespace bz::kernel::occt
