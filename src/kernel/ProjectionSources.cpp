#include "kernel/ProjectionSources.h"

#include "document/Document.h"
#include "document/Origin.h"
#include "document/SketchFeature.h"
#include "document/WorkAxisFeature.h"
#include "document/WorkPlaneFeature.h"
#include "document/WorkPointFeature.h"
#include "kernel/Model.h"
#include "sheetmetal/SheetPart.h"

#include <algorithm>
#include <cmath>

namespace bz::kernel {
namespace {

constexpr double kTol = 1e-7;

ProjectedItem Point(Vec2 p) {
    ProjectedItem out;
    out.point = p;
    return out;
}

ProjectedItem Curve(const ProjectedCurve& c) {
    ProjectedItem out;
    out.curve = c;
    return out;
}

ProjectedCurve Line(Vec2 a, Vec2 b) {
    ProjectedCurve c;
    c.start = a;
    c.end = b;
    return c;
}

double AngleOf(Vec2 p, Vec2 c) { return std::atan2(p.y - c.y, p.x - c.x); }

/// The counter-clockwise sweep from `a` to `b` round `c`, in (0, 2π].
double Sweep(Vec2 a, Vec2 b, Vec2 c) {
    double s = AngleOf(b, c) - AngleOf(a, c);
    while (s <= 1e-12) s += 2 * kPi;
    return s;
}

/// A line through `p` along `d` (unit, 3D) on `plane`: `half` both ways from
/// `p`'s projection; a point when the line stands square to the plane.
ProjectedItem LineOrPoint(const Frame& plane, Vec3 p, Vec3 d, double half) {
    const Vec2 at = plane.ToLocal(p);
    Vec2 along{d.Dot(plane.xDir), d.Dot(plane.YDir())};
    if (along.Length() < 1e-9) return Point(at);
    along = along.Normalized();
    return Curve(Line(at - along * half, at + along * half));
}

} // namespace

Expected<ProjectedItem> ProjectPlanar(const Frame& from, const PlanarCurve& c, const Frame& to) {
    const auto world = [&](Vec2 p) { return from.ToWorld(p); };
    const auto local = [&](Vec2 p) { return to.ToLocal(world(p)); };
    switch (c.kind) {
        case PlanarCurve::Kind::Point: return Point(local(c.a));
        case PlanarCurve::Kind::Line: {
            const Vec2 a = local(c.a), b = local(c.b);
            if ((b - a).Length() < 1e-6) return Point(a);   // seen end-on
            return Curve(Line(a, b));
        }
        default: break;
    }
    const double facing = from.normal.Dot(to.normal);
    if (std::abs(facing) > 1.0 - kTol) {
        // Parallel planes: the same shape; looked at from behind, it runs the other way.
        ProjectedCurve out;
        out.centre = local(c.centre);
        out.radius = c.radius;
        if (c.kind == PlanarCurve::Kind::Circle) {
            out.kind = ProjectedCurve::Kind::Circle;
        } else if (c.kind == PlanarCurve::Kind::Arc) {
            out.kind = ProjectedCurve::Kind::Arc;
            out.start = local(facing > 0 ? c.a : c.b);
            out.end = local(facing > 0 ? c.b : c.a);
        } else {
            out.kind = ProjectedCurve::Kind::Ellipse;
            out.start = local(c.a);
            out.end = local(c.b);
        }
        return Curve(out);
    }
    if (c.kind == PlanarCurve::Kind::Ellipse) return Error{"Эллипс не параллелен плоскости эскиза — в проекции не эллипс того же вида"};
    // The circle's diameter that keeps its length: along the line where the planes cross.
    const Vec3 along = from.normal.Cross(to.normal).Normalized();
    const Vec3 centre = world(c.centre);
    if (std::abs(facing) < kTol) {
        // Seen edge-on: the stretch it covers along that diameter.
        double lo = -c.radius, hi = c.radius;
        if (c.kind == PlanarCurve::Kind::Arc) {
            const Vec2 u{along.Dot(from.xDir), along.Dot(from.YDir())};   // the diameter in the curve's own plane
            const double a0 = AngleOf(c.a, c.centre), sweep = Sweep(c.a, c.b, c.centre);
            const auto s = [&](double t) { return c.radius * (std::cos(t) * u.x + std::sin(t) * u.y); };
            lo = std::min(s(a0), s(a0 + sweep));
            hi = std::max(s(a0), s(a0 + sweep));
            const double t0 = std::atan2(u.y, u.x);
            for (const double t : {t0, t0 + kPi})
                for (int k = -2; k <= 2; ++k)
                    if (double tk = t + 2 * kPi * k, d = tk - a0; d > 0 && d < sweep) {
                        lo = std::min(lo, s(tk));
                        hi = std::max(hi, s(tk));
                    }
        }
        if (hi - lo < 1e-6) return Error{"Дуга смотрит прямо на плоскость эскиза — в проекции точка"};
        return Curve(Line(to.ToLocal(centre + along * lo), to.ToLocal(centre + along * hi)));
    }
    if (c.kind == PlanarCurve::Kind::Arc) return Error{"Дуга не параллельна плоскости эскиза — в проекции дуга эллипса (пока не поддерживается)"};
    const Vec3 across = from.normal.Cross(along).Normalized();
    ProjectedCurve out;
    out.kind = ProjectedCurve::Kind::Ellipse;
    out.centre = to.ToLocal(centre);
    out.start = to.ToLocal(centre + along * c.radius);
    out.end = to.ToLocal(centre + across * c.radius);
    return Curve(out);
}

Expected<ProjectedItem> ProjectWork(const doc::Document& doc, ObjectId work, const Frame& plane, double half) {
    namespace o = doc::origin;
    std::optional<Frame> workPlane;
    std::optional<std::pair<Vec3, Vec3>> axis;
    std::optional<Vec3> point;
    if (work == o::kPoint) point = Vec3{0, 0, 0};
    else if (work == o::kAxisX) axis = std::pair{Vec3{0, 0, 0}, Vec3{1, 0, 0}};
    else if (work == o::kAxisY) axis = std::pair{Vec3{0, 0, 0}, Vec3{0, 1, 0}};
    else if (work == o::kAxisZ) axis = std::pair{Vec3{0, 0, 0}, Vec3{0, 0, 1}};
    else if (const auto f = o::PlaneFrame(work)) workPlane = f;
    else if (const auto* wp = doc.FindAs<doc::WorkPlaneFeature>(work)) workPlane = wp->frame;
    else if (const auto* wa = doc.FindAs<doc::WorkAxisFeature>(work)) axis = wa->line;
    else if (const auto* pt = doc.FindAs<doc::WorkPointFeature>(work)) point = pt->position;
    else return Error{"Рабочий элемент удалён"};
    if (point) return Point(plane.ToLocal(*point));
    if (axis) return LineOrPoint(plane, axis->first, axis->second.Normalized(), half);
    if (!workPlane) return Error{"Рабочий элемент ещё не построен"};
    const Vec3 dir = plane.normal.Cross(workPlane->normal);
    if (dir.Length() < 1e-9) return Error{"Плоскость параллельна эскизу — линии пересечения нет"};
    // The point of the crossing line nearest the work plane's own origin:
    // in both planes, and moved from that origin square to the line only.
    const Vec3 d = dir.Normalized();
    const Vec3 n1 = plane.normal, n2 = workPlane->normal;
    const double h1 = n1.Dot(plane.origin), h2 = n2.Dot(workPlane->origin);
    const double k = n1.Dot(n2), det = 1.0 - k * k;
    const double c1 = (h1 - h2 * k) / det, c2 = (h2 - h1 * k) / det;
    const Vec3 onLine = n1 * c1 + n2 * c2;
    const Vec3 nearest = onLine + d * (workPlane->origin - onLine).Dot(d);
    return LineOrPoint(plane, nearest, d, half);
}

Expected<ProjectedItem> ProjectSketchEntity(const doc::Document& doc, const doc::SketchSource& source, const Frame& plane) {
    const auto* other = doc.FindAs<doc::SketchFeature>(source.sketch);
    if (!other) return Error{"Эскиз, из которого проецировали, удалён"};
    const sketch::Sketch& sk = other->sketch;
    const Frame& from = other->placement.frame;
    PlanarCurve c;
    if (const sketch::Point* p = sk.FindPoint(source.curve)) {
        c.kind = PlanarCurve::Kind::Point;
        c.a = p->pos;
    } else if (const sketch::Line* l = sk.FindLine(source.curve)) {
        c.a = sk.PositionOf(l->start);
        c.b = sk.PositionOf(l->end);
    } else if (const sketch::Circle* o = sk.FindCircle(source.curve)) {
        c.kind = PlanarCurve::Kind::Circle;
        c.centre = sk.PositionOf(o->center);
        c.radius = o->radius;
    } else if (const sketch::Arc* a = sk.FindArc(source.curve)) {
        c.kind = PlanarCurve::Kind::Arc;
        c.centre = sk.PositionOf(a->center);
        c.a = sk.PositionOf(a->start);
        c.b = sk.PositionOf(a->end);
        c.radius = (c.a - c.centre).Length();
    } else if (const sketch::Ellipse* e = sk.FindEllipse(source.curve)) {
        c.kind = PlanarCurve::Kind::Ellipse;
        c.centre = sk.PositionOf(e->center);
        c.a = sk.PositionOf(e->major);
        c.b = sk.PositionOf(e->minor);
    } else {
        return Error{"Кривая другого эскиза удалена"};
    }
    return ProjectPlanar(from, c, plane);
}

std::vector<PlanarCurve> FlatPatternCurves(const SheetInfo& sheet) {
    const sheet::FlatPattern flat = sheet::Flatten(sheet.part, sheet.layout);
    std::vector<PlanarCurve> out;
    const auto add = [&](const sheet::Segment& s) {
        PlanarCurve c;
        if (!s.arc) {
            c.a = s.a;
            c.b = s.b;
        } else if (s.full) {
            c.kind = PlanarCurve::Kind::Circle;
            c.centre = s.center;
            c.radius = s.Radius();
        } else {
            c.kind = PlanarCurve::Kind::Arc;
            c.centre = s.center;
            c.radius = s.Radius();
            c.a = s.ccw ? s.a : s.b;
            c.b = s.ccw ? s.b : s.a;
        }
        out.push_back(c);
    };
    for (const sheet::Segment& s : flat.outline.outer) add(s);
    for (const sheet::Loop& hole : flat.outline.holes)
        for (const sheet::Segment& s : hole) add(s);
    for (const sheet::FlatPattern::Bend& b : flat.bends) {
        PlanarCurve c;
        c.a = b.a;
        c.b = b.b;
        out.push_back(c);
    }
    return out;
}

std::size_t FlatPatternBends(const SheetInfo& sheet) { return sheet::Flatten(sheet.part, sheet.layout).bends.size(); }

} // namespace bz::kernel
