#include "kernel/occt/Thread.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <gp_Ax1.hxx>
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
#include <optional>
#include <utility>
#include <vector>

namespace bz::kernel::occt {
namespace {

// The groove is the basic profile of the kind (doc::BasicProfile: metric
// ISO 68-1, pipe G ISO 228-1, trapezoidal ISO 2904) — a trapezoid as wide as
// the space between two teeth at the part's surface, narrowing along the
// flanks to a flat root — moved and deepened by the fit (doc::CutOf).
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

Expected<ThreadSite> FindThreadSite(const TopoDS_Shape& body, const doc::CylinderTouch& ref, doc::ThreadStandard standard) {
    try {
        const Expected<Site> s = Find(body, ref);
        if (!s) return s.GetError();
        const double d = 2 * s->radius;
        // A hole is the minor diameter: add back the depth of the thread
        // that a tap of the matching size would cut.
        double nominal = d;
        if (!s->external)
            for (int i = 0; i < 3; ++i)
                nominal = d + 2 * doc::BasicProfile(standard, doc::StandardPitch(standard, nominal), false).minorOffset;
        return ThreadSite{s->external, doc::NearestStandardSize(standard, nominal), s->hi - s->lo};
    } catch (const Standard_Failure& failure) {
        return Error{std::format("Ошибка резьбы: {}", failure.GetMessageString())};
    }
}

namespace {

/// Where and how a thread runs, worked out once for the cut and for the
/// cosmetic drawing.
struct Layout {
    Site   site;
    double pitch = 0;
    doc::ThreadCut cut;        ///< the groove (anchor radius, depth, mouth) and the turned or bored surface
    double flank = 0;          ///< tan of the half angle: the flank's run along per unit of depth
    double begin = 0, end = 0; ///< along `axis` from the face's axis point
    Vec3   axis, across;
    double side = -1;          ///< -1 shaft (the groove goes in), +1 hole
    /// Open ends for the lead-in chamfer: where (along `axis`) and which way is out (±1).
    std::vector<std::pair<double, double>> openEnds;
};

/// The helix as a whole number of half turns, each edge exactly half a turn.
/// Spread evenly over a length that is not, every edge is a little short of
/// half a turn, and on some lengths (14, 16, 18, 19, 22 mm of an M8 shaft)
/// the MinGW build of OCCT 7.9.3 (Windows) faults cutting the groove
/// (0xc0000005; found 25.10.2026, Linux and macOS never did). The extra
/// part runs out past an open end, where it cuts nothing; with no open end
/// the thread ends up to half a pitch earlier.
void WholeHalfTurns(Layout& l, bool beginOpen, bool endOpen) {
    const double halfTurn = l.pitch / 2;
    const double length = l.end - l.begin;
    if (endOpen || beginOpen) {
        const double whole = std::ceil(length / halfTurn - 1e-9) * halfTurn;
        if (endOpen) l.end = l.begin + whole;
        else l.begin = l.end - whole;
    } else {
        l.end = l.begin + std::max(2.0, std::floor(length / halfTurn + 1e-9)) * halfTurn;
    }
}

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
    if (f.spec.fit == doc::ThreadFit::Custom && f.spec.clearance.HasError()) return Error{f.spec.clearance.error};
    l.side = s.external ? -1.0 : 1.0;
    // A chosen standard size wins over the face: a shaft is cut down to
    // it, a hole bored out to its minor diameter. Without one the face is
    // the basic surface (a shaft's major, a hole's minor diameter).
    const double nominal = f.size > 0 ? f.size : (s.external ? 2 * s.radius : 0.0);
    l.cut = doc::CutOf(f.spec, nominal, l.pitch, s.external, f.size > 0 ? std::nullopt : std::optional<double>(s.radius));
    l.flank = std::tan(l.cut.halfAngle);
    if (l.cut.anchor <= 1e-3 || l.cut.surface <= 1e-3) return Error{"Шаг слишком крупный для такого диаметра"};
    if (s.external && l.cut.depth >= l.cut.anchor * 0.9) return Error{"Шаг слишком крупный для такого диаметра"};
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
    const double half = l.cut.mouth / 2 + kOvershoot * l.pitch * l.flank;
    BRepClass3d_SolidClassifier classifier(body);
    const auto open = [&](double along, double outward) {
        const Vec3 p = s.axisPoint + axis * (along + outward * std::min(0.2 * l.pitch, 0.5)) + l.across * (s.radius + l.side * l.cut.depth / 2);
        classifier.Perform(gp_Pnt(p.x, p.y, p.z), 1e-7);
        return classifier.State() == TopAbs_OUT;
    };
    if (open(l.begin, -1)) {
        l.openEnds.push_back({l.begin, -1.0});
        l.begin -= l.pitch;
    } else {
        l.begin += half;
    }
    if (length >= faceLength - 1e-9) {
        if (open(l.end, 1)) {
            l.openEnds.push_back({l.end, 1.0});
            l.end += l.pitch;
        } else {
            l.end -= half;
        }
    }
    if (l.end - l.begin < l.pitch) return Error{"Грань слишком короткая для резьбы с таким шагом"};
    if ((l.end - l.begin) / l.pitch > 1000) return Error{"Слишком много витков (больше 1000)"};
    const auto openAt = [&](double outward) {
        return std::ranges::any_of(l.openEnds, [&](const auto& e) { return e.second == outward; });
    };
    WholeHalfTurns(l, openAt(-1.0), openAt(1.0));
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

/// The helical groove of a planned thread (the solid that is cut out).
Expected<TopoDS_Shape> Groove(const Layout& l, bool leftHanded) {
    const Expected<TopoDS_Wire> helix = Helix(l, l.cut.anchor, leftHanded);
    if (!helix) return helix.GetError();

    // The groove in the plane through the axis at the helix start: its
    // mouth a little outside the surface, its flat root `depth` into
    // the material (see the profile above).
    const double over = kOvershoot * l.pitch;
    const double top = l.cut.mouth / 2 + over * l.flank;          // half width a little outside
    const double root = l.cut.mouth / 2 - l.cut.depth * l.flank;  // half width at the root
    const Vec3 origin = l.site.axisPoint + l.axis * l.begin;
    const auto at = [&](double radial, double along) {
        const Vec3 p = origin + l.across * (l.cut.anchor + radial) + l.axis * along;
        return gp_Pnt(p.x, p.y, p.z);
    };
    BRepBuilderAPI_MakePolygon profile;
    profile.Add(at(-l.side * over, -top));
    profile.Add(at(-l.side * over, top));
    profile.Add(at(l.side * l.cut.depth, root));
    profile.Add(at(l.side * l.cut.depth, -root));
    profile.Close();
    if (!profile.IsDone()) return Error{"Резьба: не удалось построить профиль"};

    BRepOffsetAPI_MakePipeShell sweep(*helix);
    sweep.SetMode(gp_Dir(l.axis.x, l.axis.y, l.axis.z));
    sweep.Add(profile.Wire(), false, false);
    sweep.Build();
    if (!sweep.IsDone() || !sweep.MakeSolid()) return Error{"Резьба не строится с такими размерами"};
    return sweep.Shape();
}

/// The lead-in chamfers at the open ends: rings of 45° that take the
/// crests off from the surface down to the root, so the first turn starts
/// whole (and a printed first layer's bulge goes with them).
TopoDS_Shape Chamfers(const Layout& l) {
    TopoDS_Shape out;
    const double over = kOvershoot * l.pitch;
    const double r0 = l.cut.surface;
    const double r1 = l.cut.anchor + l.side * l.cut.depth;   // the root
    const double deep = std::abs(r1 - r0);
    for (const auto& [end, outward] : l.openEnds) {
        const auto at = [&](double along, double radius) {
            const Vec3 p = l.site.axisPoint + l.axis * along + l.across * std::max(radius, 1e-3);
            return gp_Pnt(p.x, p.y, p.z);
        };
        // A triangle in a plane through the axis: its long side is the cone
        // through (end, root) and (end - depth, surface), the rest outside.
        // Outside it reaches further than the groove (1.7 over, not 1): their
        // faces must not lie on one cylinder, the fuse fails on such.
        const double reach = 1.7 * over;
        BRepBuilderAPI_MakePolygon triangle(at(end + outward * over, r1 + l.side * over),
                                            at(end - outward * (deep + reach), r0 - l.side * reach),
                                            at(end + outward * over, r0 - l.side * reach), true);
        if (!triangle.IsDone()) continue;
        const BRepBuilderAPI_MakeFace face(triangle.Wire());
        if (!face.IsDone()) continue;
        const gp_Ax1 axis(gp_Pnt(l.site.axisPoint.x, l.site.axisPoint.y, l.site.axisPoint.z), gp_Dir(l.axis.x, l.axis.y, l.axis.z));
        const TopoDS_Shape ring = BRepPrimAPI_MakeRevol(face.Face(), axis, 2 * kPi).Shape();
        out = out.IsNull() ? ring : BRepAlgoAPI_Fuse(out, ring).Shape();
    }
    return out;
}

/// A solid cylinder (radius `r`) along the layout's thread length.
TopoDS_Shape Rod(const Layout& l, double r) {
    const Vec3 o = l.site.axisPoint + l.axis * l.begin;
    return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(o.x, o.y, o.z), gp_Dir(l.axis.x, l.axis.y, l.axis.z)), r, l.end - l.begin).Shape();
}

} // namespace

Expected<ThreadShapes> ThreadTool(const TopoDS_Shape& body, const doc::ThreadFeature& f) {
    try {
        const Expected<Layout> planned = Plan(body, f);
        if (!planned) return planned.GetError();
        const Layout& l = *planned;
        const Expected<TopoDS_Shape> groove = Groove(l, f.leftHanded);
        if (!groove) return groove.GetError();
        TopoDS_Shape tool = *groove;

        // A face of another diameter than the chosen size. Too much material
        // (a thicker shaft, a narrower hole): what lies outside the major
        // diameter, or inside the minor one, is cut too. Too little (a thinner
        // shaft, a wider hole): material is added up to the crest first, along
        // the face only, so the thread is whole instead of cut short.
        // A fit's flat crests are the same: the shaft turned or the hole
        // bored to the cut's surface.
        ThreadShapes out;
        const double over = kOvershoot * l.pitch;
        const double r = l.site.radius;
        const double target = l.cut.surface;
        if (l.site.external && r > target + 1e-6) {
            tool = BRepAlgoAPI_Fuse(tool, BRepAlgoAPI_Cut(Rod(l, r + over), Rod(l, target)).Shape()).Shape();
        } else if (!l.site.external && r < target - 1e-6) {
            tool = BRepAlgoAPI_Fuse(tool, Rod(l, target)).Shape();
        } else if (std::abs(r - target) > 1e-6) {
            Layout onFace = l;   // the face's own length: nothing added past its ends
            const double faceStart = f.fromOtherEnd ? -l.site.hi : l.site.lo;
            onFace.begin = std::max(l.begin, faceStart);
            onFace.end = std::min(l.end, faceStart + (l.site.hi - l.site.lo));
            if (onFace.end - onFace.begin > 1e-6) {
                const double inner = std::min(r, target), outer = std::max(r, target);
                // A hair into the face so the two solids fuse into one.
                const double hair = std::min(0.01, (outer - inner) / 4);
                out.fill = l.site.external ? BRepAlgoAPI_Cut(Rod(onFace, outer), Rod(onFace, inner - hair)).Shape()
                                           : BRepAlgoAPI_Cut(Rod(onFace, outer + hair), Rod(onFace, inner)).Shape();
            }
        }
        if (f.spec.leadChamfer && !l.openEnds.empty())
            if (TopoDS_Shape c = Chamfers(l); !c.IsNull()) out.chamfer = std::move(c);
        out.cut = tool;
        return out;
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

Expected<TopoDS_Shape> HoleThreadGroove(Vec3 top, Vec3 down, const doc::ThreadCut& cut, double pitch, double length,
                                        bool leftHanded, bool throughEnd) {
    if (pitch <= 1e-3) return Error{"Шаг резьбы должен быть больше нуля"};
    if (length < pitch) return Error{"Резьба короче одного шага"};
    if (length / pitch > 1000) return Error{"Слишком много витков (больше 1000)"};
    try {
        Layout l;
        l.pitch = pitch;
        l.cut = cut;
        l.flank = std::tan(cut.halfAngle);
        l.side = 1.0;
        if (cut.anchor <= 1e-3 || cut.surface <= 1e-3) return Error{"Шаг слишком крупный для такого диаметра"};
        l.site.axisPoint = top;
        l.site.axisDir = down.Normalized();
        l.site.radius = cut.surface;
        l.site.external = false;
        l.axis = l.site.axisDir;
        l.across = std::abs(l.axis.z) < 0.9 ? l.axis.Cross({0, 0, 1}).Normalized() : l.axis.Cross({1, 0, 0}).Normalized();
        // The mouth of the hole is open: the groove starts a pitch above it.
        // At the bottom of a blind hole it stops a groove's width short.
        const double half = l.cut.mouth / 2 + kOvershoot * pitch * l.flank;
        l.begin = -pitch;
        l.end = throughEnd ? length + pitch : length - half;
        if (l.end - l.begin < pitch) return Error{"Отверстие слишком мелкое для резьбы с таким шагом"};
        WholeHalfTurns(l, true, throughEnd);
        return Groove(l, leftHanded);
    } catch (const Standard_Failure& failure) {
        return Error{std::format("Ошибка резьбы: {}", failure.GetMessageString())};
    }
}

} // namespace bz::kernel::occt
