#include "app/viewport/Planes.h"

#include "document/Origin.h"
#include "document/WorkPlaneFeature.h"
#include "ui/Widgets.h"

#include <algorithm>
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
    ui::ThemeColors c = app.ui.C();
    if (faint) {
        c.planeFill = c.planeFill.WithAlpha(c.planeFill.a * 0.35f);
        c.planeEdge = c.planeEdge.WithAlpha(c.planeEdge.a * 0.45f);
    }
    const Frame& f = plane.frame;
    const Vec2 m = plane.centre;
    const double h = plane.half;
    // The square itself is drawn in 3D, so the body hides what is behind it.
    render::PlaneQuad quad;
    quad.corners = {f.ToWorld({m.x - h, m.y - h}), f.ToWorld({m.x + h, m.y - h}), f.ToWorld({m.x + h, m.y + h}),
                    f.ToWorld({m.x - h, m.y + h})};
    quad.fill = hot ? c.regionFill : c.planeFill;
    quad.edge = hot ? c.sketchHover : c.planeEdge;
    quad.edgeWidth = hot ? 2.0f : 1.2f;
    app.shell.planeQuads.push_back(quad);
    // The label on top, in the UI.
    const ImVec2 label = vf.Project(f.ToWorld({m.x - h * 0.92, m.y + h * 0.86}));
    dl->AddText(app.ui.fonts.strong, app.ui.S(12), label, ui::ToU32(hot ? c.sketchHover : c.planeEdge), plane.label.c_str());
}

} // namespace bz::app
