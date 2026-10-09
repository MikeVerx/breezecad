#include "kernel/occt/Thread.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepLib.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepTools.hxx>
#include <GCE2d_MakeSegment.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Ax3.hxx>
#include <gp_Cylinder.hxx>

#include <algorithm>
#include <cmath>
#include <format>

namespace bz::kernel::occt {
namespace {

// ISO 68-1: fundamental triangle height H = 0.866P; an external thread is
// 17/24 H deep (0.6134P), an internal one 5/8 H (0.5413P, from the minor
// diameter a tap drill leaves to the nominal one).
constexpr double kExternalDepth = 0.6134;
constexpr double kInternalDepth = 0.5413;
constexpr double kOvershoot = 0.1;   // of the pitch: the groove starts this far outside the surface

struct Site {
    TopoDS_Face face;
    Vec3 axisPoint, axisDir;
    double radius = 0, lo = 0, hi = 0;   // extent along axisDir from axisPoint
    bool external = true;
};

Expected<Site> Find(const TopoDS_Shape& body, const doc::CylinderTouch& ref) {
    std::optional<Site> best;
    double bestScore = 0;
    const Vec3 dir = ref.axisDir.Normalized();
    for (TopExp_Explorer e(body, TopAbs_FACE); e.More(); e.Next()) {
        const TopoDS_Face& face = TopoDS::Face(e.Current());
        const BRepAdaptor_Surface surface(face);
        if (surface.GetType() != GeomAbs_Cylinder) continue;
        const gp_Cylinder cyl = surface.Cylinder();
        const gp_Pnt o = cyl.Axis().Location();
        const gp_Dir d = cyl.Axis().Direction();
        if (Vec3{d.X(), d.Y(), d.Z()}.Cross(dir).Length() > 1e-6) continue;
        const Vec3 off = Vec3{o.X(), o.Y(), o.Z()} - ref.axisPoint;
        const double axisGap = (off - dir * off.Dot(dir)).Length();
        const double score = axisGap + std::abs(cyl.Radius() - ref.radius);
        if (axisGap > std::max(ref.radius, 1.0) || (best && score >= bestScore)) continue;

        Site s{face, ref.axisPoint + (off - dir * off.Dot(dir)), dir, cyl.Radius()};
        // Extent: the face's boundary projected on the axis.
        s.lo = 1e300;
        s.hi = -1e300;
        for (TopExp_Explorer v(face, TopAbs_VERTEX); v.More(); v.Next()) {
            const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(v.Current()));
            const double h = (Vec3{p.X(), p.Y(), p.Z()} - s.axisPoint).Dot(dir);
            s.lo = std::min(s.lo, h);
            s.hi = std::max(s.hi, h);
        }
        // Shaft or hole: does the outward normal point away from the axis?
        double u0, u1, v0, v1;
        BRepTools::UVBounds(face, u0, u1, v0, v1);
        BRepGProp_Face props(face);
        gp_Pnt p;
        gp_Vec n;
        props.Normal((u0 + u1) / 2, (v0 + v1) / 2, p, n);
        const Vec3 at = Vec3{p.X(), p.Y(), p.Z()} - s.axisPoint;
        s.external = Vec3{n.X(), n.Y(), n.Z()}.Dot(at - dir * at.Dot(dir)) > 0;
        if (s.hi - s.lo < 1e-6) continue;
        best = s;
        bestScore = score;
    }
    if (!best) return Error{"Цилиндрическая грань для резьбы не найдена — выберите её заново"};
    return *best;
}

} // namespace

Expected<ThreadSite> FindThreadSite(const TopoDS_Shape& body, const doc::CylinderTouch& ref) {
    try {
        const Expected<Site> s = Find(body, ref);
        if (!s) return s.GetError();
        const double d = 2 * s->radius;
        // A hole is the minor diameter: add back the depth of the thread
        // that a coarse tap of the matching size would cut.
        double nominal = d;
        if (!s->external)
            for (int i = 0; i < 3; ++i) nominal = d + 2 * kInternalDepth * doc::CoarsePitch(nominal);
        return ThreadSite{s->external, doc::StandardSize(nominal), s->hi - s->lo};
    } catch (const Standard_Failure& failure) {
        return Error{std::format("Ошибка резьбы: {}", failure.GetMessageString())};
    }
}

Expected<TopoDS_Shape> ThreadTool(const TopoDS_Shape& body, const doc::ThreadFeature& f) {
    if (!f.face) return Error{"Щёлкните цилиндрическую грань: вал или стенку отверстия"};
    for (const Parameter* p : {&f.pitch, &f.length})
        if (p->HasError()) return Error{p->error};
    const double pitch = f.pitch.value;
    if (pitch <= 1e-3) return Error{"Шаг резьбы должен быть больше нуля"};
    try {
        const Expected<Site> found = Find(body, *f.face);
        if (!found) return found.GetError();
        const Site& s = *found;
        const double depth = (s.external ? kExternalDepth : kInternalDepth) * pitch;
        if (s.external && depth >= s.radius * 0.9) return Error{"Шаг слишком крупный для такого диаметра"};
        const double faceLength = s.hi - s.lo;
        double length = faceLength;
        if (!f.fullLength) {
            if (f.length.value <= 1e-6) return Error{"Длина резьбы должна быть больше нуля"};
            length = std::min(f.length.value, faceLength);
        }
        // The helix runs along the axis from the chosen end of the face.
        Vec3 axis = s.axisDir;
        double begin = s.lo;
        if (f.fromOtherEnd) {
            axis = -axis;
            begin = -s.hi;
        }
        double end = begin + length;
        const Vec3 across = std::abs(axis.z) < 0.9 ? axis.Cross({0, 0, 1}).Normalized() : axis.Cross({1, 0, 0}).Normalized();
        // An open end (the tip of a shaft, the mouth of a hole): the groove
        // runs out past it. A closed one (a shoulder, the bottom of a blind
        // hole): the groove stops short of it rather than cutting into it.
        const double half = (depth + kOvershoot * pitch) * std::tan(kPi / 6);
        const double side = s.external ? -1.0 : 1.0;
        BRepClass3d_SolidClassifier classifier(body);
        const auto open = [&](double along, double outward) {
            const Vec3 p = s.axisPoint + axis * (along + outward * std::min(0.2 * pitch, 0.5)) + across * (s.radius + side * depth / 2);
            classifier.Perform(gp_Pnt(p.x, p.y, p.z), 1e-7);
            return classifier.State() == TopAbs_OUT;
        };
        begin += open(begin, -1) ? -pitch : half;
        if (length >= faceLength - 1e-9) end += open(end, 1) ? pitch : -half;
        if (end - begin < pitch) return Error{"Грань слишком короткая для резьбы с таким шагом"};
        length = end - begin;
        const double turns = length / pitch;
        if (turns > 1000) return Error{"Слишком много витков (больше 1000)"};

        const Vec3 origin = s.axisPoint + axis * begin;
        const gp_Dir ax(axis.x, axis.y, axis.z);
        const gp_Ax3 frame(gp_Pnt(origin.x, origin.y, origin.z), ax, gp_Dir(across.x, across.y, across.z));
        Handle(Geom_CylindricalSurface) cylinder = new Geom_CylindricalSurface(frame, s.radius);
        const int pieces = std::max(1, static_cast<int>(std::ceil(turns * 2 - 1e-9)));   // half turns
        const double angle = 2 * kPi * turns * (f.leftHanded ? -1.0 : 1.0);
        BRepBuilderAPI_MakeWire helixWire;
        for (int i = 0; i < pieces; ++i) {
            const double t0 = static_cast<double>(i) / pieces, t1 = static_cast<double>(i + 1) / pieces;
            const Handle(Geom2d_TrimmedCurve) segment =
                GCE2d_MakeSegment(gp_Pnt2d(angle * t0, length * t0), gp_Pnt2d(angle * t1, length * t1));
            TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(segment, cylinder);
            BRepLib::BuildCurves3d(edge);
            helixWire.Add(edge);
        }
        if (!helixWire.IsDone()) return Error{"Резьба: не удалось построить винтовую линию"};

        // The 60° groove in the plane through the axis at the helix start:
        // its base a little outside the surface, its tip `depth` into the material.
        const double over = kOvershoot * pitch;
        const auto at = [&](double radial, double along) {
            const Vec3 p = origin + across * (s.radius + radial) + axis * along;
            return gp_Pnt(p.x, p.y, p.z);
        };
        BRepBuilderAPI_MakePolygon triangle(at(-side * over, -half), at(-side * over, half), at(side * depth, 0), true);
        if (!triangle.IsDone()) return Error{"Резьба: не удалось построить профиль"};

        BRepOffsetAPI_MakePipeShell sweep(helixWire.Wire());
        sweep.SetMode(ax);
        sweep.Add(triangle.Wire(), false, false);
        sweep.Build();
        if (!sweep.IsDone() || !sweep.MakeSolid()) return Error{"Резьба не строится с такими размерами"};
        return sweep.Shape();
    } catch (const Standard_Failure& failure) {
        return Error{std::format("Ошибка резьбы: {}", failure.GetMessageString())};
    }
}

} // namespace bz::kernel::occt
