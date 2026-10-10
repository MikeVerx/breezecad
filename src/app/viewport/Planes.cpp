#include "app/viewport/Planes.h"

#include "document/Origin.h"
#include "document/WorkAxisFeature.h"
#include "document/WorkPlaneFeature.h"
#include "document/WorkPointFeature.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace bz::app {
namespace {

/// Half size of the plane squares: big enough to see around the part.
double PlaneHalfSize(const Session& session) {
    const auto box = session.model.BoundingBox();
    if (!box) return 40.0;
    const Vec3 d = box->max - box->min;
    return std::max({40.0, d.x * 0.65, d.y * 0.65, d.z * 0.65});
}

} // namespace

std::vector<ShownPlane> OriginPlanes(const Session& session) {
    const double half = PlaneHalfSize(session);
    std::vector<ShownPlane> planes;
    for (const auto& [id, label] : {std::pair{doc::origin::kPlaneXY, "XY"}, {doc::origin::kPlaneXZ, "XZ"}, {doc::origin::kPlaneYZ, "YZ"}})
        planes.push_back({id, *doc::origin::PlaneFrame(id), {0, 0}, half, label});
    return planes;
}

std::vector<ShownPlane> WorkPlanes(const Session& session, std::size_t before) {
    const double half = PlaneHalfSize(session) * 0.8;
    const auto box = session.model.BoundingBox();
    const Vec3 middle = box ? (box->min + box->max) * 0.5 : Vec3{};
    std::vector<ShownPlane> planes;
    const std::size_t end = std::min(before, session.doc.EndMarker());
    for (std::size_t i = 0; i < end; ++i) {
        const auto* wp = dynamic_cast<const doc::WorkPlaneFeature*>(session.doc.Features()[i].get());
        if (!wp || wp->suppressed || !wp->frame) continue;
        planes.push_back({wp->id, *wp->frame, wp->frame->ToLocal(middle), half, wp->name});
    }
    return planes;
}

std::optional<double> HitPlane(const ViewportFrame& vf, ImVec2 mouse, const ShownPlane& plane) {
    Vec3 origin, dir;
    vf.projection.Unproject(vf.ToLocal(mouse), origin, dir);
    const double denom = dir.Dot(plane.frame.normal);
    if (std::abs(denom) < 1e-9) return std::nullopt;
    const double t = (plane.frame.origin - origin).Dot(plane.frame.normal) / denom;
    const Vec2 local = plane.frame.ToLocal(origin + dir * t);
    if (std::abs(local.x - plane.centre.x) > plane.half || std::abs(local.y - plane.centre.y) > plane.half) return std::nullopt;
    return t;
}

const ShownPlane* PlaneUnderCursor(const ViewportFrame& vf, ImVec2 mouse, const std::vector<ShownPlane>& planes) {
    const ShownPlane* best = nullptr;
    double bestDistance = 1e300;
    for (const ShownPlane& p : planes)
        if (const auto t = HitPlane(vf, mouse, p); t && *t < bestDistance) {
            bestDistance = *t;
            best = &p;
        }
    return best;
}

void DrawPlane(AppContext& app, ImDrawList* dl, const ViewportFrame& vf, const ShownPlane& plane, bool hot, bool faint) {
    const ui::ThemeColors& c = app.ui.C();
    // Origin planes in the colour of their normal's axis, muted so they
    // do not shout over the model: XY bluish, XZ greenish, YZ reddish;
    // work planes amber.
    Color tint = c.planeEdge;
    if (plane.id == doc::origin::kPlaneXY) tint = c.planeXY;
    else if (plane.id == doc::origin::kPlaneXZ) tint = c.planeXZ;
    else if (plane.id == doc::origin::kPlaneYZ) tint = c.planeYZ;
    const float fade = faint ? 0.4f : 1.0f;
    const Frame& f = plane.frame;
    const Vec2 m = plane.centre;
    const double h = plane.half;
    const std::array<Vec3, 4> corners{f.ToWorld({m.x - h, m.y - h}), f.ToWorld({m.x + h, m.y - h}), f.ToWorld({m.x + h, m.y + h}),
                                      f.ToWorld({m.x - h, m.y + h})};
    // The square itself is drawn in 3D, so the body hides what is behind it.
    render::PlaneQuad quad;
    quad.corners = {corners[0], corners[1], corners[2], corners[3]};
    quad.fill = tint.WithAlpha((hot ? 0.22f : 0.06f) * fade);
    quad.edge = tint.WithAlpha(0.0f);
    quad.edgeWidth = 0.0f;
    app.shell.planeQuads.push_back(quad);
    // The border on top in the UI: overlapping planes keep clear outlines.
    std::array<ImVec2, 4> pts;
    for (std::size_t i = 0; i < 4; ++i) pts[i] = vf.Project(corners[i]);
    dl->AddPolyline(pts.data(), 4, ui::ToU32(tint.WithAlpha((hot ? 0.9f : 0.55f) * fade)), ImDrawFlags_Closed,
                    app.ui.S(hot ? 2.0f : 1.0f));
    // The label in the corner.
    const ImVec2 label = vf.Project(f.ToWorld({m.x - h * 0.92, m.y + h * 0.86}));
    dl->AddText(hot ? app.ui.fonts.title : app.ui.fonts.strong, app.ui.S(hot ? 14.0f : 12.0f), label,
                ui::ToU32(tint.WithAlpha((hot ? 1.0f : 0.8f) * fade)), plane.label.c_str());
}

void DrawWorkAxes(AppContext& app, ImDrawList* dl, const ViewportFrame& vf, const Session& session, std::size_t before,
                  ObjectId hot) {
    const ui::ThemeColors& c = app.ui.C();
    const double reach = PlaneHalfSize(session) * 1.2;
    const auto& features = session.doc.Features();
    for (std::size_t i = 0; i < features.size() && i < before; ++i) {
        const auto* wa = dynamic_cast<const doc::WorkAxisFeature*>(features[i].get());
        if (!wa || wa->suppressed || !wa->line) continue;
        const auto& [point, dir] = *wa->line;
        const ImVec2 a = vf.Project(point - dir * reach), b = vf.Project(point + dir * reach);
        const bool strong = wa->id == hot;
        const ImU32 ink = ui::ToU32(strong ? c.accent : c.planeEdge);
        // Dash-dot, as an axis is drawn on paper.
        const float len = std::hypot(b.x - a.x, b.y - a.y);
        if (len < 1) continue;
        const ImVec2 u{(b.x - a.x) / len, (b.y - a.y) / len};
        const float dash = app.ui.S(14), gap = app.ui.S(4), dot = app.ui.S(2);
        for (float t = 0; t < len; t += dash + gap + dot + gap) {
            const float e = std::min(t + dash, len);
            dl->AddLine({a.x + u.x * t, a.y + u.y * t}, {a.x + u.x * e, a.y + u.y * e}, ink, app.ui.S(strong ? 2.2f : 1.4f));
            const float d0 = e + gap;
            if (d0 < len)
                dl->AddLine({a.x + u.x * d0, a.y + u.y * d0}, {a.x + u.x * std::min(d0 + dot, len), a.y + u.y * std::min(d0 + dot, len)},
                            ink, app.ui.S(strong ? 2.2f : 1.4f));
        }
        dl->AddText(app.ui.fonts.bodySmall, app.ui.S(12), {b.x + app.ui.S(4), b.y - app.ui.S(14)}, ink, wa->name.c_str());
    }
}

ObjectId AxisUnderCursor(AppContext& app, const ViewportFrame& vf, ImVec2 mouse, const Session& session, std::size_t before) {
    const double reach = PlaneHalfSize(session) * 1.2;
    const auto& features = session.doc.Features();
    ObjectId best;
    float bestDistance = app.ui.S(6);
    for (std::size_t i = 0; i < features.size() && i < before; ++i) {
        const auto* wa = dynamic_cast<const doc::WorkAxisFeature*>(features[i].get());
        if (!wa || wa->suppressed || !wa->line) continue;
        const auto& [point, dir] = *wa->line;
        const ImVec2 a = vf.Project(point - dir * reach), b = vf.Project(point + dir * reach);
        const float dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
        if (len2 < 1) continue;
        const float t = std::clamp(((mouse.x - a.x) * dx + (mouse.y - a.y) * dy) / len2, 0.0f, 1.0f);
        const float d = std::hypot(a.x + dx * t - mouse.x, a.y + dy * t - mouse.y);
        if (d < bestDistance) {
            bestDistance = d;
            best = wa->id;
        }
    }
    return best;
}

std::vector<ShownPoint> WorkPoints(const Session& session, std::size_t before) {
    std::vector<ShownPoint> out;
    const auto& features = session.doc.Features();
    for (std::size_t i = 0; i < features.size() && i < before; ++i) {
        if (!session.doc.IsActive(i)) continue;
        const auto* wp = dynamic_cast<const doc::WorkPointFeature*>(features[i].get());
        if (wp && !wp->suppressed && wp->position) out.push_back({wp->id, *wp->position, wp->name});
    }
    return out;
}

void DrawWorkPoints(AppContext& app, ImDrawList* dl, const ViewportFrame& vf, const std::vector<ShownPoint>& points, ObjectId hot) {
    const ui::ThemeColors& c = app.ui.C();
    for (const ShownPoint& p : points) {
        const bool strong = p.id == hot;
        const ImU32 ink = ui::ToU32(strong ? c.accent : c.planeEdge);
        const ImVec2 at = vf.Project(p.at);
        const float arm = app.ui.S(strong ? 8.0f : 6.0f), w = app.ui.S(strong ? 2.0f : 1.3f);
        dl->AddLine({at.x - arm, at.y}, {at.x + arm, at.y}, ink, w);
        dl->AddLine({at.x, at.y - arm}, {at.x, at.y + arm}, ink, w);
        dl->AddCircleFilled(at, app.ui.S(strong ? 3.5f : 2.6f), ink);
        dl->AddText(app.ui.fonts.bodySmall, app.ui.S(12), {at.x + app.ui.S(6), at.y - app.ui.S(18)}, ink, p.label.c_str());
    }
}

const ShownPoint* PointUnderCursor(AppContext& app, const ViewportFrame& vf, ImVec2 mouse, const std::vector<ShownPoint>& points) {
    const ShownPoint* best = nullptr;
    float bestDistance = app.ui.S(8);
    for (const ShownPoint& p : points) {
        const ImVec2 at = vf.Project(p.at);
        const float d = std::hypot(at.x - mouse.x, at.y - mouse.y);
        if (d < bestDistance) {
            bestDistance = d;
            best = &p;
        }
    }
    return best;
}

} // namespace bz::app
