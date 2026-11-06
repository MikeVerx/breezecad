#include "kernel/occt/SketchSource.h"

#include "kernel/occt/EdgeMatch.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRep_Tool.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <unordered_set>

namespace bz::kernel::occt {
namespace {

constexpr double kTolerance = 1e-4;   // mm: the edge lies on the curve
constexpr double kTwoPi = 2.0 * std::numbers::pi;

/// Points along the edge, ends included.
std::array<Vec3, 5> Samples(const TopoDS_Edge& edge) {
    const BRepAdaptor_Curve c(edge);
    const double a = c.FirstParameter(), b = c.LastParameter();
    std::array<Vec3, 5> out;
    for (int i = 0; i < 5; ++i) {
        const gp_Pnt p = c.Value(a + (b - a) * i / 4.0);
        out[i] = {p.X(), p.Y(), p.Z()};
    }
    return out;
}

/// The angle of `p` around `centre`, 0..2π.
double AngleOf(Vec2 p, Vec2 centre) {
    const double a = std::atan2(p.y - centre.y, p.x - centre.x);
    return a < 0 ? a + kTwoPi : a;
}

/// A sketch curve the projections can follow: what makes faces (not
/// construction lines, not geometry projected from elsewhere).
struct Curve {
    enum class Kind { Line, Circle, Arc } kind;
    Vec2   a, b;            ///< line ends; arc start and end
    Vec2   centre;
    double radius = 0.0;
    double startAngle = 0.0, sweep = kTwoPi;   ///< arcs and circles, counter-clockwise
};

std::optional<Curve> CurveOf(const sketch::Sketch& sk, ObjectId id) {
    if (const sketch::Line* l = sk.FindLine(id)) {
        if (l->construction || l->projected) return std::nullopt;
        Curve c{Curve::Kind::Line, sk.PositionOf(l->start), sk.PositionOf(l->end)};
        if ((c.b - c.a).Length() < kTolerance) return std::nullopt;
        return c;
    }
    if (const sketch::Circle* o = sk.FindCircle(id)) {
        if (o->construction || o->projected || o->radius < kTolerance) return std::nullopt;
        Curve c{Curve::Kind::Circle};
        c.centre = sk.PositionOf(o->center);
        c.radius = o->radius;
        return c;
    }
    if (const sketch::Arc* r = sk.FindArc(id)) {
        if (r->construction || r->projected) return std::nullopt;
        Curve c{Curve::Kind::Arc, sk.PositionOf(r->start), sk.PositionOf(r->end)};
        c.centre = sk.PositionOf(r->center);
        c.radius = (c.a - c.centre).Length();
        if (c.radius < kTolerance) return std::nullopt;
        c.startAngle = AngleOf(c.a, c.centre);
        c.sweep = AngleOf(c.b, c.centre) - c.startAngle;
        if (c.sweep <= 1e-12) c.sweep += kTwoPi;
        return c;
    }
    return std::nullopt;
}

/// Where along `curve` the point `p` (sketch coordinates) is, 0..1 (a line
/// may be a little outside); nullopt when it is off the curve.
std::optional<double> ParameterOn(const Curve& curve, Vec2 p) {
    if (curve.kind == Curve::Kind::Line) {
        const Vec2 d = curve.b - curve.a;
        const double length = d.Length();
        const double t = (p - curve.a).Dot(d) / (length * length);
        const Vec2 foot = curve.a + d * t;
        if ((p - foot).Length() > kTolerance) return std::nullopt;
        const double slack = kTolerance / length;
        if (t < -slack || t > 1 + slack) return std::nullopt;
        return std::clamp(t, 0.0, 1.0);
    }
    if (std::abs((p - curve.centre).Length() - curve.radius) > kTolerance) return std::nullopt;
    double along = AngleOf(p, curve.centre) - curve.startAngle;
    while (along < 0) along += kTwoPi;
    while (along >= kTwoPi) along -= kTwoPi;
    if (curve.kind == Curve::Kind::Circle) return along / kTwoPi;
    const double slack = kTolerance / curve.radius;
    if (along > curve.sweep + slack) {
        if (kTwoPi - along < slack) return 0.0;   // just before the start
        return std::nullopt;
    }
    return std::min(along / curve.sweep, 1.0);
}

/// `edge` against `curve` of a sketch on `frame`: its offset and range when
/// it lies on the curve, parallel to the sketch.
bool Fit(const TopoDS_Edge& edge, const Frame& frame, const Curve& curve, doc::SketchSource& out) {
    if (BRep_Tool::Degenerated(edge)) return false;
    const std::array<Vec3, 5> points = Samples(edge);
    std::array<double, 5> t{};
    const double offset = (points[0] - frame.origin).Dot(frame.normal);
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (std::abs((points[i] - frame.origin).Dot(frame.normal) - offset) > kTolerance) return false;   // not parallel
        const std::optional<double> on = ParameterOn(curve, frame.ToLocal(points[i]));
        if (!on) return false;
        t[i] = *on;
    }
    double from = std::min(t[0], t[4]), to = std::max(t[0], t[4]);
    if (curve.kind == Curve::Kind::Circle) {
        // Around a whole circle the edge may pass through the angle zero:
        // which way it goes is told by its middle.
        const auto ccw = [](double a, double b) { return b >= a ? b - a : b + 1 - a; };
        const bool closed = (points[0] - points[4]).Length() < kTolerance;
        if (closed) {
            from = 0.0;
            to = 1.0;
        } else if (ccw(t[0], t[2]) < ccw(t[0], t[4])) {
            from = t[0];
            to = t[0] + ccw(t[0], t[4]);
        } else {
            from = t[4];
            to = t[4] + ccw(t[4], t[0]);
        }
    }
    if (to - from < 1e-9) return false;   // a point on the curve, not a piece of it
    out.offset = offset;
    out.from = from;
    out.to = to;
    return true;
}

const doc::SketchFeature* SketchOf(const doc::Document& doc, ObjectId id) { return doc.FindAs<doc::SketchFeature>(id); }

/// Sketches before `beforeIndex` that an operation uses (their curves made faces).
std::vector<const doc::SketchFeature*> UsedSketches(const doc::Document& doc, std::size_t beforeIndex) {
    const auto& features = doc.Features();
    std::unordered_set<ObjectId> used;
    for (const auto& f : features)
        if (!dynamic_cast<const doc::SketchFeature*>(f.get()))
            for (ObjectId d : f->Dependencies()) used.insert(d);
    std::vector<const doc::SketchFeature*> out;
    const std::size_t end = std::min(beforeIndex, features.size());
    for (std::size_t i = 0; i < end; ++i)
        if (const auto* sk = dynamic_cast<const doc::SketchFeature*>(features[i].get()); sk && used.count(sk->id)) out.push_back(sk);
    return out;
}

} // namespace

std::optional<doc::SketchSource> FindSketchSource(const TopoDS_Edge& edge, const doc::Document& doc, std::size_t beforeIndex) {
    try {
        for (const doc::SketchFeature* sk : UsedSketches(doc, beforeIndex)) {
            const sketch::Sketch& s = sk->sketch;
            const auto tryCurve = [&](ObjectId id) -> std::optional<doc::SketchSource> {
                const std::optional<Curve> c = CurveOf(s, id);
                doc::SketchSource out{sk->id, id};
                if (c && Fit(edge, sk->placement.frame, *c, out)) return out;
                return std::nullopt;
            };
            for (const sketch::Line& l : s.lines)
                if (auto found = tryCurve(l.id)) return found;
            for (const sketch::Circle& c : s.circles)
                if (auto found = tryCurve(c.id)) return found;
            for (const sketch::Arc& a : s.arcs)
                if (auto found = tryCurve(a.id)) return found;
        }
    } catch (const Standard_Failure&) {
    }
    return std::nullopt;
}

bool LiesOnSource(const TopoDS_Edge& edge, const doc::Document& doc, doc::SketchSource& source) {
    const doc::SketchFeature* sk = SketchOf(doc, source.sketch);
    if (!sk) return false;
    const std::optional<Curve> c = CurveOf(sk->sketch, source.curve);
    try {
        return c && Fit(edge, sk->placement.frame, *c, source);
    } catch (const Standard_Failure&) {
        return false;
    }
}

std::optional<TopoDS_Edge> SourceCurveEdge(const doc::Document& doc, const doc::SketchSource& source) {
    const doc::SketchFeature* sk = SketchOf(doc, source.sketch);
    if (!sk) return std::nullopt;
    const std::optional<Curve> c = CurveOf(sk->sketch, source.curve);
    if (!c) return std::nullopt;
    const Frame& f = sk->placement.frame;
    const auto at = [&](Vec2 p) {
        const Vec3 w = f.ToWorld(p) + f.normal * source.offset;
        return gp_Pnt(w.x, w.y, w.z);
    };
    try {
        if (c->kind == Curve::Kind::Line) {
            const Vec2 d = c->b - c->a;
            const gp_Pnt p0 = at(c->a + d * source.from), p1 = at(c->a + d * source.to);
            if (p0.Distance(p1) < kTolerance) return std::nullopt;
            BRepBuilderAPI_MakeEdge make(p0, p1);
            if (!make.IsDone()) return std::nullopt;
            return make.Edge();
        }
        const gp_Pnt centre = at(c->centre);
        const gp_Ax2 axes(centre, gp_Dir(f.normal.x, f.normal.y, f.normal.z), gp_Dir(f.xDir.x, f.xDir.y, f.xDir.z));
        const gp_Circ circle(axes, c->radius);
        const bool whole = c->kind == Curve::Kind::Circle && source.to - source.from >= 1.0 - 1e-9;
        BRepBuilderAPI_MakeEdge make = whole ? BRepBuilderAPI_MakeEdge(circle)
                                             : BRepBuilderAPI_MakeEdge(circle, c->startAngle + c->sweep * source.from,
                                                                       c->startAngle + c->sweep * source.to);
        if (!make.IsDone()) return std::nullopt;
        return make.Edge();
    } catch (const Standard_Failure&) {
        return std::nullopt;
    }
}

std::optional<TopoDS_Edge> FollowProjection(const TopoDS_Shape& body, std::optional<TopoDS_Edge> resolved,
                                            const doc::Document* doc, std::size_t beforeIndex, doc::EdgeReference& edge,
                                            doc::SketchSource& source, bool* onBody) {
    if (onBody) *onBody = true;
    if (!doc) return resolved;
    if (!source.IsSet()) {
        // An older projection: it learns where its edge came from.
        if (resolved)
            if (const auto found = FindSketchSource(*resolved, *doc, beforeIndex)) source = *found;
        return resolved;
    }
    const doc::SketchFeature* sk = SketchOf(*doc, source.sketch);
    const int index = sk ? doc->IndexOf(sk->id) : -1;
    if (!sk || index < 0 || static_cast<std::size_t>(index) >= beforeIndex || !CurveOf(sk->sketch, source.curve)) {
        source = {};   // the profile curve was deleted: only the edge is left to follow
        return resolved;
    }
    if (resolved) {
        doc::SketchSource now = source;
        if (LiesOnSource(*resolved, *doc, now)) {
            source = now;
            return resolved;
        }
    }
    // The edge found by its look is not on the profile any more (or none
    // was found): an edge of the body on the profile curve, the one nearest
    // the old offset and covering the most of the old range.
    if (!body.IsNull()) {
        try {
            TopTools_IndexedMapOfShape edges;
            TopExp::MapShapes(body, TopAbs_EDGE, edges);
            std::optional<TopoDS_Edge> best;
            doc::SketchSource bestSource;
            double bestScore = 0.0;
            for (int i = 1; i <= edges.Extent(); ++i) {
                const TopoDS_Edge e = TopoDS::Edge(edges(i));
                doc::SketchSource s = source;
                if (!LiesOnSource(e, *doc, s)) continue;
                const double overlap = std::min(s.to, source.to) - std::max(s.from, source.from);
                if (overlap <= 0) continue;
                const double score = overlap / (1.0 + std::abs(s.offset - source.offset));
                if (score > bestScore) {
                    bestScore = score;
                    best = e;
                    bestSource = s;
                }
            }
            if (best) {
                source = bestSource;
                edge = DescribeEdge(*best, body);
                return best;
            }
        } catch (const Standard_Failure&) {
        }
    }
    // No edge on it at all (a chamfer took the whole edge): the profile
    // curve itself, as the edge last covered it.
    if (auto own = SourceCurveEdge(*doc, source)) {
        if (onBody) *onBody = false;
        return own;
    }
    return resolved;
}

} // namespace bz::kernel::occt
