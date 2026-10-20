#include "kernel/occt/Project.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepBndLib.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_MapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Ax1.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cmath>
#include <deque>
#include <format>

namespace bz::kernel::occt {
namespace {

Vec3 ToVec(const gp_Pnt& p) { return {p.X(), p.Y(), p.Z()}; }
Vec3 ToVec(const gp_Dir& d) { return {d.X(), d.Y(), d.Z()}; }

/// A circle or arc not parallel to the sketch plane. Seen edge-on (its plane
/// across the sketch plane: the rim of a cylinder looked at from the side) it
/// covers a line; a whole circle at a slant is an ellipse.
Expected<ProjectedCurve> ProjectSlantedCircle(const BRepAdaptor_Curve& curve, const gp_Circ& circle, bool closed,
                                              const Frame& frame) {
    const Vec3 axis = ToVec(circle.Axis().Direction());
    const Vec3 centre = ToVec(circle.Location());
    const double r = circle.Radius();
    const double facing = std::abs(axis.Dot(frame.normal));
    // The circle's diameter that stays its full length on the plane.
    const Vec3 along = axis.Cross(frame.normal).Normalized();
    ProjectedCurve out;
    if (facing < 1e-7) {
        // The extreme positions along that diameter: the ends of the arc and,
        // if the arc passes them, the points where it turns back.
        const gp_Ax2& pos = circle.Position();
        const double cx = ToVec(pos.XDirection()).Dot(along), cy = ToVec(pos.YDirection()).Dot(along);
        const double t0 = std::atan2(cy, cx);
        double lo = 1e300, hi = -1e300;
        const auto take = [&](double t) {
            const double s = r * (std::cos(t) * cx + std::sin(t) * cy);
            lo = std::min(lo, s);
            hi = std::max(hi, s);
        };
        const double a = curve.FirstParameter(), b = curve.LastParameter();
        take(a);
        take(b);
        for (const double t : {t0, t0 + kPi})
            for (int k = -2; k <= 2; ++k)
                if (const double tk = t + 2 * kPi * k; closed || (tk > a && tk < b)) take(tk);
        if (hi - lo < 1e-6) return Error{"Дуга смотрит прямо на плоскость эскиза — в проекции точка"};
        out.kind = ProjectedCurve::Kind::Line;
        out.start = frame.ToLocal(centre + along * lo);
        out.end = frame.ToLocal(centre + along * hi);
        return out;
    }
    if (!closed) return Error{"Дуга не параллельна плоскости эскиза — в проекции дуга эллипса (пока не поддерживается)"};
    // The other axis: the circle's diameter across `along`, shortened by the slant.
    const Vec3 across = axis.Cross(along).Normalized();
    out.kind = ProjectedCurve::Kind::Ellipse;
    out.centre = frame.ToLocal(centre);
    out.start = frame.ToLocal(centre + along * r);
    out.end = frame.ToLocal(centre + across * r);
    return out;
}

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
            const bool closed = first.IsNull() || BRep_Tool::Pnt(first).Distance(BRep_Tool::Pnt(last)) < 1e-7;
            if (std::abs(facing) < 0.9999) return ProjectSlantedCircle(curve, circle, closed, frame);
            out.centre = frame.ToLocal(ToVec(circle.Location()));
            out.radius = circle.Radius();
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

// --- bodies of revolution -------------------------------------------------

namespace bz::kernel::occt {
namespace {

constexpr double kLinear = 1e-6;

/// The axis of a surface of revolution; nullopt for any other surface.
/// A sphere has no axis of its own: it gets one only from a neighbour.
std::optional<gp_Ax1> AxisOf(const BRepAdaptor_Surface& s) {
    switch (s.GetType()) {
        case GeomAbs_Cylinder:            return s.Cylinder().Axis();
        case GeomAbs_Cone:                return s.Cone().Axis();
        case GeomAbs_Torus:               return s.Torus().Axis();
        case GeomAbs_Sphere:              return s.Sphere().Position().Axis();
        case GeomAbs_SurfaceOfRevolution: return s.AxeOfRevolution();
        default:                          return std::nullopt;
    }
}

bool OnAxis(const gp_Ax1& axis, const gp_Pnt& p) { return gp_Lin(axis).Distance(p) < 1e-5; }

bool SameAxis(const gp_Ax1& a, const gp_Ax1& b) {
    return a.Direction().IsParallel(b.Direction(), 1e-6) && OnAxis(a, b.Location());
}

/// The face turns around `axis`: a coaxial surface of revolution, a sphere
/// centred on it, or a flat ring/disc across it (all its edges coaxial circles).
bool AroundAxis(const TopoDS_Face& face, const gp_Ax1& axis) {
    const BRepAdaptor_Surface s(face);
    if (s.GetType() == GeomAbs_Sphere) return OnAxis(axis, s.Sphere().Location());
    if (const std::optional<gp_Ax1> own = AxisOf(s)) return SameAxis(*own, axis);
    if (s.GetType() != GeomAbs_Plane || !s.Plane().Axis().Direction().IsParallel(axis.Direction(), 1e-6)) return false;
    for (TopExp_Explorer e(face, TopAbs_EDGE); e.More(); e.Next()) {
        const TopoDS_Edge& edge = TopoDS::Edge(e.Current());
        if (BRep_Tool::Degenerated(edge)) continue;
        const BRepAdaptor_Curve c(edge);
        if (c.GetType() != GeomAbs_Circle || !SameAxis(c.Circle().Axis(), axis)) return false;
    }
    return true;
}

Vec3 ToVec(const gp_Vec& v) { return {v.X(), v.Y(), v.Z()}; }

/// A section curve (OCCT gives them as B-splines) that is really a straight
/// line or an arc on the sketch plane, as that one curve: the meridian of a
/// sphere or a torus is a circle. As short lines it was hundreds of them —
/// a heavy sketch, and turning them made a revolve OCCT faulted on.
std::optional<ProjectedCurve> FitLineOrArc(const BRepAdaptor_Curve& curve, const Frame& frame, double tolerance) {
    constexpr int steps = 48;
    std::vector<Vec2> p;
    for (int i = 0; i <= steps; ++i)
        p.push_back(frame.ToLocal(ToVec(curve.Value(curve.FirstParameter() + (curve.LastParameter() - curve.FirstParameter()) * i / steps))));
    const Vec2 a = p.front(), m = p[steps / 2], b = p.back();
    const bool closed = (b - a).Length() < tolerance;
    if (!closed && (b - a).Length() > tolerance) {
        const Vec2 d = (b - a).Normalized();
        if (std::ranges::all_of(p, [&](Vec2 q) { return std::abs(d.Cross(q - a)) < tolerance; })) {
            ProjectedCurve line;
            line.start = a;
            line.end = b;
            return line;
        }
    }
    // The circle through three points (for a closed curve: a quarter and three quarters round).
    const Vec2 q1 = closed ? p[steps / 4] : a, q2 = m, q3 = closed ? p[3 * steps / 4] : b;
    const double det = 2 * ((q2.x - q1.x) * (q3.y - q1.y) - (q2.y - q1.y) * (q3.x - q1.x));
    if (std::abs(det) < 1e-12) return std::nullopt;
    const double s1 = q2.x * q2.x - q1.x * q1.x + q2.y * q2.y - q1.y * q1.y;
    const double s2 = q3.x * q3.x - q1.x * q1.x + q3.y * q3.y - q1.y * q1.y;
    const Vec2 c{(s1 * (q3.y - q1.y) - s2 * (q2.y - q1.y)) / det, (s2 * (q2.x - q1.x) - s1 * (q3.x - q1.x)) / det};
    const double r = (q1 - c).Length();
    if (!std::ranges::all_of(p, [&](Vec2 q) { return std::abs((q - c).Length() - r) < tolerance; })) return std::nullopt;
    ProjectedCurve arc;
    arc.centre = c;
    arc.radius = r;
    if (closed) {
        arc.kind = ProjectedCurve::Kind::Circle;
        return arc;
    }
    arc.kind = ProjectedCurve::Kind::Arc;
    const auto angle = [&](Vec2 q) { return std::atan2(q.y - c.y, q.x - c.x); };
    const auto ccwSpan = [](double from, double to) {
        double d = to - from;
        while (d < 0) d += 2 * kPi;
        return d;
    };
    arc.start = a;
    arc.end = b;
    if (ccwSpan(angle(a), angle(m)) > ccwSpan(angle(a), angle(b))) std::swap(arc.start, arc.end);
    return arc;
}

/// Curves of `shape` (section edges) on the sketch plane: lines and circles
/// as they are, curves that are really lines or arcs as those, anything else
/// as a chain of short lines.
void AddCurves(const TopoDS_Shape& shape, const Frame& frame, double deflection, std::vector<ProjectedCurve>& out) {
    for (TopExp_Explorer e(shape, TopAbs_EDGE); e.More(); e.Next()) {
        const TopoDS_Edge& edge = TopoDS::Edge(e.Current());
        if (BRep_Tool::Degenerated(edge)) continue;
        const BRepAdaptor_Curve curve(edge);
        if (curve.GetType() == GeomAbs_Line || curve.GetType() == GeomAbs_Circle) {
            if (const Expected<ProjectedCurve> pc = ProjectEdge(edge, frame)) {
                out.push_back(*pc);
                continue;
            }
            if (curve.GetType() == GeomAbs_Line) continue;   // seen end-on: a point
        }
        if (const std::optional<ProjectedCurve> simple = FitLineOrArc(curve, frame, std::max(deflection * 0.05, 1e-5))) {
            out.push_back(*simple);
            continue;
        }
        GCPnts_QuasiUniformDeflection points(curve, deflection);
        if (!points.IsDone() || points.NbPoints() < 2) continue;
        Vec2 prev = frame.ToLocal(ToVec(points.Value(1)));
        for (int i = 2; i <= points.NbPoints(); ++i) {
            const Vec2 next = frame.ToLocal(ToVec(points.Value(i)));
            if ((next - prev).Length() < kLinear) continue;
            ProjectedCurve line;
            line.start = prev;
            line.end = next;
            out.push_back(line);
            prev = next;
        }
    }
}

bool SameLine(const ProjectedCurve& a, const ProjectedCurve& b) {
    if (a.kind != ProjectedCurve::Kind::Line || b.kind != ProjectedCurve::Kind::Line) return false;
    constexpr double tol = 1e-5;
    return ((a.start - b.start).Length() < tol && (a.end - b.end).Length() < tol) ||
           ((a.start - b.end).Length() < tol && (a.end - b.start).Length() < tol);
}

/// The rims of the faces (circles around the axis that are not shared by two
/// of them) seen edge-on: the lines that close the outline where no flat end
/// face does it already — a cylinder boss standing on a plate, a cylinder
/// clicked on its own. Together with the profile's sides they make the
/// rectangle a cylinder is from the side.
void AddRims(const TopoDS_Shape& faces, const gp_Ax1& axis, const Frame& frame, std::vector<ProjectedCurve>& out) {
    TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
    TopExp::MapShapesAndAncestors(faces, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
    for (int i = 1; i <= edgeFaces.Extent(); ++i) {
        const TopoDS_Edge& edge = TopoDS::Edge(edgeFaces.FindKey(i));
        if (BRep_Tool::Degenerated(edge) || edgeFaces(i).Extent() > 1) continue;
        const BRepAdaptor_Curve c(edge);
        if (c.GetType() != GeomAbs_Circle || !SameAxis(c.Circle().Axis(), axis)) continue;
        const Expected<ProjectedCurve> rim = ProjectEdge(edge, frame);
        if (!rim) continue;
        if (std::none_of(out.begin(), out.end(), [&](const ProjectedCurve& o) { return SameLine(o, *rim); })) out.push_back(*rim);
    }
}

TopoDS_Shape Section(const TopoDS_Shape& faces, const gp_Pln& plane) {
    BRepAlgoAPI_Section section(faces, plane, false);
    section.ComputePCurveOn1(false);
    section.Approximation(true);
    section.Build();
    return section.IsDone() ? section.Shape() : TopoDS_Shape();
}

/// Sample points of every edge of `shape` (ends included).
std::vector<gp_Pnt> Samples(const TopoDS_Shape& shape, int perEdge) {
    std::vector<gp_Pnt> out;
    for (TopExp_Explorer e(shape, TopAbs_EDGE); e.More(); e.Next()) {
        const TopoDS_Edge& edge = TopoDS::Edge(e.Current());
        if (BRep_Tool::Degenerated(edge)) continue;
        const BRepAdaptor_Curve c(edge);
        for (int i = 0; i <= perEdge; ++i)
            out.push_back(c.Value(c.FirstParameter() + (c.LastParameter() - c.FirstParameter()) * i / perEdge));
    }
    return out;
}

} // namespace

Expected<RevolvedProjection> ProjectRevolved(const TopoDS_Shape& body, const TopoDS_Face& face, const Frame& frame) {
    try {
        std::optional<gp_Ax1> axis = AxisOf(BRepAdaptor_Surface(face));
        if (!axis) return Error{"Это не поверхность вращения"};

        // The faces around the same axis, connected to the clicked one.
        TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
        TopExp::MapShapesAndAncestors(body, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
        // A clicked sphere takes its axis from a neighbour of revolution, if any.
        if (BRepAdaptor_Surface(face).GetType() == GeomAbs_Sphere)
            for (TopExp_Explorer e(face, TopAbs_EDGE); e.More(); e.Next()) {
                if (!edgeFaces.Contains(e.Current())) continue;
                for (const TopoDS_Shape& other : edgeFaces.FindFromKey(e.Current())) {
                    const BRepAdaptor_Surface s(TopoDS::Face(other));
                    const std::optional<gp_Ax1> own = AxisOf(s);
                    if (own && s.GetType() != GeomAbs_Sphere && OnAxis(*own, axis->Location())) axis = own;
                }
            }
        TopTools_MapOfShape taken;
        std::deque<TopoDS_Face> queue{face};
        taken.Add(face);
        BRep_Builder builder;
        TopoDS_Compound faces;
        builder.MakeCompound(faces);
        while (!queue.empty()) {
            const TopoDS_Face current = queue.front();
            queue.pop_front();
            builder.Add(faces, current);
            for (TopExp_Explorer e(current, TopAbs_EDGE); e.More(); e.Next()) {
                if (!edgeFaces.Contains(e.Current())) continue;
                for (const TopoDS_Shape& other : edgeFaces.FindFromKey(e.Current()))
                    if (!taken.Contains(other) && AroundAxis(TopoDS::Face(other), *axis)) {
                        taken.Add(other);
                        queue.push_back(TopoDS::Face(other));
                    }
            }
        }

        Bnd_Box box;
        BRepBndLib::Add(faces, box);
        const double size = box.IsVoid() ? 1.0 : std::sqrt(box.SquareExtent());
        const double deflection = std::max(size * 2e-3, 1e-3);

        const gp_Pnt o = axis->Location();
        const gp_Dir a = axis->Direction();
        const gp_Dir n(frame.normal.x, frame.normal.y, frame.normal.z);
        const double facing = std::abs(gp_Vec(a).Dot(gp_Vec(n)));
        const bool endOn = facing > 1 - 1e-9, along = facing < 1e-6;

        // A plane through the axis: its cut is the profile, both sides.
        const gp_Dir meridianNormal = along ? n : gp_Dir(gp_Vec(a).Crossed(gp_Vec(endOn ? gp_Dir(frame.xDir.x, frame.xDir.y, frame.xDir.z) : n)));
        const TopoDS_Shape meridian = Section(faces, gp_Pln(o, meridianNormal));
        const std::vector<gp_Pnt> profile = Samples(meridian, 32);
        if (profile.empty()) return Error{"Не удалось найти контур тела вращения"};

        RevolvedProjection out;
        const auto along_axis = [&](const gp_Pnt& p) { return gp_Vec(o, p).Dot(gp_Vec(a)); };
        if (endOn) {
            out.centre = frame.ToLocal(ToVec(o));
            // Seen along the axis the outline is a set of circles: the radii at
            // the ends of the profile curves and where a curve turns back.
            std::vector<double> radii;
            const gp_Lin line(*axis);
            for (TopExp_Explorer e(meridian, TopAbs_EDGE); e.More(); e.Next()) {
                const BRepAdaptor_Curve c(TopoDS::Edge(e.Current()));
                constexpr int steps = 64;
                std::vector<double> r;
                for (int i = 0; i <= steps; ++i)
                    r.push_back(line.Distance(c.Value(c.FirstParameter() + (c.LastParameter() - c.FirstParameter()) * i / steps)));
                radii.push_back(r.front());
                radii.push_back(r.back());
                for (std::size_t i = 1; i + 1 < r.size(); ++i)
                    if ((r[i] > r[i - 1] && r[i] >= r[i + 1]) || (r[i] < r[i - 1] && r[i] <= r[i + 1])) radii.push_back(r[i]);
            }
            std::sort(radii.begin(), radii.end());
            for (const double r : radii) {
                if (r < 1e-4 || (!out.outline.empty() && r - out.outline.back().radius < 1e-4)) continue;
                ProjectedCurve circle;
                circle.kind = ProjectedCurve::Kind::Circle;
                circle.centre = *out.centre;
                circle.radius = r;
                out.outline.push_back(circle);
            }
        } else {
            double lo = 1e300, hi = -1e300;
            for (const gp_Pnt& p : profile) {
                lo = std::min(lo, along_axis(p));
                hi = std::max(hi, along_axis(p));
            }
            const double margin = std::max((hi - lo) * 0.05, 1.0);
            ProjectedCurve line;
            line.start = frame.ToLocal(ToVec(gp_Vec(o.XYZ()) + gp_Vec(a) * (lo - margin)));
            line.end = frame.ToLocal(ToVec(gp_Vec(o.XYZ()) + gp_Vec(a) * (hi + margin)));
            out.axis = line;
            if (along) {
                AddCurves(meridian, frame, deflection, out.outline);
                AddRims(faces, *axis, frame, out.outline);
            } else {
                out.outlineSkipped = true;
            }
        }

        // Where the sketch plane cuts the body (if it does, and is not the
        // plane through the axis already drawn as the outline).
        const gp_Pnt origin(frame.origin.x, frame.origin.y, frame.origin.z);
        const bool throughAxis = along && std::abs(gp_Vec(origin, o).Dot(gp_Vec(n))) < kLinear;
        if (!throughAxis) {
            const TopoDS_Shape cut = Section(faces, gp_Pln(origin, n));
            if (!cut.IsNull()) AddCurves(cut, frame, deflection, out.section);
            // End-on, a cut circle that repeats an outline circle is drawn once.
            if (endOn)
                std::erase_if(out.section, [&](const ProjectedCurve& c) {
                    return c.kind == ProjectedCurve::Kind::Circle &&
                           std::any_of(out.outline.begin(), out.outline.end(), [&](const ProjectedCurve& o2) {
                               return std::abs(o2.radius - c.radius) < 1e-4 && (o2.centre - c.centre).Length() < 1e-4;
                           });
                });
        }
        return out;
    } catch (const Standard_Failure& failure) {
        return Error{std::format("Не удалось спроецировать тело вращения: {}", failure.GetMessageString())};
    }
}

} // namespace bz::kernel::occt
