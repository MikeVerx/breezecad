#include "app/sketch/SketchPick.h"

#include "app/sketch/SketchText.h"
#include "sketch/SplineGeometry.h"

#include "app/sketch/DimensionLayout.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace bz::app {
namespace {

constexpr float kPointRadius = 8.0f;   // px
constexpr float kMidRadius = 12.0f;    // px: the middle of a line pulls from a bit further
constexpr double kSquareDegrees = 4.0; // a line this close to 90° to another becomes exactly 90°
constexpr float kCurveRadius = 6.0f;   // px
constexpr float kAlignRadius = 6.0f;   // px: lining up with a point's x or y

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
        if (InsideTextBox(sk, *t, *local)) return 0.5f * kCurveRadius;
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
    float bestDistance = kPointRadius;
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

    bestDistance = kCurveRadius;
    const auto tryCurve = [&](ObjectId id, sketch::EntityKind kind) {
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

Snap SnapCursor(const sketch::Sketch& sk, const SketchView& view, ImVec2 mouse, const SnapOptions& options) {
    Snap snap;
    const std::optional<Vec2> local = view.FromScreen(mouse);
    if (!local) return snap;
    snap.pos = *local;

    if (options.points) {
        float best = kPointRadius + 2.0f;
        for (const auto& p : sk.points) {
            if (p.id == options.ignore) continue;
            const float d = Distance(mouse, view.ToScreen(p.pos));
            if (d < best) {
                best = d;
                snap = {p.pos, SnapKind::Point, p.id, {}, false, false, {}, {}, {}};
            }
        }
        if (snap.kind == SnapKind::Point) return snap;

        best = std::max(best, kMidRadius);
        for (const auto& l : sk.lines) {
            const Vec2 mid = (sk.PositionOf(l.start) + sk.PositionOf(l.end)) * 0.5;
            const float d = Distance(mouse, view.ToScreen(mid));
            if (d < best) {
                best = d;
                snap = {mid, SnapKind::Midpoint, {}, l.id, false, false, {}, {}, {}};
            }
        }
        if (snap.kind == SnapKind::Midpoint) return snap;

        float bestCurve = kCurveRadius;
        const auto tryCurve = [&](ObjectId id) {
            const float d = CurveDistance(sk, view, id, mouse);
            if (d < bestCurve) {
                bestCurve = d;
                snap.kind = SnapKind::Curve;
                snap.curve = id;
            }
        };
        for (const auto& l : sk.lines) tryCurve(l.id);
        for (const auto& c : sk.circles) tryCurve(c.id);
        for (const auto& a : sk.arcs) tryCurve(a.id);
        if (snap.kind == SnapKind::Curve) {
            snap.pos = ClosestOnCurve(sk, snap.curve, *local);
            // Ending on a line nearly square to it: exactly square, at the foot.
            if (const sketch::Line* l = sk.FindLine(snap.curve); l && options.from) {
                const Vec2 a = sk.PositionOf(l->start), dir = (sk.PositionOf(l->end) - a).Normalized();
                const Vec2 d = snap.pos - *options.from;
                if (d.Length() > 1e-9 && std::abs(d.Normalized().Dot(dir)) < std::sin(kSquareDegrees * kPi / 180.0)) {
                    snap.pos = a + dir * (*options.from - a).Dot(dir);
                    snap.perpendicularTo = l->id;
                }
            }
            return snap;
        }
    }

    if (options.grid && options.gridStep > 0) {
        snap.pos = {std::round(local->x / options.gridStep) * options.gridStep,
                    std::round(local->y / options.gridStep) * options.gridStep};
        snap.kind = SnapKind::Grid;
    }

    // Horizontal / vertical inference from the previous point (within ~3 degrees).
    if (options.from) {
        const Vec2 d = snap.pos - *options.from;
        const double len = d.Length();
        if (len > 1e-9) {
            // "Орто": always the nearer of the two axes.
            const double tolerance = options.ortho ? 2.0 : std::sin(3.0 * kPi / 180.0);
            // (A snap to a point or curve returned above: it keeps its place.)
            if (options.ortho ? std::abs(d.y) <= std::abs(d.x) : std::abs(d.y) / len < tolerance) {
                snap.pos.y = options.from->y;
                snap.horizontal = true;
            } else if (options.ortho || std::abs(d.x) / len < tolerance) {
                snap.pos.x = options.from->x;
                snap.vertical = true;
            }
        }
    }

    // Starting where a line ends (or on a line) and going nearly square to
    // it: exactly square, keeping the length.
    if (options.from && !snap.horizontal && !snap.vertical) {
        std::vector<ObjectId> lines;
        for (const auto& l : sk.lines)
            if (options.fromPoint && (l.start == options.fromPoint || l.end == options.fromPoint)) lines.push_back(l.id);
        if (options.fromCurve && sk.FindLine(options.fromCurve)) lines.push_back(options.fromCurve);
        const Vec2 d = snap.pos - *options.from;
        const double len = d.Length();
        for (const ObjectId id : lines) {
            if (len < 1e-9) break;
            const sketch::Line* l = sk.FindLine(id);
            const Vec2 dir = (sk.PositionOf(l->end) - sk.PositionOf(l->start)).Normalized();
            if (std::abs(d.Dot(dir)) / len >= std::sin(kSquareDegrees * kPi / 180.0)) continue;
            const Vec2 n{-dir.y, dir.x};
            snap.pos = *options.from + n * (d.Dot(n) >= 0 ? len : -len);
            snap.perpendicularTo = id;
            break;
        }
    }

    // Lining up with other points and the middles of lines: the nearest
    // same-x and same-y within a few pixels.
    if (options.points) {
        const double reach = kAlignRadius * std::max(view.MmPerPixel(), 1e-9);
        double bestX = reach, bestY = reach;
        for (const auto& p : sk.points) {
            if (p.id == options.ignore) continue;
            if (const double dx = std::abs(snap.pos.x - p.pos.x); !snap.vertical && dx < bestX && std::abs(snap.pos.y - p.pos.y) > reach) {
                bestX = dx;
                snap.alignedX = p.pos;
            }
            if (const double dy = std::abs(snap.pos.y - p.pos.y); !snap.horizontal && dy < bestY && std::abs(snap.pos.x - p.pos.x) > reach) {
                bestY = dy;
                snap.alignedY = p.pos;
            }
        }
        for (const auto& l : sk.lines) {
            const Vec2 mid = (sk.PositionOf(l.start) + sk.PositionOf(l.end)) * 0.5;
            if (const double dx = std::abs(snap.pos.x - mid.x); !snap.vertical && dx < bestX && std::abs(snap.pos.y - mid.y) > reach) {
                bestX = dx;
                snap.alignedX = mid;
            }
            if (const double dy = std::abs(snap.pos.y - mid.y); !snap.horizontal && dy < bestY && std::abs(snap.pos.x - mid.x) > reach) {
                bestY = dy;
                snap.alignedY = mid;
            }
        }
        if (!snap.perpendicularTo) {   // a square line keeps its exact direction
            if (snap.alignedX) snap.pos.x = snap.alignedX->x;
            if (snap.alignedY) snap.pos.y = snap.alignedY->y;
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
