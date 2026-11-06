#include "app/sketch/SketchPick.h"

#include "app/sketch/SketchText.h"
#include "sketch/SplineGeometry.h"

#include "app/sketch/DimensionLayout.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

namespace bz::app {
namespace {

float gPickScale = 1.0f;   // SetPickScale: a finger with no cursor picks from further
constexpr float kPointRadiusBase = 8.0f;   // px
constexpr float kMidRadiusBase = 12.0f;    // px: the middle of a line pulls from a bit further
constexpr double kSquareDegrees = 4.0; // a line this close to 90° to another becomes exactly 90°
constexpr float kCurveRadiusBase = 6.0f;   // px
float PointRadius() { return kPointRadiusBase * gPickScale; }
float MidRadius() { return kMidRadiusBase * gPickScale; }
float CurveRadius() { return kCurveRadiusBase * gPickScale; }
constexpr float kAlignRadius = 6.0f;   // px: lining up with a point's x or y

}  // namespace

void SetPickScale(float scale) { gPickScale = scale > 0 ? scale : 1.0f; }

namespace {

float Distance(ImVec2 a, ImVec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

float DistanceToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len2 = dx * dx + dy * dy;
    float t = len2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    return Distance(p, {a.x + dx * t, a.y + dy * t});
}

bool AngleInArc(double angle, double start, double end) {
    double sweep = end - start;
    while (sweep <= 0) sweep += 2 * kPi;
    double rel = angle - start;
    while (rel < 0) rel += 2 * kPi;
    while (rel >= 2 * kPi) rel -= 2 * kPi;
    return rel <= sweep;
}

/// Screen distance from the mouse to a curve.
float CurveDistance(const sketch::Sketch& sk, const SketchView& view, ObjectId id, ImVec2 mouse) {
    if (const sketch::Line* l = sk.FindLine(id))
        return DistanceToSegment(mouse, view.ToScreen(sk.PositionOf(l->start)), view.ToScreen(sk.PositionOf(l->end)));
    const std::optional<Vec2> local = view.FromScreen(mouse);
    if (!local) return FLT_MAX;
    const double mmpp = std::max(view.MmPerPixel(), 1e-9);
    if (const sketch::Circle* c = sk.FindCircle(id)) {
        const double d = std::abs((*local - sk.PositionOf(c->center)).Length() - c->radius);
        return static_cast<float>(d / mmpp);
    }
    if (const sketch::Arc* a = sk.FindArc(id)) {
        const Vec2 center = sk.PositionOf(a->center);
        const Vec2 s = sk.PositionOf(a->start) - center, e = sk.PositionOf(a->end) - center;
        const Vec2 m = *local - center;
        if (!AngleInArc(std::atan2(m.y, m.x), std::atan2(s.y, s.x), std::atan2(e.y, e.x))) return FLT_MAX;
        return static_cast<float>(std::abs(m.Length() - s.Length()) / mmpp);
    }
    if (const sketch::Ellipse* e = sk.FindEllipse(id)) {
        const std::vector<Vec2> pts = sketch::SampleEllipse(sk, *e);
        float best = FLT_MAX;
        for (std::size_t i = 0; i + 1 < pts.size(); ++i)
            best = std::min(best, DistanceToSegment(mouse, view.ToScreen(pts[i]), view.ToScreen(pts[i + 1])));
        return best;
    }
    if (const sketch::Text* t = sk.FindText(id)) {
        // Anywhere in its box picks the text (the box lines themselves stay lines).
        if (InsideTextBox(sk, *t, *local)) return 0.5f * CurveRadius();
        return FLT_MAX;
    }
    if (const sketch::Spline* sp = sk.FindSpline(id)) {
        const std::vector<Vec2> pts = sketch::SampleSpline(sketch::SplineShape(sk, *sp));
        float best = FLT_MAX;
        for (std::size_t i = 0; i + 1 < pts.size(); ++i)
            best = std::min(best, DistanceToSegment(mouse, view.ToScreen(pts[i]), view.ToScreen(pts[i + 1])));
        return best;
    }
    return FLT_MAX;
}

} // namespace

Vec2 ClosestOnCurve(const sketch::Sketch& sk, ObjectId curve, Vec2 p) {
    if (const sketch::Line* l = sk.FindLine(curve)) {
        const Vec2 a = sk.PositionOf(l->start), b = sk.PositionOf(l->end);
        const Vec2 d = b - a;
        const double len2 = d.Dot(d);
        const double t = len2 > 0 ? std::clamp((p - a).Dot(d) / len2, 0.0, 1.0) : 0.0;
        return a + d * t;
    }
    ObjectId centerId;
    double r = 0;
    if (const sketch::Circle* c = sk.FindCircle(curve)) {
        centerId = c->center;
        r = c->radius;
    } else if (const sketch::Arc* a = sk.FindArc(curve)) {
        centerId = a->center;
        r = sk.RadiusOf(curve);
    }
    const Vec2 center = sk.PositionOf(centerId);
    const Vec2 dir = (p - center).Length() > 1e-12 ? (p - center).Normalized() : Vec2{1, 0};
    return center + dir * r;
}

SketchHit HitTest(const sketch::Sketch& sk, const SketchView& view, ImVec2 mouse, const UnitSystem& units, ObjectId ignore) {
    SketchHit best;
    float bestDistance = PointRadius();
    for (const auto& p : sk.points) {
        if (p.id == ignore) continue;
        const float d = Distance(mouse, view.ToScreen(p.pos));
        if (d < bestDistance) {
            bestDistance = d;
            best = {p.id, sketch::EntityKind::Point};
        }
    }
    if (best) return best;

    // Dimension labels, before the curves: the text is drawn over them, and a
    // label lying on a line (a diameter across its circle) must still be
    // grabbed to move it.
    for (const auto& c : sk.constraints) {
        if (!sketch::IsDimensional(c.type)) continue;
        const auto layout = LayoutDimension(sk, c, units);
        if (!layout) continue;
        const ImVec2 at = view.ToScreen(layout->label);
        const float halfW = 6.0f + 3.6f * static_cast<float>(layout->text.size()), halfH = 11.0f;
        if (std::abs(mouse.x - at.x) < halfW && std::abs(mouse.y - at.y) < halfH) return {c.id, sketch::EntityKind::Constraint};
    }

    bestDistance = CurveRadius();
    const auto tryCurve = [&](ObjectId id, sketch::EntityKind kind) {
        if (id == ignore) return;
        const float d = CurveDistance(sk, view, id, mouse);
        if (d < bestDistance) {
            bestDistance = d;
            best = {id, kind};
        }
    };
    for (const auto& l : sk.lines) tryCurve(l.id, sketch::EntityKind::Line);
    for (const auto& c : sk.circles) tryCurve(c.id, sketch::EntityKind::Circle);
    for (const auto& a : sk.arcs) tryCurve(a.id, sketch::EntityKind::Arc);
    for (const auto& s : sk.splines) tryCurve(s.id, sketch::EntityKind::Spline);
    for (const auto& t : sk.texts) tryCurve(t.id, sketch::EntityKind::Text);
    for (const auto& e : sk.ellipses) tryCurve(e.id, sketch::EntityKind::Ellipse);
    return best;
}

namespace {

/// A line, circle or arc as plain geometry for intersections.
struct CurveShape {
    bool   line = false;
    Vec2   a, b;            ///< line ends
    Vec2   center;
    double r = 0;
    bool   arc = false;
    double start = 0, end = 0;   ///< arc angles, counter-clockwise from start to end
};

std::optional<CurveShape> ShapeOf(const sketch::Sketch& sk, ObjectId id) {
    CurveShape s;
    if (const sketch::Line* l = sk.FindLine(id)) {
        s.line = true;
        s.a = sk.PositionOf(l->start);
        s.b = sk.PositionOf(l->end);
        if ((s.b - s.a).Length() < 1e-12) return std::nullopt;
        return s;
    }
    if (const sketch::Circle* c = sk.FindCircle(id)) {
        s.center = sk.PositionOf(c->center);
        s.r = c->radius;
        return s.r > 1e-12 ? std::optional(s) : std::nullopt;
    }
    if (const sketch::Arc* a = sk.FindArc(id)) {
        s.center = sk.PositionOf(a->center);
        const Vec2 st = sk.PositionOf(a->start) - s.center, en = sk.PositionOf(a->end) - s.center;
        s.r = st.Length();
        s.arc = true;
        s.start = std::atan2(st.y, st.x);
        s.end = std::atan2(en.y, en.x);
        return s.r > 1e-12 ? std::optional(s) : std::nullopt;
    }
    return std::nullopt;
}

/// Whether `p` (already on the curve's carrier) lies on the drawn piece.
bool OnPiece(const CurveShape& s, Vec2 p) {
    if (s.line) {
        const Vec2 d = s.b - s.a;
        const double t = (p - s.a).Dot(d) / d.Dot(d);
        return t >= -1e-9 && t <= 1 + 1e-9;
    }
    if (!s.arc) return true;
    const Vec2 m = p - s.center;
    return AngleInArc(std::atan2(m.y, m.x), s.start, s.end);
}

/// Where the infinite line through `p` along `dir` crosses the curve's carrier.
std::vector<Vec2> CrossCarrier(const CurveShape& s, Vec2 p, Vec2 dir) {
    std::vector<Vec2> out;
    if (s.line) {
        const Vec2 e = s.b - s.a;
        const double den = dir.Cross(e);
        if (std::abs(den) < 1e-12 * e.Length() * std::max(dir.Length(), 1e-12)) return out;
        const double t = (s.a - p).Cross(e) / den;
        out.push_back(p + dir * t);
        return out;
    }
    const Vec2 f = p - s.center;
    const double A = dir.Dot(dir), B = 2 * f.Dot(dir), C = f.Dot(f) - s.r * s.r;
    const double disc = B * B - 4 * A * C;
    if (A < 1e-24 || disc < 0) return out;
    const double q = std::sqrt(disc);
    out.push_back(p + dir * ((-B - q) / (2 * A)));
    if (q > 1e-12) out.push_back(p + dir * ((-B + q) / (2 * A)));
    return out;
}

/// Points where two drawn curves cross.
std::vector<Vec2> Intersections(const CurveShape& s1, const CurveShape& s2) {
    std::vector<Vec2> raw;
    if (s1.line) {
        raw = CrossCarrier(s2, s1.a, s1.b - s1.a);
    } else if (s2.line) {
        raw = CrossCarrier(s1, s2.a, s2.b - s2.a);
    } else {
        const Vec2 d = s2.center - s1.center;
        const double dist = d.Length();
        if (dist < 1e-12 || dist > s1.r + s2.r || dist < std::abs(s1.r - s2.r)) return {};
        const double along = (s1.r * s1.r - s2.r * s2.r + dist * dist) / (2 * dist);
        const double h = std::sqrt(std::max(0.0, s1.r * s1.r - along * along));
        const Vec2 u = d / dist, n{-u.y, u.x};
        const Vec2 m = s1.center + u * along;
        raw.push_back(m + n * h);
        if (h > 1e-12) raw.push_back(m - n * h);
    }
    std::vector<Vec2> out;
    for (const Vec2& p : raw)
        if (OnPiece(s1, p) && OnPiece(s2, p)) out.push_back(p);
    return out;
}

Vec2 ArcMiddle(const CurveShape& s) {
    double sweep = s.end - s.start;
    while (sweep <= 0) sweep += 2 * kPi;
    const double t = s.start + sweep / 2;
    return s.center + Vec2{std::cos(t), std::sin(t)} * s.r;
}

/// The middles of lines and arcs, with their curve.
std::vector<std::pair<Vec2, ObjectId>> Middles(const sketch::Sketch& sk) {
    std::vector<std::pair<Vec2, ObjectId>> out;
    for (const auto& l : sk.lines) out.push_back({(sk.PositionOf(l.start) + sk.PositionOf(l.end)) * 0.5, l.id});
    for (const auto& a : sk.arcs)
        if (const auto s = ShapeOf(sk, a.id)) out.push_back({ArcMiddle(*s), a.id});
    return out;
}

/// Turns `pos` about `from` onto `dir` (either way), keeping its distance.
Vec2 OntoDirection(Vec2 from, Vec2 pos, Vec2 dir) {
    const Vec2 d = pos - from;
    return from + dir * (d.Dot(dir) >= 0 ? d.Length() : -d.Length());
}

/// Within `degrees` of being along `dir` (either way).
bool NearlyAlong(Vec2 d, Vec2 dir, double degrees) {
    const double len = d.Length();
    return len > 1e-9 && std::abs(d.Cross(dir)) / len < std::sin(degrees * kPi / 180.0);
}

} // namespace

Snap SnapCursor(const sketch::Sketch& sk, const SketchView& view, ImVec2 mouse, const SnapOptions& options) {
    Snap snap;
    const std::optional<Vec2> local = view.FromScreen(mouse);
    if (!local) return snap;
    snap.pos = *local;
    const double mmpp = std::max(view.MmPerPixel(), 1e-9);
    const double reach = kAlignRadius * mmpp;

    // Points to line up with (dashed guides): the points and the middles.
    std::vector<Vec2> guides;
    if (options.points) {
        for (const auto& p : sk.points)
            if (p.id != options.ignore) guides.push_back(p.pos);
        for (const auto& [m, id] : Middles(sk)) guides.push_back(m);
    }

    if (options.points) {
        // 1. An existing point: reused as it is.
        float best = PointRadius() + 2.0f;
        for (const auto& p : sk.points) {
            if (p.id == options.ignore) continue;
            const float d = Distance(mouse, view.ToScreen(p.pos));
            if (d < best) {
                best = d;
                snap.pos = p.pos;
                snap.kind = SnapKind::Point;
                snap.point = p.id;
            }
        }
        if (snap.kind == SnapKind::Point) return snap;

        // 2. The middle of a line or an arc.
        float bestMid = std::max(best, MidRadius());
        for (const auto& [m, id] : Middles(sk)) {
            const float d = Distance(mouse, view.ToScreen(m));
            if (d < bestMid) {
                bestMid = d;
                snap.pos = m;
                snap.kind = SnapKind::Midpoint;
                snap.curve = id;
            }
        }
        if (snap.kind == SnapKind::Midpoint) return snap;

        // The curves near the cursor.
        std::vector<std::pair<ObjectId, CurveShape>> nearby;
        float bestCurve = CurveRadius();
        const auto tryCurve = [&](ObjectId id) {
            const float d = CurveDistance(sk, view, id, mouse);
            if (d < PointRadius() + 2.0f)
                if (const auto s = ShapeOf(sk, id)) nearby.push_back({id, *s});
            if (d < bestCurve) {
                bestCurve = d;
                snap.kind = SnapKind::Curve;
                snap.curve = id;
            }
        };
        for (const auto& l : sk.lines) tryCurve(l.id);
        for (const auto& c : sk.circles) tryCurve(c.id);
        for (const auto& a : sk.arcs) tryCurve(a.id);

        // 3. Where two curves cross (their shared ends are points, found above).
        float bestCross = PointRadius() + 2.0f;
        for (std::size_t i = 0; i < nearby.size(); ++i)
            for (std::size_t j = i + 1; j < nearby.size(); ++j)
                for (const Vec2& p : Intersections(nearby[i].second, nearby[j].second)) {
                    const float d = Distance(mouse, view.ToScreen(p));
                    if (d >= bestCross) continue;
                    bool atPoint = false;
                    for (const auto& pt : sk.points) atPoint = atPoint || (pt.pos - p).Length() < 1e-7;
                    if (atPoint) continue;
                    bestCross = d;
                    snap.pos = p;
                    snap.kind = SnapKind::Intersection;
                    snap.curve = nearby[i].first;
                    snap.curve2 = nearby[j].first;
                }
        if (snap.kind == SnapKind::Intersection) return snap;

        // 4. On a curve, combined with what else fits there.
        if (snap.kind == SnapKind::Curve) {
            snap.pos = ClosestOnCurve(sk, snap.curve, *local);
            // Ending on a line nearly square to it: exactly square, at the foot.
            if (const sketch::Line* l = sk.FindLine(snap.curve); l && options.from) {
                const Vec2 a = sk.PositionOf(l->start), dir = (sk.PositionOf(l->end) - a).Normalized();
                const Vec2 d = snap.pos - *options.from;
                if (d.Length() > 1e-9 && std::abs(d.Normalized().Dot(dir)) < std::sin(kSquareDegrees * kPi / 180.0)) {
                    snap.pos = a + dir * (*options.from - a).Dot(dir);
                    snap.perpendicularTo = l->id;
                    return snap;
                }
            }
            const std::optional<CurveShape> shape = ShapeOf(sk, snap.curve);
            if (!shape) return snap;
            // Sliding along the curve to where it is level with the previous
            // point, or lined up with another point: the nearest that is close.
            const Vec2 onCurve = snap.pos;
            double bestShift = reach * 1.5;
            const auto consider = [&](Vec2 through, Vec2 dir, auto&& mark) {
                for (const Vec2& p : CrossCarrier(*shape, through, dir)) {
                    const double shift = (p - onCurve).Length();
                    if (shift < bestShift && OnPiece(*shape, p)) {
                        bestShift = shift;
                        snap.pos = p;
                        snap.horizontal = snap.vertical = false;
                        snap.alignedX.reset();
                        snap.alignedY.reset();
                        mark();
                    }
                }
            };
            if (options.from) {
                consider(*options.from, {1, 0}, [&] { snap.horizontal = true; });
                consider(*options.from, {0, 1}, [&] { snap.vertical = true; });
            }
            for (const Vec2& g : guides) {
                if ((g - onCurve).Length() < reach) continue;   // its own end
                consider(g, {0, 1}, [&] { snap.alignedX = g; });
                consider(g, {1, 0}, [&] { snap.alignedY = g; });
            }
            return snap;
        }
    }

    if (options.grid && options.gridStep > 0) {
        snap.pos = {std::round(local->x / options.gridStep) * options.gridStep,
                    std::round(local->y / options.gridStep) * options.gridStep};
        snap.kind = SnapKind::Grid;
    }

    // The direction from the previous point, when one fits: horizontal or
    // vertical, square to the line it starts on, along the tangent of the arc
    // it leaves, parallel or square to a line the cursor rested on.
    std::optional<Vec2> lockedDir;
    if (options.from) {
        const Vec2 d = snap.pos - *options.from;
        const double len = d.Length();
        if (len > 1e-9) {
            // "Орто": always the nearer of the two axes.
            const double tolerance = options.ortho ? 2.0 : std::sin(3.0 * kPi / 180.0);
            if (options.ortho ? std::abs(d.y) <= std::abs(d.x) : std::abs(d.y) / len < tolerance) {
                snap.pos.y = options.from->y;
                snap.horizontal = true;
                lockedDir = Vec2{1, 0};
            } else if (options.ortho || std::abs(d.x) / len < tolerance) {
                snap.pos.x = options.from->x;
                snap.vertical = true;
                lockedDir = Vec2{0, 1};
            } else if (options.polarStep > 0) {
                // Polar tracking: the direction always at the nearest multiple
                // of the step (a finger cannot hold an angle by itself), the
                // length along it as the cursor gives it.
                const double step = options.polarStep * kPi / 180.0;
                const double angle = std::round(std::atan2(d.y, d.x) / step) * step;
                const Vec2   dir{std::cos(angle), std::sin(angle)};
                snap.pos = OntoDirection(*options.from, snap.pos, dir);
                double degrees = std::fmod(angle * 180.0 / kPi + 360.0, 360.0);
                if (std::abs(degrees - 360.0) < 1e-6) degrees = 0.0;
                snap.polarAngle = degrees;
                snap.polarFrom = *options.from;
                // Along an axis: the line is horizontal or vertical, with its constraint.
                if (std::abs(std::fmod(degrees, 180.0)) < 1e-6) {
                    snap.pos.y = options.from->y;
                    snap.horizontal = true;
                } else if (std::abs(std::fmod(degrees, 180.0) - 90.0) < 1e-6) {
                    snap.pos.x = options.from->x;
                    snap.vertical = true;
                }
                lockedDir = dir;
            }
        }
    }

    // Starting where a line ends (or on a line) and going nearly square to
    // it: exactly square, keeping the length.
    if (options.from && !lockedDir) {
        std::vector<ObjectId> lines;
        for (const auto& l : sk.lines)
            if (options.fromPoint && (l.start == options.fromPoint || l.end == options.fromPoint)) lines.push_back(l.id);
        if (options.fromCurve && sk.FindLine(options.fromCurve)) lines.push_back(options.fromCurve);
        const Vec2 d = snap.pos - *options.from;
        for (const ObjectId id : lines) {
            const sketch::Line* l = sk.FindLine(id);
            const Vec2 dir = (sk.PositionOf(l->end) - sk.PositionOf(l->start)).Normalized();
            const Vec2 n{-dir.y, dir.x};
            if (!NearlyAlong(d, n, kSquareDegrees)) continue;
            snap.pos = OntoDirection(*options.from, snap.pos, n);
            snap.perpendicularTo = id;
            lockedDir = n;
            break;
        }
    }

    // Leaving the end of an arc (or a point on a circle or arc) along its tangent.
    if (options.from && !lockedDir) {
        std::vector<ObjectId> rounds;
        for (const auto& a : sk.arcs)
            if (options.fromPoint && (a.start == options.fromPoint || a.end == options.fromPoint)) rounds.push_back(a.id);
        if (options.fromCurve && (sk.FindArc(options.fromCurve) || sk.FindCircle(options.fromCurve)))
            rounds.push_back(options.fromCurve);
        const Vec2 d = snap.pos - *options.from;
        for (const ObjectId id : rounds) {
            const auto s = ShapeOf(sk, id);
            if (!s || s->line) continue;
            const Vec2 radial = (*options.from - s->center).Normalized();
            const Vec2 t{-radial.y, radial.x};
            if (!NearlyAlong(d, t, kSquareDegrees)) continue;
            snap.pos = OntoDirection(*options.from, snap.pos, t);
            snap.tangentTo = id;
            lockedDir = t;
            break;
        }
    }

    // Parallel or square to a line the cursor rested on during the command.
    if (options.from && !lockedDir && options.reference && options.reference != options.fromCurve) {
        if (const sketch::Line* l = sk.FindLine(options.reference)) {
            const bool own = options.fromPoint && (l->start == options.fromPoint || l->end == options.fromPoint);
            const Vec2 dir = (sk.PositionOf(l->end) - sk.PositionOf(l->start)).Normalized();
            const Vec2 d = snap.pos - *options.from;
            if (!own && NearlyAlong(d, dir, 3.0)) {
                snap.pos = OntoDirection(*options.from, snap.pos, dir);
                snap.parallelTo = l->id;
                lockedDir = dir;
            } else if (!own && NearlyAlong(d, Vec2{-dir.y, dir.x}, 3.0)) {
                snap.pos = OntoDirection(*options.from, snap.pos, Vec2{-dir.y, dir.x});
                snap.perpendicularTo = l->id;
                lockedDir = Vec2{-dir.y, dir.x};
            }
        }
    }

    // Lining up with other points and the middles of lines and arcs: the
    // nearest same-x and same-y within a few pixels. With a direction already
    // chosen the point slides along it to the guide, so both hold.
    if (options.points) {
        std::optional<Vec2> gx, gy;
        double bestX = reach, bestY = reach;
        for (const Vec2& g : guides) {
            if (const double dx = std::abs(snap.pos.x - g.x); !snap.vertical && dx < bestX && std::abs(snap.pos.y - g.y) > reach) {
                bestX = dx;
                gx = g;
            }
            if (const double dy = std::abs(snap.pos.y - g.y); !snap.horizontal && dy < bestY && std::abs(snap.pos.x - g.x) > reach) {
                bestY = dy;
                gy = g;
            }
        }
        if (!lockedDir) {
            snap.alignedX = gx;
            snap.alignedY = gy;
            if (gx) snap.pos.x = gx->x;
            if (gy) snap.pos.y = gy->y;
        } else {
            // One guide only: the direction leaves a single free amount.
            const Vec2 u = *lockedDir, from = *options.from;
            std::optional<Vec2> slid;
            double bestShift = reach * 1.5;
            if (gx && std::abs(u.x) > 1e-3) {
                const Vec2 p = from + u * ((gx->x - from.x) / u.x);
                if (const double s = (p - snap.pos).Length(); s < bestShift) {
                    bestShift = s;
                    slid = p;
                    snap.alignedX = gx;
                }
            }
            if (gy && std::abs(u.y) > 1e-3) {
                const Vec2 p = from + u * ((gy->y - from.y) / u.y);
                if (const double s = (p - snap.pos).Length(); s < bestShift) {
                    slid = p;
                    snap.alignedX.reset();
                    snap.alignedY = gy;
                }
            }
            if (slid) snap.pos = *slid;
        }
    }
    return snap;
}

double AdaptiveGridStep(double mmPerPixel, double minPixels) {
    const double target = mmPerPixel * minPixels;
    const double power = std::pow(10.0, std::floor(std::log10(std::max(target, 1e-9))));
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (power * m >= target) return power * m;
    return power * 10.0;
}

} // namespace bz::app
