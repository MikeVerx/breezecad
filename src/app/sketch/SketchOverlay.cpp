#include "app/sketch/SketchOverlay.h"

#include "app/sketch/SketchText.h"
#include "sketch/SplineGeometry.h"

#include <algorithm>
#include <cmath>
#include <span>

namespace bz::app {
namespace {

using sketch::ConstraintType;

constexpr int kCircleSegments = 72;

bool Contains(const std::unordered_set<ObjectId>* set, ObjectId id) { return set && set->count(id); }

bool Contains(const std::vector<ObjectId>& v, ObjectId id) { return std::find(v.begin(), v.end(), id) != v.end(); }

/// Polyline in screen space: solid, dashed (6 on, 4 off) or dash-dot
/// (14 on, 3 off, 2 on, 3 off — the drafting centre line).
void Polyline(ImDrawList* dl, const std::vector<ImVec2>& pts, ImU32 color, float width, LinePattern pattern) {
    if (pts.size() < 2) return;
    if (pattern == LinePattern::Solid) {
        dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), color, ImDrawFlags_None, width);
        return;
    }
    static constexpr float dashes[] = {6.0f, 4.0f};
    static constexpr float dashDot[] = {14.0f, 3.0f, 2.0f, 3.0f};
    const std::span<const float> marks = pattern == LinePattern::Dashed ? std::span<const float>(dashes) : std::span<const float>(dashDot);
    float period = 0.0f;
    for (float m : marks) period += m;
    float phase = 0.0f;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const ImVec2 a = pts[i], b = pts[i + 1];
        const float len = std::hypot(b.x - a.x, b.y - a.y);
        float t = 0.0f;
        while (t < len) {
            // Which mark of the pattern the phase is in, and how much of it is left.
            float cycle = std::fmod(phase, period);
            std::size_t k = 0;
            while (cycle >= marks[k]) cycle -= marks[k++];
            const bool drawing = k % 2 == 0;
            const float step = std::min(len - t, std::max(marks[k] - cycle, 1e-3f));
            if (drawing) {
                const ImVec2 p0{a.x + (b.x - a.x) * t / len, a.y + (b.y - a.y) * t / len};
                const ImVec2 p1{a.x + (b.x - a.x) * (t + step) / len, a.y + (b.y - a.y) * (t + step) / len};
                dl->AddLine(p0, p1, color, width);
            }
            t += step;
            phase += step;
        }
    }
}

std::vector<ImVec2> ArcPoints(const SketchView& view, Vec2 c, double r, double a0, double a1) {
    const double sweep = a1 - a0;
    const int n = std::max(8, static_cast<int>(std::abs(sweep) / (2 * kPi) * kCircleSegments));
    std::vector<ImVec2> pts;
    pts.reserve(static_cast<std::size_t>(n) + 1);
    for (int i = 0; i <= n; ++i) {
        const double a = a0 + sweep * i / n;
        pts.push_back(view.ToScreen(c + Vec2{std::cos(a), std::sin(a)} * r));
    }
    return pts;
}

void Arrowhead(ImDrawList* dl, ImVec2 tip, ImVec2 dir, float size, ImU32 color) {
    const float len = std::hypot(dir.x, dir.y);
    if (len < 1e-6f) return;
    const ImVec2 d{dir.x / len, dir.y / len}, n{-d.y, d.x};
    dl->AddTriangleFilled(tip, {tip.x - d.x * size + n.x * size * 0.36f, tip.y - d.y * size + n.y * size * 0.36f},
                          {tip.x - d.x * size - n.x * size * 0.36f, tip.y - d.y * size - n.y * size * 0.36f}, color);
}

const char* GlyphIcon(ConstraintType t) {
    switch (t) {
        case ConstraintType::Horizontal:    return "c-horizontal";
        case ConstraintType::Vertical:      return "c-vertical";
        case ConstraintType::Parallel:      return "c-parallel";
        case ConstraintType::Perpendicular: return "c-perpendicular";
        case ConstraintType::Equal:         return "c-equal";
        case ConstraintType::Tangent:       return "c-tangent";
        case ConstraintType::Fix:           return "c-fix";
        case ConstraintType::Midpoint:      return "c-midpoint";
        case ConstraintType::Concentric:    return "c-concentric";
        case ConstraintType::Symmetric:     return "c-symmetric";
        case ConstraintType::Coincident:
        case ConstraintType::PointOnCurve:  return "c-coincident";
        default:                            return nullptr;
    }
}

} // namespace

void DrawSketchSegment(ui::Ui& ui, ImDrawList* dl, const SketchView& view, Vec2 a, Vec2 b, Color color, float width,
                       LinePattern pattern) {
    Polyline(dl, {view.ToScreen(a), view.ToScreen(b)}, ui::ToU32(color), ui.S(width), pattern);
}

void DrawSketchSegment(ui::Ui& ui, ImDrawList* dl, const SketchView& view, Vec2 a, Vec2 b, Color color, float width,
                       bool dashed) {
    DrawSketchSegment(ui, dl, view, a, b, color, width, dashed ? LinePattern::Dashed : LinePattern::Solid);
}

void DrawSketchArc(ui::Ui& ui, ImDrawList* dl, const SketchView& view, Vec2 center, double radius, double a0, double a1,
                   Color color, float width, bool dashed) {
    Polyline(dl, ArcPoints(view, center, radius, a0, a1), ui::ToU32(color), ui.S(width),
             dashed ? LinePattern::Dashed : LinePattern::Solid);
}

void DrawSketchGrid(ui::Ui& ui, ImDrawList* dl, const SketchView& view, double step) {
    // Visible part of the plane: unproject the viewport corners.
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    for (const ImVec2 corner : {view.vf.min, ImVec2{view.vf.max.x, view.vf.min.y}, view.vf.max, ImVec2{view.vf.min.x, view.vf.max.y}}) {
        const auto p = view.FromScreen(corner);
        if (!p) return;   // plane seen edge-on: no grid
        minX = std::min(minX, p->x);
        maxX = std::max(maxX, p->x);
        minY = std::min(minY, p->y);
        maxY = std::max(maxY, p->y);
    }
    const double limit = 1e5;
    minX = std::max(minX, -limit);
    maxX = std::min(maxX, limit);
    minY = std::max(minY, -limit);
    maxY = std::min(maxY, limit);
    if ((maxX - minX) / step > 400 || (maxY - minY) / step > 400) return;

    const ImU32 minor = ui::ToU32(ui.C().sketchGrid), major = ui::ToU32(ui.C().sketchGridMajor);
    const float w = ui.S(1.0f);
    for (double x = std::floor(minX / step) * step; x <= maxX; x += step) {
        const bool isMajor = std::abs(std::fmod(std::round(x / step), 5.0)) < 0.5;
        dl->AddLine(view.ToScreen({x, minY}), view.ToScreen({x, maxY}), isMajor ? major : minor, w);
    }
    for (double y = std::floor(minY / step) * step; y <= maxY; y += step) {
        const bool isMajor = std::abs(std::fmod(std::round(y / step), 5.0)) < 0.5;
        dl->AddLine(view.ToScreen({minX, y}), view.ToScreen({maxX, y}), isMajor ? major : minor, w);
    }
    // Sketch axes through the origin.
    dl->AddLine(view.ToScreen({minX, 0}), view.ToScreen({maxX, 0}), ui::ToU32(ui.C().axisX.WithAlpha(0.35f)), ui.S(1.2f));
    dl->AddLine(view.ToScreen({0, minY}), view.ToScreen({0, maxY}), ui::ToU32(ui.C().axisY.WithAlpha(0.35f)), ui.S(1.2f));
}

void DrawDimension(ui::Ui& ui, ImDrawList* dl, const SketchView& view, const DimensionLayout& layout, Color color,
                   bool highlight) {
    const ImU32 ink = ui::ToU32(color);
    const float w = ui.S(highlight ? 1.6f : 1.1f);
    for (const auto& s : layout.lines) dl->AddLine(view.ToScreen(s.a), view.ToScreen(s.b), ink, w);
    if (!layout.arc.empty()) {
        std::vector<ImVec2> pts;
        pts.reserve(layout.arc.size());
        for (const Vec2 p : layout.arc) pts.push_back(view.ToScreen(p));
        dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), ink, ImDrawFlags_None, w);
    }
    for (const auto& a : layout.arrows) {
        const ImVec2 tip = view.ToScreen(a.tip);
        const ImVec2 tail = view.ToScreen(a.tip - a.direction);
        Arrowhead(dl, tip, {tip.x - tail.x, tip.y - tail.y}, ui.S(8), ink);
    }

    ImFont* font = ui.fonts.mono;
    const float fs = ui.S(12);
    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0, layout.text.c_str());
    const ImVec2 at = view.ToScreen(layout.label);
    // The parameter's name in grey under the value: what to type in a formula.
    const float nfs = ui.S(9.5f);
    const ImVec2 ns = layout.name.empty() ? ImVec2{0, 0} : font->CalcTextSizeA(nfs, FLT_MAX, 0, layout.name.c_str());
    const float halfW = std::max(ts.x, ns.x) / 2;
    const ImVec2 p0{at.x - halfW - ui.S(5), at.y - ts.y / 2 - ui.S(2)};
    const ImVec2 p1{at.x + halfW + ui.S(5), at.y + ts.y / 2 + ui.S(2) + ns.y};
    dl->AddRectFilled(p0, p1, ui::ToU32(ui.C().panel.WithAlpha(0.92f)), ui.S(4));
    if (highlight) dl->AddRect(p0, p1, ink, ui.S(4), 0, ui.S(1));
    dl->AddText(font, fs, {at.x - ts.x / 2, at.y - ts.y / 2}, ink, layout.text.c_str());
    if (!layout.name.empty())
        dl->AddText(font, nfs, {at.x - ns.x / 2, at.y + ts.y / 2}, ui::ToU32(ui.C().textMuted), layout.name.c_str());
}

void DrawSnapMarker(ui::Ui& ui, ImDrawList* dl, ImVec2 at, const Snap& snap) {
    const ui::ThemeColors& c = ui.C();
    switch (snap.kind) {
        case SnapKind::Point:
            dl->AddCircle(at, ui.S(7), ui::ToU32(c.constraintGlyph), 20, ui.S(2));
            break;
        case SnapKind::Midpoint: {
            // A small triangle, the usual CAD mark for a midpoint.
            const float r = ui.S(6);
            dl->AddTriangle({at.x, at.y - r}, {at.x - r * 0.87f, at.y + r * 0.5f}, {at.x + r * 0.87f, at.y + r * 0.5f},
                            ui::ToU32(c.constraintGlyph), ui.S(2));
            break;
        }
        case SnapKind::Curve:
            dl->AddCircleFilled(at, ui.S(3.5f), ui::ToU32(c.constraintGlyph));
            break;
        case SnapKind::Grid:
            dl->AddLine({at.x - ui.S(5), at.y}, {at.x + ui.S(5), at.y}, ui::ToU32(c.sketchHover), ui.S(1.2f));
            dl->AddLine({at.x, at.y - ui.S(5)}, {at.x, at.y + ui.S(5)}, ui::ToU32(c.sketchHover), ui.S(1.2f));
            break;
        case SnapKind::None:
            dl->AddCircleFilled(at, ui.S(2.5f), ui::ToU32(c.sketchHover));
            break;
    }
    if (snap.horizontal || snap.vertical)
        ui.icons.Draw(dl, snap.horizontal ? "c-horizontal" : "c-vertical", {at.x + ui.S(10), at.y + ui.S(8)}, ui.S(16), {1, 1, 1, 1});
}

void DrawSnapGuides(ui::Ui& ui, ImDrawList* dl, const SketchView& view, const Snap& snap) {
    const Color guide = ui.C().constraintGlyph.WithAlpha(0.7f);
    if (snap.alignedX) DrawSketchSegment(ui, dl, view, *snap.alignedX, snap.pos, guide, 1.0f, true);
    if (snap.alignedY) DrawSketchSegment(ui, dl, view, *snap.alignedY, snap.pos, guide, 1.0f, true);
}

void FillSketchRegion(ImDrawList* dl, const SketchView& view, const kernel::SketchRegion& region, Color fill,
                      Color outline, float outlineWidth) {
    // Even-odd scanline fill in screen space: handles holes without triangulating.
    std::vector<std::vector<ImVec2>> loops;
    float minY = FLT_MAX, maxY = -FLT_MAX;
    for (const auto& loop : region.loops) {
        std::vector<ImVec2> pts;
        pts.reserve(loop.size());
        for (const Vec2 p : loop) {
            pts.push_back(view.ToScreen(p));
            minY = std::min(minY, pts.back().y);
            maxY = std::max(maxY, pts.back().y);
        }
        loops.push_back(std::move(pts));
    }
    const ImU32 color = ui::ToU32(fill);
    std::vector<float> xs;
    for (float y = std::floor(minY); y <= maxY; y += 1.0f) {
        const float sy = y + 0.5f;
        xs.clear();
        for (const auto& pts : loops) {
            for (std::size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
                const ImVec2 a = pts[j], b = pts[i];
                if ((a.y > sy) == (b.y > sy)) continue;
                xs.push_back(a.x + (sy - a.y) * (b.x - a.x) / (b.y - a.y));
            }
        }
        std::sort(xs.begin(), xs.end());
        for (std::size_t k = 0; k + 1 < xs.size(); k += 2) dl->AddRectFilled({xs[k], y}, {xs[k + 1], y + 1.0f}, color);
    }
    if (outlineWidth > 0)
        for (const auto& pts : loops)
            dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), ui::ToU32(outline), ImDrawFlags_Closed, outlineWidth);
}

std::vector<GlyphPlacement> LayoutGlyphs(const sketch::Sketch& sk, const SketchView& view, float scale,
                                         const std::unordered_set<ObjectId>* only) {
    std::vector<GlyphPlacement> out;
    std::vector<std::pair<ObjectId, int>> stack;   // glyphs already placed per entity
    const auto slot = [&](ObjectId entity) {
        for (auto& [id, n] : stack)
            if (id == entity) return n++;
        stack.push_back({entity, 1});
        return 0;
    };
    const float offset = 13.0f * scale, spacing = 17.0f * scale;

    /// Glyph position next to an entity: lines get it beside their middle.
    const auto place = [&](ObjectId entity) -> std::optional<ImVec2> {
        const int n = slot(entity);
        if (const sketch::Line* l = sk.FindLine(entity)) {
            const ImVec2 a = view.ToScreen(sk.PositionOf(l->start)), b = view.ToScreen(sk.PositionOf(l->end));
            const float len = std::max(std::hypot(b.x - a.x, b.y - a.y), 1.0f);
            const ImVec2 d{(b.x - a.x) / len, (b.y - a.y) / len}, nrm{-d.y, d.x};
            const ImVec2 mid{(a.x + b.x) / 2, (a.y + b.y) / 2};
            const float along = static_cast<float>(n) * spacing;
            return ImVec2{mid.x + nrm.x * offset + d.x * along, mid.y + nrm.y * offset + d.y * along};
        }
        if (sk.FindPoint(entity)) {
            const ImVec2 p = view.ToScreen(sk.PositionOf(entity));
            return ImVec2{p.x + offset + static_cast<float>(n) * spacing, p.y - offset};
        }
        ObjectId center;
        double r = 0;
        if (const sketch::Circle* c = sk.FindCircle(entity)) center = c->center, r = c->radius;
        if (const sketch::Arc* a = sk.FindArc(entity)) center = a->center, r = sk.RadiusOf(entity);
        if (!center) return std::nullopt;
        const Vec2 top = sk.PositionOf(center) + Vec2{0.7071, 0.7071} * r;
        const ImVec2 p = view.ToScreen(top);
        return ImVec2{p.x + offset + static_cast<float>(n) * spacing, p.y - offset};
    };

    for (const auto& c : sk.constraints) {
        const char* icon = GlyphIcon(c.type);
        if (!icon || (only && !only->contains(c.id))) continue;
        // Coincidences show as a dot at the point; the others beside each entity.
        const bool both = c.type == ConstraintType::Parallel || c.type == ConstraintType::Perpendicular ||
                          c.type == ConstraintType::Equal || c.type == ConstraintType::Tangent ||
                          c.type == ConstraintType::Concentric || c.type == ConstraintType::Symmetric;
        if (const auto p = place(c.refs[0])) out.push_back({c.id, *p, icon});
        if (both && c.refs[1])
            if (const auto p = place(c.refs[1])) out.push_back({c.id, *p, icon});
    }
    return out;
}

void DrawSketch(ui::Ui& ui, ImDrawList* dl, const doc::SketchFeature& feature, const SketchView& view,
                const UnitSystem& units, const SketchDrawStyle& style) {
    const sketch::Sketch& sk = feature.sketch;
    const ui::ThemeColors& c = ui.C();
    const auto defined = [&](ObjectId id) { return style.analysis && style.analysis->fullyDefined.count(id); };
    const auto failing = [&](ObjectId id) {
        if (!style.analysis) return false;
        for (ObjectId f : style.analysis->failing)
            if (const sketch::Constraint* con = sk.FindConstraint(f); con && con->References(id))
                return true;
        return false;
    };
    const auto curveColor = [&](ObjectId id, bool construction, bool projected) -> Color {
        if (style.passive) return construction ? c.sketchConstruction.WithAlpha(0.45f) : c.sketchFree.WithAlpha(0.6f);
        if (Contains(style.selection, id) || Contains(style.picked, id)) return c.sketchSelected;
        if (style.hover == id) return c.sketchHover;
        if (failing(id)) return c.danger;
        if (projected) return c.sketchProjected;   // held by the model, as in Inventor
        if (construction) return c.sketchConstruction;
        return defined(id) ? c.sketchDefined : c.sketchFree;
    };
    const auto curveWidth = [&](ObjectId id, bool construction) {
        if (style.passive) return 1.3f;
        if (style.hover == id || Contains(style.selection, id) || Contains(style.picked, id)) return 2.8f;
        return construction ? 1.3f : 2.0f;
    };

    for (const auto& l : sk.lines) {
        const bool thin = l.construction || l.centerline;
        DrawSketchSegment(ui, dl, view, sk.PositionOf(l.start), sk.PositionOf(l.end), curveColor(l.id, thin, l.projected),
                          curveWidth(l.id, thin),
                          l.centerline ? LinePattern::DashDot : l.construction ? LinePattern::Dashed : LinePattern::Solid);
    }
    for (const auto& ci : sk.circles)
        DrawSketchArc(ui, dl, view, sk.PositionOf(ci.center), ci.radius, 0, 2 * kPi, curveColor(ci.id, ci.construction, ci.projected),
                      curveWidth(ci.id, ci.construction), ci.construction);
    for (const auto& a : sk.arcs) {
        const Vec2 center = sk.PositionOf(a.center);
        const Vec2 s = sk.PositionOf(a.start) - center, e = sk.PositionOf(a.end) - center;
        double a0 = std::atan2(s.y, s.x), a1 = std::atan2(e.y, e.x);
        if (a1 <= a0) a1 += 2 * kPi;
        DrawSketchArc(ui, dl, view, center, s.Length(), a0, a1, curveColor(a.id, a.construction, a.projected),
                      curveWidth(a.id, a.construction), a.construction);
    }
    for (const auto& e : sk.ellipses) {
        const std::vector<Vec2> pts = sketch::SampleEllipse(sk, e);
        for (std::size_t i = 0; i + 1 < pts.size(); ++i)
            DrawSketchSegment(ui, dl, view, pts[i], pts[i + 1], curveColor(e.id, e.construction, false), curveWidth(e.id, e.construction),
                              e.construction ? LinePattern::Dashed : LinePattern::Solid);
    }
    for (const auto& t : sk.texts) {
        const Color ink = curveColor(t.id, t.construction, false);
        const float width = curveWidth(t.id, t.construction);
        for (const auto& loop : TextLoops(sk, t))
            for (std::size_t i = 0; i + 1 < loop.size(); ++i)
                DrawSketchSegment(ui, dl, view, loop[i], loop[i + 1], ink, width,
                                  t.construction ? LinePattern::Dashed : LinePattern::Solid);
    }
    for (const auto& sp : sk.splines) {
        const std::vector<Vec2> pts = sketch::SampleSpline(sketch::SplineShape(sk, sp));
        const Color ink = curveColor(sp.id, sp.construction, false);
        const float width = curveWidth(sp.id, sp.construction);
        for (std::size_t i = 0; i + 1 < pts.size(); ++i)
            DrawSketchSegment(ui, dl, view, pts[i], pts[i + 1], ink, width, sp.construction ? LinePattern::Dashed : LinePattern::Solid);
        // Control vertices pull the curve: show the polygon they make.
        if (!sp.interpolate && !style.passive)
            for (std::size_t i = 0; i + 1 < sp.points.size(); ++i)
                DrawSketchSegment(ui, dl, view, sk.PositionOf(sp.points[i]), sk.PositionOf(sp.points[i + 1]),
                                  ui.C().textMuted.WithAlpha(0.6f), 1.0f, LinePattern::Dashed);
    }
    if (style.passive) return;

    // Points: curve ends, centres and lone points.
    for (const auto& p : sk.points) {
        const ImVec2 at = view.ToScreen(p.pos);
        if (p.isOrigin) {
            dl->AddCircleFilled(at, ui.S(3.5f), ui::ToU32(c.axisZ));
            dl->AddCircle(at, ui.S(6), ui::ToU32(c.axisZ.WithAlpha(0.6f)), 16, ui.S(1.2f));
            continue;
        }
        const bool hot = style.hover == p.id || Contains(style.selection, p.id) || Contains(style.picked, p.id);
        const Color ring = curveColor(p.id, false, p.projected);
        const float r = ui.S(hot ? 4.5f : 3.0f);
        dl->AddCircleFilled(at, r, ui::ToU32(defined(p.id) && !hot ? ring : c.panel));
        dl->AddCircle(at, r, ui::ToU32(ring), 12, ui.S(1.4f));
    }

    // Constraint glyphs.
    for (const GlyphPlacement& g : LayoutGlyphs(sk, view, ui.scale, style.glyphs)) {
        const bool hot = style.hover == g.constraint || Contains(style.selection, g.constraint);
        const float size = ui.S(14);
        if (hot) dl->AddRectFilled({g.center.x - size * 0.65f, g.center.y - size * 0.65f},
                                   {g.center.x + size * 0.65f, g.center.y + size * 0.65f},
                                   ui::ToU32(c.sketchSelected.WithAlpha(0.25f)), ui.S(3));
        ui.icons.Draw(dl, g.icon, {g.center.x - size / 2, g.center.y - size / 2}, size, {1, 1, 1, 0.9f});
    }

    // Dimensions on top.
    for (const auto& con : sk.constraints) {
        if (!sketch::IsDimensional(con.type)) continue;
        const auto layout = LayoutDimension(sk, con, units);
        if (!layout) continue;
        const bool hot = style.hover == con.id || Contains(style.selection, con.id);
        Color color = con.parameter.HasError() ? c.danger : con.reference ? c.textMuted : c.dimension;
        if (hot) color = Contains(style.selection, con.id) ? c.sketchSelected : c.sketchHover;
        DrawDimension(ui, dl, view, *layout, color, hot);
    }
}

} // namespace bz::app
