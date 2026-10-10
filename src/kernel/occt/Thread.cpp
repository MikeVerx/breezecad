#include "kernel/occt/Thread.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
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

namespace {

/// Where and how a thread runs, worked out once for the cut and for the
/// cosmetic drawing.
struct Layout {
    Site   site;
    double pitch = 0, depth = 0;
    double crest = 0;          ///< radius the thread is made at: the major (shaft) or minor (hole) radius
    double begin = 0, end = 0; ///< along `axis` from the face's axis point
    Vec3   axis, across;
    double side = -1;          ///< -1 shaft (the groove goes in), +1 hole
};

Expected<Layout> Plan(const TopoDS_Shape& body, const doc::ThreadFeature& f) {
    if (!f.face) return Error{"Щёлкните цилиндрическую грань: вал или стенку отверстия"};
    for (const Parameter* p : {&f.pitch, &f.length})
        if (p->HasError()) return Error{p->error};
    Layout l;
    l.pitch = f.pitch.value;
    if (l.pitch <= 1e-3) return Error{"Шаг резьбы должен быть больше нуля"};
    const Expected<Site> found = Find(body, *f.face);
    if (!found) return found.GetError();
    l.site = *found;
    const Site& s = l.site;
    l.depth = (s.external ? kExternalDepth : kInternalDepth) * l.pitch;
    l.side = s.external ? -1.0 : 1.0;
    // A chosen standard size wins over the face: a shaft is cut down to
    // it, a hole bored out to its minor diameter.
    l.crest = s.radius;
    if (f.size > 0) l.crest = s.external ? f.size / 2 : f.size / 2 - l.depth;
    if (l.crest <= 1e-3) return Error{"Шаг слишком крупный для такого диаметра"};
    if (s.external && l.depth >= l.crest * 0.9) return Error{"Шаг слишком крупный для такого диаметра"};
    const double faceLength = s.hi - s.lo;
    double length = faceLength;
    if (!f.fullLength) {
        if (f.length.value <= 1e-6) return Error{"Длина резьбы должна быть больше нуля"};
        length = std::min(f.length.value, faceLength);
    }
    // The helix runs along the axis from the chosen end of the face.
    l.axis = s.axisDir;
    l.begin = s.lo;
    if (f.fromOtherEnd) {
        l.axis = -l.axis;
        l.begin = -s.hi;
    }
    l.end = l.begin + length;
    const Vec3& axis = l.axis;
    l.across = std::abs(axis.z) < 0.9 ? axis.Cross({0, 0, 1}).Normalized() : axis.Cross({1, 0, 0}).Normalized();
    // An open end (the tip of a shaft, the mouth of a hole): the groove
    // runs out past it. A closed one (a shoulder, the bottom of a blind
    // hole): the groove stops short of it rather than cutting into it.
    const double half = (l.depth + kOvershoot * l.pitch) * std::tan(kPi / 6);
    BRepClass3d_SolidClassifier classifier(body);
    const auto open = [&](double along, double outward) {
        const Vec3 p = s.axisPoint + axis * (along + outward * std::min(0.2 * l.pitch, 0.5)) + l.across * (s.radius + l.side * l.depth / 2);
        classifier.Perform(gp_Pnt(p.x, p.y, p.z), 1e-7);
        return classifier.State() == TopAbs_OUT;
    };
    l.begin += open(l.begin, -1) ? -l.pitch : half;
    if (length >= faceLength - 1e-9) l.end += open(l.end, 1) ? l.pitch : -half;
    if (l.end - l.begin < l.pitch) return Error{"Грань слишком короткая для резьбы с таким шагом"};
    if ((l.end - l.begin) / l.pitch > 1000) return Error{"Слишком много витков (больше 1000)"};
    return l;
}

/// A helix of the layout's length at `radius`, as a wire of half-turn edges.
Expected<TopoDS_Wire> Helix(const Layout& l, double radius, bool leftHanded) {
    const double length = l.end - l.begin;
    const double turns = length / l.pitch;
    const Vec3 origin = l.site.axisPoint + l.axis * l.begin;
    const gp_Ax3 frame(gp_Pnt(origin.x, origin.y, origin.z), gp_Dir(l.axis.x, l.axis.y, l.axis.z), gp_Dir(l.across.x, l.across.y, l.across.z));
    Handle(Geom_CylindricalSurface) cylinder = new Geom_CylindricalSurface(frame, radius);
    const int pieces = std::max(1, static_cast<int>(std::ceil(turns * 2 - 1e-9)));   // half turns
    const double angle = 2 * kPi * turns * (leftHanded ? -1.0 : 1.0);
    BRepBuilderAPI_MakeWire wire;
    for (int i = 0; i < pieces; ++i) {
        const double t0 = static_cast<double>(i) / pieces, t1 = static_cast<double>(i + 1) / pieces;
        const Handle(Geom2d_TrimmedCurve) segment =
            GCE2d_MakeSegment(gp_Pnt2d(angle * t0, length * t0), gp_Pnt2d(angle * t1, length * t1));
        TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(segment, cylinder);
        BRepLib::BuildCurves3d(edge);
        wire.Add(edge);
    }
    if (!wire.IsDone()) return Error{"Резьба: не удалось построить винтовую линию"};
    return wire.Wire();
}

/// A solid cylinder (radius `r`) along the layout's thread length.
TopoDS_Shape Rod(const Layout& l, double r) {
    const Vec3 o = l.site.axisPoint + l.axis * l.begin;
    return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(o.x, o.y, o.z), gp_Dir(l.axis.x, l.axis.y, l.axis.z)), r, l.end - l.begin).Shape();
}

} // namespace

Expected<TopoDS_Shape> ThreadTool(const TopoDS_Shape& body, const doc::ThreadFeature& f) {
    try {
        const Expected<Layout> planned = Plan(body, f);
        if (!planned) return planned.GetError();
        const Layout& l = *planned;
        const Expected<TopoDS_Wire> helix = Helix(l, l.crest, f.leftHanded);
        if (!helix) return helix.GetError();

        // The 60° groove in the plane through the axis at the helix start:
        // its base a little outside the surface, its tip `depth` into the material.
        const double over = kOvershoot * l.pitch;
        const double half = (l.depth + over) * std::tan(kPi / 6);
        const Vec3 origin = l.site.axisPoint + l.axis * l.begin;
        const auto at = [&](double radial, double along) {
            const Vec3 p = origin + l.across * (l.crest + radial) + l.axis * along;
            return gp_Pnt(p.x, p.y, p.z);
        };
        BRepBuilderAPI_MakePolygon triangle(at(-l.side * over, -half), at(-l.side * over, half), at(l.side * l.depth, 0), true);
        if (!triangle.IsDone()) return Error{"Резьба: не удалось построить профиль"};

        BRepOffsetAPI_MakePipeShell sweep(*helix);
        sweep.SetMode(gp_Dir(l.axis.x, l.axis.y, l.axis.z));
        sweep.Add(triangle.Wire(), false, false);
        sweep.Build();
        if (!sweep.IsDone() || !sweep.MakeSolid()) return Error{"Резьба не строится с такими размерами"};
        TopoDS_Shape tool = sweep.Shape();

        // A face of another diameter than the chosen size: what lies outside
        // the shaft's major diameter, or inside the hole's minor one, goes too.
        const double r = l.site.radius;
        if (l.site.external && r > l.crest + 1e-6) {
            tool = BRepAlgoAPI_Fuse(tool, BRepAlgoAPI_Cut(Rod(l, r + over), Rod(l, l.crest)).Shape()).Shape();
        } else if (!l.site.external && r < l.crest - 1e-6) {
            tool = BRepAlgoAPI_Fuse(tool, Rod(l, l.crest)).Shape();
        }
        return tool;
    } catch (const Standard_Failure& failure) {
        return Error{std::format("Ошибка резьбы: {}", failure.GetMessageString())};
    }
}

Expected<TopoDS_Shape> CosmeticThread(const TopoDS_Shape& body, const doc::ThreadFeature& f) {
    try {
        Expected<Layout> planned = Plan(body, f);
        if (!planned) return planned.GetError();
        // Drawn only on the face: no run-out past its ends.
        Layout& l = *planned;
        const double faceStart = f.fromOtherEnd ? -l.site.hi : l.site.lo;
        l.begin = std::max(l.begin, faceStart);
        l.end = std::min(l.end, faceStart + (l.site.hi - l.site.lo));
        if (l.end - l.begin < 1e-6) return Error{"Грань слишком короткая для резьбы"};
        // On the face itself, a hair off it so it is not hidden in it.
        const double r = l.site.radius * (1.0 - l.side * 0.004);
        Expected<TopoDS_Wire> helix = Helix(l, r, f.leftHanded);
        if (!helix) return helix.GetError();
        return TopoDS_Shape(*helix);
    } catch (const Standard_Failure& failure) {
        return Error{std::format("Ошибка резьбы: {}", failure.GetMessageString())};
    }
}

} // namespace bz::kernel::occt
