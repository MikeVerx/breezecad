#include "sketch/Trim.h"

#include "sketch/SplineGeometry.h"

#include <algorithm>
#include <cmath>

namespace bz::sketch {
namespace {

constexpr double kEps = 1e-7;

/// To [0; 2 pi) in constant time (see WrapAngle in EquationSystem.cpp).
double Normalize(double a) {
    a = std::fmod(a, 2 * kPi);
    if (a < 0) a += 2 * kPi;
    if (a >= 2 * kPi) a = 0;   // -tiny + 2 pi rounds to 2 pi
    return a;
}

/// A curve as plain geometry.
struct Shape {
    EntityKind kind = EntityKind::None;
    Vec2       a, b;            ///< line ends
    Vec2       centre;
    double     radius = 0;
    double     start = 0;       ///< arc: angle of its start
    double     sweep = 2 * kPi; ///< arc: counter-clockwise extent
    // Splines and ellipses: dense points along the curve and the running
    // length at each; the parameter is the fraction of the length (0..1).
    std::vector<Vec2>   poly;
    std::vector<double> run;
    bool                closed = false;
};

bool IsPoly(const Shape& s) { return s.kind == EntityKind::Spline || s.kind == EntityKind::Ellipse; }
/// Closed curves have no ends: the parameter goes round.
bool Periodic(const Shape& s) { return s.kind == EntityKind::Circle || (IsPoly(s) && s.closed); }
/// Parameter range: a full turn of a circle, the sweep of an arc, 1 otherwise.
double EndParam(const Shape& s) {
    if (s.kind == EntityKind::Circle) return 2 * kPi;
    if (s.kind == EntityKind::Arc) return s.sweep;
    return 1.0;
}

void SetPoly(Shape& s, std::vector<Vec2> pts) {
    s.poly = std::move(pts);
    s.run.assign(s.poly.size(), 0.0);
    for (std::size_t i = 1; i < s.poly.size(); ++i) s.run[i] = s.run[i - 1] + (s.poly[i] - s.poly[i - 1]).Length();
}

/// Nearest place on a polyline shape: (parameter, distance).
std::pair<double, double> NearestOnPoly(const Shape& s, Vec2 p) {
    double bestT = 0, best = 1e300;
    const double total = std::max(s.run.empty() ? 0.0 : s.run.back(), 1e-18);
    for (std::size_t i = 0; i + 1 < s.poly.size(); ++i) {
        const Vec2 a = s.poly[i], d = s.poly[i + 1] - a;
        const double u = std::clamp((p - a).Dot(d) / std::max(d.Dot(d), 1e-18), 0.0, 1.0);
        const double dist = (p - (a + d * u)).Length();
        if (dist < best) {
            best = dist;
            bestT = (s.run[i] + (s.run[i + 1] - s.run[i]) * u) / total;
        }
    }
    return {bestT, best};
}

Shape ShapeOf(const Sketch& sk, ObjectId id) {
    Shape s;
    s.kind = sk.KindOf(id);
    if (const Line* l = sk.FindLine(id)) {
        s.a = sk.PositionOf(l->start);
        s.b = sk.PositionOf(l->end);
    } else if (const Circle* c = sk.FindCircle(id)) {
        s.centre = sk.PositionOf(c->center);
        s.radius = c->radius;
    } else if (const Arc* a = sk.FindArc(id)) {
        s.centre = sk.PositionOf(a->center);
        const Vec2 p = sk.PositionOf(a->start) - s.centre, q = sk.PositionOf(a->end) - s.centre;
        s.radius = p.Length();
        s.start = std::atan2(p.y, p.x);
        s.sweep = Normalize(std::atan2(q.y, q.x) - s.start);
        if (s.sweep < kEps) s.sweep = 2 * kPi;
    } else if (const Spline* sp = sk.FindSpline(id)) {
        SetPoly(s, SampleSpline(SplineShape(sk, *sp), 48));
        s.closed = sp->Closed();
    } else if (const Ellipse* e = sk.FindEllipse(id)) {
        SetPoly(s, SampleEllipse(sk, *e, 360));
        s.closed = true;
    }
    if (IsPoly(s) && s.poly.size() < 2) s.kind = EntityKind::None;
    return s;
}

/// Parameter of `p` along the shape, and whether it lies within it.
std::optional<double> ParamOf(const Shape& s, Vec2 p) {
    if (IsPoly(s)) {
        const auto [t, dist] = NearestOnPoly(s, p);
        if (dist > 1e-6 * std::max(1.0, s.run.back())) return std::nullopt;
        return s.closed && t >= 1.0 - kEps ? 0.0 : t;
    }
    if (s.kind == EntityKind::Line) {
        const Vec2 d = s.b - s.a;
        const double t = (p - s.a).Dot(d) / std::max(d.Dot(d), 1e-18);
        if (t < -kEps || t > 1 + kEps) return std::nullopt;
        return std::clamp(t, 0.0, 1.0);
    }
    const double angle = std::atan2(p.y - s.centre.y, p.x - s.centre.x);
    if (s.kind == EntityKind::Circle) return Normalize(angle);
    const double rel = Normalize(angle - s.start);
    if (rel > s.sweep + kEps && rel < 2 * kPi - kEps) return std::nullopt;
    return rel > s.sweep + kEps ? 0.0 : rel;
}

Vec2 PointAt(const Shape& s, double t) {
    if (IsPoly(s)) {
        if (s.closed) t -= std::floor(t);
        const double at = std::clamp(t, 0.0, 1.0) * s.run.back();
        const auto it = std::upper_bound(s.run.begin(), s.run.end(), at);
        const std::size_t i = std::min<std::size_t>(it == s.run.begin() ? 0 : static_cast<std::size_t>(it - s.run.begin()) - 1, s.poly.size() - 2);
        const double span = std::max(s.run[i + 1] - s.run[i], 1e-18);
        return s.poly[i] + (s.poly[i + 1] - s.poly[i]) * std::clamp((at - s.run[i]) / span, 0.0, 1.0);
    }
    if (s.kind == EntityKind::Line) return s.a + (s.b - s.a) * t;
    const double angle = s.kind == EntityKind::Circle ? t : s.start + t;
    return s.centre + Vec2{std::cos(angle), std::sin(angle)} * s.radius;
}

/// Crossings of two shapes' carriers (infinite line / full circle).
std::vector<Vec2> Crossings(const Shape& x, const Shape& y);

/// Crossings of a polyline shape with another shape's carrier (or polyline),
/// segment by segment.
std::vector<Vec2> PolyCrossings(const Shape& poly, const Shape& other) {
    std::vector<Vec2> out;
    for (std::size_t i = 0; i + 1 < poly.poly.size(); ++i) {
        Shape seg;
        seg.kind = EntityKind::Line;
        seg.a = poly.poly[i];
        seg.b = poly.poly[i + 1];
        const std::vector<Vec2> found = IsPoly(other) ? PolyCrossings(other, seg) : Crossings(seg, other);
        for (const Vec2 p : found) {
            const Vec2 d = seg.b - seg.a;
            const double u = (p - seg.a).Dot(d) / std::max(d.Dot(d), 1e-18);
            if (u < -1e-9 || u > 1 + 1e-9) continue;   // on this segment only
            if (std::none_of(out.begin(), out.end(), [&](Vec2 q) { return (q - p).Length() < 1e-9; })) out.push_back(p);
        }
    }
    return out;
}

std::vector<Vec2> Crossings(const Shape& x, const Shape& y) {
    if (IsPoly(x)) return PolyCrossings(x, y);
    if (IsPoly(y)) return PolyCrossings(y, x);
    const bool xl = x.kind == EntityKind::Line, yl = y.kind == EntityKind::Line;
    if (xl && yl) {
        const Vec2 d1 = x.b - x.a, d2 = y.b - y.a;
        const double den = d1.Cross(d2);
        if (std::abs(den) < 1e-12 * std::max(d1.Dot(d1) * d2.Dot(d2), 1e-30)) return {};
        return {x.a + d1 * ((y.a - x.a).Cross(d2) / den)};
    }
    if (xl || yl) {
        const Shape& l = xl ? x : y;
        const Shape& c = xl ? y : x;
        const Vec2 d = (l.b - l.a).Normalized();
        const Vec2 f = l.a - c.centre;
        const double b = f.Dot(d), disc = b * b - (f.Dot(f) - c.radius * c.radius);
        if (disc < -kEps) return {};
        const double root = std::sqrt(std::max(disc, 0.0));
        if (root < kEps) return {l.a + d * -b};
        return {l.a + d * (-b - root), l.a + d * (-b + root)};
    }
    const Vec2 between = y.centre - x.centre;
    const double dist = between.Length();
    if (dist < kEps || dist > x.radius + y.radius + kEps || dist < std::abs(x.radius - y.radius) - kEps) return {};
    const double along = (dist * dist + x.radius * x.radius - y.radius * y.radius) / (2 * dist);
    const double h = std::sqrt(std::max(x.radius * x.radius - along * along, 0.0));
    const Vec2 u = between * (1.0 / dist), base = x.centre + u * along, n{-u.y, u.x};
    if (h < kEps) return {base};
    return {base + n * h, base - n * h};
}

/// An existing point at `pos` that is not one of `curve`'s own points.
ObjectId PointAt(const Sketch& sk, Vec2 pos, double tolerance) {
    for (const Point& p : sk.points)
        if ((p.pos - pos).Length() < tolerance) return p.id;
    return {};
}

double Size(const Shape& s) {
    if (IsPoly(s)) return s.run.back();
    return s.kind == EntityKind::Line ? (s.b - s.a).Length() : s.radius;
}

Constraint Make(ConstraintType type, ObjectId a, ObjectId b = {}) {
    Constraint c;
    c.type = type;
    c.refs = {a, b};
    return c;
}

} // namespace

std::vector<Cut> CutsOf(const Sketch& sk, ObjectId curve) {
    const Shape s = ShapeOf(sk, curve);
    if (s.kind == EntityKind::None) return {};
    const double tolerance = std::max(Size(s), 1.0) * 1e-6;
    std::vector<Cut> cuts;
    const auto consider = [&](ObjectId other) {
        if (other == curve) return;
        const Shape o = ShapeOf(sk, other);
        for (const Vec2 p : Crossings(s, o)) {
            const auto t = ParamOf(s, p);
            if (!t || !ParamOf(o, p)) continue;
            // A curve's own ends are not cuts (a closed circle has none).
            if (!Periodic(s) && (*t < kEps || *t > EndParam(s) - kEps)) continue;
            cuts.push_back({*t, p, other, PointAt(sk, p, tolerance)});
        }
    };
    for (const Line& l : sk.lines) consider(l.id);
    for (const Circle& c : sk.circles) consider(c.id);
    for (const Arc& a : sk.arcs) consider(a.id);
    for (const Spline& sp : sk.splines) consider(sp.id);
    for (const Ellipse& e : sk.ellipses) consider(e.id);
    std::sort(cuts.begin(), cuts.end(), [](const Cut& p, const Cut& q) { return p.at < q.at; });
    // Several curves crossing in one place cut once.
    cuts.erase(std::unique(cuts.begin(), cuts.end(), [](const Cut& p, const Cut& q) { return q.at - p.at < 1e-9; }),
               cuts.end());
    return cuts;
}

std::optional<TrimSpan> FindTrimSpan(const Sketch& sk, ObjectId curve, Vec2 pick) {
    const Shape s = ShapeOf(sk, curve);
    if (s.kind == EntityKind::None) return std::nullopt;
    const std::vector<Cut> cuts = CutsOf(sk, curve);
    TrimSpan span{curve, std::nullopt, std::nullopt};
    double t = 0;
    if (IsPoly(s)) {
        t = NearestOnPoly(s, pick).first;
    } else if (s.kind == EntityKind::Line) {
        const Vec2 d = s.b - s.a;
        t = std::clamp((pick - s.a).Dot(d) / std::max(d.Dot(d), 1e-18), 0.0, 1.0);
    } else {
        const double angle = Normalize(std::atan2(pick.y - s.centre.y, pick.x - s.centre.x));
        t = s.kind == EntityKind::Circle ? angle : std::min(Normalize(angle - s.start), s.sweep);
    }
    if (Periodic(s)) {
        if (cuts.size() < 2) return span;   // the whole closed curve
        // The cuts on either side of the pick, going round.
        auto after = std::find_if(cuts.begin(), cuts.end(), [&](const Cut& c) { return c.at > t; });
        span.to = after == cuts.end() ? cuts.front() : *after;
        span.from = after == cuts.begin() ? cuts.back() : *(after - 1);
        return span;
    }
    for (const Cut& c : cuts) {
        if (c.at <= t) span.from = c;
        else if (!span.to) span.to = c;
    }
    return span;
}

std::vector<Vec2> SpanPolyline(const Sketch& sk, const TrimSpan& span, int segments) {
    const Shape s = ShapeOf(sk, span.curve);
    if (s.kind == EntityKind::None) return {};
    double t0 = span.from ? span.from->at : 0.0;
    double t1 = span.to ? span.to->at : EndParam(s);
    if (Periodic(s) && span.from && t1 <= t0) t1 += EndParam(s);
    if (s.kind == EntityKind::Line) return {PointAt(s, t0), PointAt(s, t1)};
    std::vector<Vec2> out;
    for (int i = 0; i <= segments; ++i) out.push_back(PointAt(s, t0 + (t1 - t0) * i / segments));
    return out;
}

bool Trim(Sketch& sk, IdGenerator& ids, const TrimSpan& span) {
    const EntityKind kind = sk.KindOf(span.curve);
    if (kind == EntityKind::None) return false;
    if (!span.from && !span.to) {   // nothing cuts it there: the whole curve goes
        sk.Remove(span.curve);
        return true;
    }

    // A point at each cut: the one already there, or a new one held on the cutter.
    const auto pointAt = [&](const Cut& cut) {
        if (cut.point && sk.FindPoint(cut.point)) return cut.point;
        const ObjectId p = sk.AddPoint(ids, cut.pos);
        sk.AddConstraint(ids, Make(ConstraintType::PointOnCurve, p, cut.by));
        return p;
    };
    // Shape constraints carry over to the pieces; sizes and positions do not.
    std::vector<Constraint> kept;
    for (const Constraint& c : sk.constraints)
        if (c.References(span.curve) &&
            (c.type == ConstraintType::Horizontal || c.type == ConstraintType::Vertical ||
             c.type == ConstraintType::Parallel || c.type == ConstraintType::Perpendicular ||
             c.type == ConstraintType::Concentric || c.type == ConstraintType::Radius || c.type == ConstraintType::Diameter))
            kept.push_back(c);
    const auto carry = [&](ObjectId piece, bool first) {
        for (Constraint c : kept) {
            if (IsDimensional(c.type) && !first) continue;   // one parameter, one owner
            for (ObjectId& r : c.refs)
                if (r == span.curve) r = piece;
            c.id = {};
            sk.AddConstraint(ids, c);
        }
    };

    std::vector<ObjectId> pieces;
    // A piece whose ends are one point (a cut found on the curve's own end)
    // would be a curve of zero length: there is no such piece.
    const auto apart = [&](ObjectId a, ObjectId b) {
        return a != b && (sk.PositionOf(a) - sk.PositionOf(b)).Length() > kEps * std::max(1.0, Size(ShapeOf(sk, span.curve)));
    };
    if (kind == EntityKind::Line) {
        const Line line = *sk.FindLine(span.curve);
        if (span.from)
            if (const ObjectId p = pointAt(*span.from); apart(line.start, p)) pieces.push_back(sk.AddLine(ids, line.start, p, line.construction));
        if (span.to)
            if (const ObjectId p = pointAt(*span.to); apart(p, line.end)) pieces.push_back(sk.AddLine(ids, p, line.end, line.construction));
    } else if (kind == EntityKind::Arc) {
        const Arc arc = *sk.FindArc(span.curve);
        if (span.from)
            if (const ObjectId p = pointAt(*span.from); apart(arc.start, p)) pieces.push_back(sk.AddArc(ids, arc.center, arc.start, p, arc.construction));
        if (span.to)
            if (const ObjectId p = pointAt(*span.to); apart(p, arc.end)) pieces.push_back(sk.AddArc(ids, arc.center, p, arc.end, arc.construction));
    } else if (kind == EntityKind::Spline || kind == EntityKind::Ellipse) {
        // What is left becomes splines through points along the old curve
        // (an ellipse cut open is no longer an ellipse).
        const Shape s = ShapeOf(sk, span.curve);
        const bool construction = kind == EntityKind::Spline ? sk.FindSpline(span.curve)->construction
                                                             : sk.FindEllipse(span.curve)->construction;
        const auto piece = [&](double t0, double t1, ObjectId first, ObjectId last) {
            const int n = std::clamp(static_cast<int>(std::ceil((t1 - t0) * 24)), 3, 24);
            std::vector<ObjectId> pts{first};
            for (int i = 1; i < n; ++i) pts.push_back(sk.AddPoint(ids, PointAt(s, t0 + (t1 - t0) * i / n)));
            pts.push_back(last);
            pieces.push_back(sk.AddSpline(ids, pts, true, construction));
        };
        if (s.closed) {
            if (!span.from || !span.to) return false;
            double t0 = span.to->at, t1 = span.from->at;
            if (t1 <= t0) t1 += 1.0;
            piece(t0, t1, pointAt(*span.to), pointAt(*span.from));
        } else {
            const Spline sp = *sk.FindSpline(span.curve);
            if (span.from) piece(0.0, span.from->at, sp.points.front(), pointAt(*span.from));
            if (span.to) piece(span.to->at, 1.0, pointAt(*span.to), sp.points.back());
        }
        sk.Remove(span.curve);
        return true;
    } else {   // circle: what is left runs from the far cut round to the near one
        const Circle circle = *sk.FindCircle(span.curve);
        if (!span.from || !span.to) return false;
        pieces.push_back(sk.AddArc(ids, circle.center, pointAt(*span.to), pointAt(*span.from), circle.construction));
    }
    for (std::size_t i = 0; i < pieces.size(); ++i) carry(pieces[i], i == 0);
    // The two halves of a split arc stay one size.
    if (kind == EntityKind::Arc && pieces.size() == 2) sk.AddConstraint(ids, Make(ConstraintType::Equal, pieces[0], pieces[1]));
    sk.Remove(span.curve);
    return true;
}

std::optional<Extension> FindExtension(const Sketch& sk, ObjectId lineId, Vec2 pick) {
    const Line* line = sk.FindLine(lineId);
    if (!line || line->projected) return std::nullopt;
    const bool atStart = (sk.PositionOf(line->start) - pick).Length() < (sk.PositionOf(line->end) - pick).Length();
    const ObjectId end = atStart ? line->start : line->end;
    const Vec2 from = sk.PositionOf(end), other = sk.PositionOf(atStart ? line->end : line->start);
    const double length = (from - other).Length();
    if (length < kEps) return std::nullopt;
    // A shared end would drag the other curve along.
    int uses = 0;
    for (const Line& l : sk.lines) uses += (l.start == end) + (l.end == end);
    for (const Arc& a : sk.arcs) uses += (a.start == end) + (a.end == end) + (a.center == end);
    for (const Circle& c : sk.circles) uses += c.center == end;
    const Point* origin = sk.Origin();
    if (uses > 1 || (origin && origin->id == end)) return std::nullopt;

    const Vec2 dir = (from - other) * (1.0 / length);
    const Shape self = ShapeOf(sk, lineId);
    std::optional<Extension> best;
    double nearest = 0;
    const auto consider = [&](ObjectId id) {
        if (id == lineId) return;
        const Shape target = ShapeOf(sk, id);
        for (const Vec2 x : Crossings(self, target)) {
            const double ahead = (x - from).Dot(dir);
            if (ahead < kEps * std::max(1.0, length) || !ParamOf(target, x)) continue;
            if (!best || ahead < nearest) {
                best = Extension{lineId, end, x, id};
                nearest = ahead;
            }
        }
    };
    for (const Line& l : sk.lines) consider(l.id);
    for (const Circle& c : sk.circles) consider(c.id);
    for (const Arc& a : sk.arcs) consider(a.id);
    for (const Spline& sp : sk.splines) consider(sp.id);
    for (const Ellipse& e : sk.ellipses) consider(e.id);
    return best;
}

void Extend(Sketch& sk, IdGenerator& ids, const Extension& e) {
    if (Point* p = sk.FindPoint(e.end)) p->pos = e.to;
    sk.AddConstraint(ids, Make(ConstraintType::PointOnCurve, e.end, e.target));
}

std::optional<Vec2> SplitPoint(const Sketch& sk, ObjectId curve, Vec2 pick) {
    const Shape s = ShapeOf(sk, curve);
    if (s.kind != EntityKind::Line && s.kind != EntityKind::Arc) return std::nullopt;
    if (s.kind == EntityKind::Line) {
        const Vec2 d = s.b - s.a;
        const double t = (pick - s.a).Dot(d) / std::max(d.Dot(d), 1e-18);
        if (t < 0.01 || t > 0.99) return std::nullopt;
        return s.a + d * t;
    }
    const double angle = Normalize(std::atan2(pick.y - s.centre.y, pick.x - s.centre.x) - s.start);
    if (angle < 0.01 * s.sweep || angle > 0.99 * s.sweep) return std::nullopt;
    return PointAt(s, angle);
}

std::optional<ObjectId> Split(Sketch& sk, IdGenerator& ids, ObjectId curve, Vec2 pick) {
    const std::optional<Vec2> at = SplitPoint(sk, curve, pick);
    if (!at) return std::nullopt;
    const ObjectId point = sk.AddPoint(ids, *at);
    if (Line* l = sk.FindLine(curve)) {
        const ObjectId start = l->start, end = l->end;
        const bool construction = l->construction, centerline = l->centerline;
        l->end = point;
        // What measured the whole line keeps measuring it, not the first
        // piece: its length becomes the distance between its ends, and its
        // middle or an equality goes to a helper line over both pieces
        // (else the sketch tore: a centred rectangle's split diagonal kept
        // its centre on the first half, and nothing could be solved).
        bool needsWhole = false;
        for (Constraint& c : sk.constraints) {
            if (!c.References(curve)) continue;
            if (c.type == ConstraintType::Length) {
                c.type = ConstraintType::Distance;
                c.refs = {start, end, {}};
            } else if (c.type == ConstraintType::Midpoint || c.type == ConstraintType::Equal) {
                needsWhole = true;
            }
        }
        if (needsWhole) {
            const ObjectId whole = sk.AddLine(ids, start, end, true);
            for (Constraint& c : sk.constraints)
                if (c.type == ConstraintType::Midpoint || c.type == ConstraintType::Equal)
                    for (ObjectId& r : c.refs)
                        if (r == curve) r = whole;
        }
        const ObjectId second = sk.AddLine(ids, point, end, construction);
        sk.FindLine(second)->centerline = centerline;
        sk.AddConstraint(ids, Make(ConstraintType::Parallel, curve, second));
        return point;
    }
    const Arc* a = sk.FindArc(curve);
    const Arc copy = *a;
    // The sweep of the whole arc no longer belongs to either piece.
    std::erase_if(sk.constraints, [&](const Constraint& c) { return c.type == ConstraintType::ArcAngle && c.refs[0] == curve; });
    for (Arc& arc : sk.arcs)
        if (arc.id == curve) arc.end = point;
    sk.AddArc(ids, copy.center, point, copy.end, copy.construction);
    return point;
}

} // namespace bz::sketch
