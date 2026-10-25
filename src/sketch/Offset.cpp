#include "sketch/Offset.h"

#include "sketch/SplineGeometry.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace bz::sketch {
namespace {

constexpr double kEps = 1e-9;

/// The two ends of a line or arc as stored (start, end); none for others.
std::optional<std::pair<ObjectId, ObjectId>> EndsOf(const Sketch& sk, ObjectId curve) {
    if (const Line* l = sk.FindLine(curve)) return std::make_pair(l->start, l->end);
    if (const Arc* a = sk.FindArc(curve)) return std::make_pair(a->start, a->end);
    return std::nullopt;
}

/// A piece as plain geometry, travelled from `a` to `b`.
struct Seg {
    bool   line = true;
    Vec2   a, b;
    Vec2   centre;
    double radius = 0;
    bool   ccw = true;      ///< arcs: travelled counter-clockwise
    bool   circle = false;  ///< a whole circle (a and b unused)
};

Seg SegOf(const Sketch& sk, const Chain::Piece& p) {
    Seg s;
    if (const Line* l = sk.FindLine(p.curve)) {
        s.a = sk.PositionOf(p.reversed ? l->end : l->start);
        s.b = sk.PositionOf(p.reversed ? l->start : l->end);
    } else if (const Arc* a = sk.FindArc(p.curve)) {
        s.line = false;
        s.centre = sk.PositionOf(a->center);
        s.a = sk.PositionOf(p.reversed ? a->end : a->start);
        s.b = sk.PositionOf(p.reversed ? a->start : a->end);
        s.radius = (s.a - s.centre).Length();
        s.ccw = !p.reversed;
    } else if (const Circle* c = sk.FindCircle(p.curve)) {
        s.line = false;
        s.circle = true;
        s.centre = sk.PositionOf(c->center);
        s.radius = c->radius;
    }
    return s;
}

/// The piece moved to its left by `d`; false when an arc shrinks to nothing.
bool Shift(Seg& s, double d) {
    if (s.line) {
        const Vec2 dir = s.b - s.a;
        const Vec2 left = Vec2{-dir.y, dir.x}.Normalized();
        s.a = s.a + left * d;
        s.b = s.b + left * d;
        return true;
    }
    // Left of a counter-clockwise travel is toward the centre.
    const double r = s.ccw ? s.radius - d : s.radius + d;
    if (r < kEps * std::max(1.0, s.radius)) return false;
    if (!s.circle) {
        s.a = s.centre + (s.a - s.centre) * (r / s.radius);
        s.b = s.centre + (s.b - s.centre) * (r / s.radius);
    }
    s.radius = r;
    return true;
}

/// Crossings of the carriers of two pieces (infinite line, full circle).
std::vector<Vec2> Crossings(const Seg& x, const Seg& y) {
    if (x.line && y.line) {
        const Vec2 d1 = x.b - x.a, d2 = y.b - y.a;
        const double den = d1.Cross(d2);
        if (std::abs(den) < 1e-12 * std::max(d1.Length() * d2.Length(), 1e-30)) return {};
        return {x.a + d1 * ((y.a - x.a).Cross(d2) / den)};
    }
    if (x.line || y.line) {
        const Seg& l = x.line ? x : y;
        const Seg& c = x.line ? y : x;
        const Vec2 d = (l.b - l.a).Normalized(), f = l.a - c.centre;
        const double b = f.Dot(d), disc = b * b - (f.Dot(f) - c.radius * c.radius);
        if (disc < -kEps) return {};
        const double root = std::sqrt(std::max(disc, 0.0));
        return {l.a + d * (-b - root), l.a + d * (-b + root)};
    }
    const Vec2 between = y.centre - x.centre;
    const double dist = between.Length();
    if (dist < kEps || dist > x.radius + y.radius + kEps || dist < std::abs(x.radius - y.radius) - kEps) return {};
    const double along = (dist * dist + x.radius * x.radius - y.radius * y.radius) / (2 * dist);
    const double h = std::sqrt(std::max(x.radius * x.radius - along * along, 0.0));
    const Vec2 u = between * (1.0 / dist), base = x.centre + u * along, n{-u.y, u.x};
    return {base + n * h, base - n * h};
}

/// The pieces of the offset, joints resolved; nullopt when it fails.
std::optional<std::vector<Seg>> Build(const Sketch& sk, const Chain& chain, double d) {
    if (chain.pieces.empty()) return std::nullopt;
    std::vector<Seg> segs;
    for (const Chain::Piece& p : chain.pieces) {
        Seg s = SegOf(sk, p);
        const Vec2 direction = s.b - s.a;
        if (!Shift(s, d)) return std::nullopt;
        if (s.line && (s.b - s.a).Dot(direction) <= 0) return std::nullopt;
        segs.push_back(s);
    }
    if (segs.size() == 1 && segs[0].circle) return segs;
    const std::size_t n = segs.size();
    const std::size_t joints = chain.closed ? n : n - 1;
    for (std::size_t i = 0; i < joints; ++i) {
        Seg& x = segs[i];
        Seg& y = segs[(i + 1) % n];
        if ((x.b - y.a).Length() > 1e-7 * std::max(1.0, std::abs(d))) {   // a corner: meet where the carriers cross
            const Vec2 mid = (x.b + y.a) * 0.5;
            const std::vector<Vec2> found = Crossings(x, y);
            if (found.empty()) return std::nullopt;
            const Vec2 j = *std::min_element(found.begin(), found.end(),
                                             [&](Vec2 p, Vec2 q) { return (p - mid).Length() < (q - mid).Length(); });
            x.b = j;
            y.a = j;
        } else {
            y.a = x.b;
        }
    }
    // A line that turned around has been offset past its length; a piece
    // that shrank to (almost) nothing would be a zero-length curve.
    const double tiny = 1e-6 * std::max(1.0, std::abs(d));
    for (std::size_t i = 0; i < n; ++i) {
        const Seg orig = SegOf(sk, chain.pieces[i]);
        if (segs[i].line && (segs[i].b - segs[i].a).Dot(orig.b - orig.a) <= 0) return std::nullopt;
        if ((segs[i].b - segs[i].a).Length() < tiny) return std::nullopt;
    }
    return segs;
}

/// A lone spline or ellipse: its dense points in travel order; empty for others.
std::vector<Vec2> FreeformPoints(const Sketch& sk, const Chain& chain, bool* closed = nullptr) {
    if (chain.pieces.size() != 1) return {};
    const ObjectId id = chain.pieces[0].curve;
    if (const Spline* sp = sk.FindSpline(id)) {
        if (closed) *closed = sp->Closed();
        return SampleSpline(SplineShape(sk, *sp), 48);
    }
    if (const Ellipse* e = sk.FindEllipse(id)) {
        if (closed) *closed = true;
        return SampleEllipse(sk, *e, 360);
    }
    return {};
}

/// Points moved `d` to the left of the travel direction; empty when the
/// copy would fold over itself (the distance beats the curvature).
std::vector<Vec2> ShiftPolyline(const std::vector<Vec2>& pts, double d, bool closed) {
    const std::size_t n = pts.size();
    if (n < 2) return {};
    std::vector<Vec2> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 prev = i > 0 ? pts[i - 1] : (closed ? pts[n - 2] : pts[0]);
        const Vec2 next = i + 1 < n ? pts[i + 1] : (closed ? pts[1] : pts[n - 1]);
        const Vec2 t = (next - prev).Normalized();
        out[i] = pts[i] + Vec2{-t.y, t.x} * d;
    }
    for (std::size_t i = 0; i + 1 < n; ++i)
        if ((out[i + 1] - out[i]).Dot(pts[i + 1] - pts[i]) <= 0) return {};
    return out;
}

double DistanceTo(const Seg& s, Vec2 p) {
    if (s.line) {
        const Vec2 d = s.b - s.a;
        const double t = std::clamp((p - s.a).Dot(d) / std::max(d.Dot(d), 1e-18), 0.0, 1.0);
        return (s.a + d * t - p).Length();
    }
    return std::abs((p - s.centre).Length() - s.radius);
}

} // namespace

std::optional<Chain> ChainOf(const Sketch& sk, ObjectId curve) {
    if (sk.FindCircle(curve) || sk.FindEllipse(curve)) return Chain{{{curve, false}}, true};
    if (const Spline* sp = sk.FindSpline(curve)) return Chain{{{curve, false}}, sp->Closed()};
    if (!EndsOf(sk, curve)) return std::nullopt;
    // Curves ending at each point. Only those of the same kind as the picked
    // one: an outline runs on through a corner where a helper line also
    // ends (a centred rectangle's diagonal, a fillet's phantom side).
    const auto helper = [&](ObjectId id) {
        if (const Line* l = sk.FindLine(id)) return l->construction;
        if (const Arc* a = sk.FindArc(id)) return a->construction;
        return false;
    };
    const bool kind = helper(curve);
    std::map<ObjectId, std::vector<ObjectId>> at;
    for (const Line& l : sk.lines) {
        if (l.construction != kind) continue;
        at[l.start].push_back(l.id);
        at[l.end].push_back(l.id);
    }
    for (const Arc& a : sk.arcs) {
        if (a.construction != kind) continue;
        at[a.start].push_back(a.id);
        at[a.end].push_back(a.id);
    }
    const auto other = [&](ObjectId point, ObjectId from) -> ObjectId {
        const auto it = at.find(point);
        if (it == at.end() || it->second.size() != 2) return {};
        return it->second[0] == from ? it->second[1] : it->second[0];
    };

    Chain chain;
    chain.pieces.push_back({curve, false});
    const std::size_t limit = sk.lines.size() + sk.arcs.size();
    // Forward from the end.
    while (chain.pieces.size() <= limit) {
        const Chain::Piece last = chain.pieces.back();
        const auto [s, e] = *EndsOf(sk, last.curve);
        const ObjectId exit = last.reversed ? s : e;
        const ObjectId next = other(exit, last.curve);
        if (!next) break;
        if (next == curve) {
            chain.closed = true;
            break;
        }
        chain.pieces.push_back({next, EndsOf(sk, next)->second == exit});
    }
    if (chain.closed) return chain;
    // Backward from the start.
    while (chain.pieces.size() <= limit) {
        const Chain::Piece first = chain.pieces.front();
        const auto [s, e] = *EndsOf(sk, first.curve);
        const ObjectId entry = first.reversed ? e : s;
        const ObjectId prev = other(entry, first.curve);
        if (!prev) break;
        chain.pieces.insert(chain.pieces.begin(), {prev, EndsOf(sk, prev)->first == entry});
    }
    return chain;
}

int SideOf(const Sketch& sk, const Chain& chain, Vec2 p) {
    if (const std::vector<Vec2> pts = FreeformPoints(sk, chain); !pts.empty()) {
        double best = 1e300;
        int side = 1;
        for (std::size_t k = 0; k + 1 < pts.size(); ++k) {
            const Vec2 a = pts[k], d = pts[k + 1] - a;
            const double u = std::clamp((p - a).Dot(d) / std::max(d.Dot(d), 1e-18), 0.0, 1.0);
            const double dist = (p - (a + d * u)).Length();
            if (dist < best) {
                best = dist;
                side = d.Cross(p - a) >= 0 ? 1 : -1;
            }
        }
        return side;
    }
    double best = 1e300;
    int side = 1;
    for (const Chain::Piece& piece : chain.pieces) {
        const Seg s = SegOf(sk, piece);
        const double dist = DistanceTo(s, p);
        if (dist >= best) continue;
        best = dist;
        if (s.line) side = (s.b - s.a).Cross(p - s.a) >= 0 ? 1 : -1;
        else side = ((p - s.centre).Length() < s.radius) == s.ccw ? 1 : -1;
    }
    return side;
}

std::vector<std::vector<Vec2>> OffsetPreview(const Sketch& sk, const Chain& chain, double distance) {
    std::vector<std::vector<Vec2>> out;
    bool closed = false;
    if (const std::vector<Vec2> pts = FreeformPoints(sk, chain, &closed); !pts.empty()) {
        if (std::vector<Vec2> shifted = ShiftPolyline(pts, distance, closed); !shifted.empty()) out.push_back(std::move(shifted));
        return out;
    }
    const auto segs = Build(sk, chain, distance);
    if (!segs) return out;
    for (const Seg& s : *segs) {
        if (s.line) {
            out.push_back({s.a, s.b});
            continue;
        }
        double from = 0, sweep = 2 * kPi;
        if (!s.circle) {
            from = std::atan2(s.a.y - s.centre.y, s.a.x - s.centre.x);
            const double to = std::atan2(s.b.y - s.centre.y, s.b.x - s.centre.x);
            sweep = std::fmod(to - from + 4 * kPi, 2 * kPi);
            if (!s.ccw) sweep -= 2 * kPi;
        }
        std::vector<Vec2> poly;
        for (int i = 0; i <= 48; ++i) {
            const double t = from + sweep * i / 48;
            poly.push_back(s.centre + Vec2{std::cos(t), std::sin(t)} * s.radius);
        }
        out.push_back(std::move(poly));
    }
    return out;
}

std::optional<OffsetCurves> OffsetChain(Sketch& sk, IdGenerator& ids, const Chain& chain, double distance) {
    if (std::abs(distance) < kEps) return std::nullopt;
    // A spline or an ellipse: a spline through points of the shifted curve
    // (the offset of either is not the same kind of curve).
    bool closed = false;
    if (const std::vector<Vec2> pts = FreeformPoints(sk, chain, &closed); !pts.empty()) {
        const std::vector<Vec2> shifted = ShiftPolyline(pts, distance, closed);
        if (shifted.empty()) return std::nullopt;
        const Spline* sp = sk.FindSpline(chain.pieces[0].curve);
        const std::size_t count = std::clamp<std::size_t>(sp ? sp->points.size() * 4 : 16, 8, 48);
        std::vector<ObjectId> pointIds;
        const std::size_t last = closed ? count : count + 1;
        for (std::size_t k = 0; k < last; ++k) {
            const std::size_t at = std::min(k * (shifted.size() - 1) / count, shifted.size() - 1);
            pointIds.push_back(sk.AddPoint(ids, shifted[at]));
        }
        if (closed) pointIds.push_back(pointIds.front());
        OffsetCurves out;
        out.curves.push_back(sk.AddSpline(ids, pointIds, true, sp ? sp->construction : false));
        return out;
    }
    const auto built = Build(sk, chain, distance);
    if (!built) return std::nullopt;
    const std::vector<Seg>& segs = *built;
    OffsetCurves out;
    if (segs.size() == 1 && segs[0].circle) {
        const Circle* c = sk.FindCircle(chain.pieces[0].curve);
        out.curves.push_back(sk.AddCircle(ids, c->center, segs[0].radius));
        return out;
    }
    // One point per joint, shared by the neighbours.
    const std::size_t n = segs.size();
    std::vector<ObjectId> starts(n), ends(n);
    for (std::size_t i = 0; i < n; ++i) {
        starts[i] = i > 0 ? ends[i - 1] : sk.AddPoint(ids, segs[i].a);
        ends[i] = (chain.closed && i + 1 == n) ? starts[0] : sk.AddPoint(ids, segs[i].b);
    }
    for (std::size_t i = 0; i < n; ++i) {
        const Chain::Piece& piece = chain.pieces[i];
        // Stored the way the original is (arcs counter-clockwise).
        const ObjectId s = piece.reversed ? ends[i] : starts[i];
        const ObjectId e = piece.reversed ? starts[i] : ends[i];
        if (const Line* l = sk.FindLine(piece.curve)) {
            const ObjectId origin = l->start;
            const ObjectId line = sk.AddLine(ids, s, e);
            Constraint parallel;
            parallel.type = ConstraintType::Parallel;
            parallel.refs = {piece.curve, line};
            sk.AddConstraint(ids, parallel);
            out.curves.push_back(line);
            out.distances.emplace_back(origin, line);
        } else if (const Arc* a = sk.FindArc(piece.curve)) {
            // The same centre point: concentric by construction.
            out.curves.push_back(sk.AddArc(ids, a->center, s, e));
        }
    }
    return out;
}

} // namespace bz::sketch
