#include "app/features/PointPick.h"

#include "app/viewport/Planes.h"
#include "ui/Widgets.h"

#include <string>

namespace bz::app {

bool PickPoints(AppContext& app, Session& session, const ViewportFrame& vf, std::size_t featureIndex, std::size_t max,
                std::vector<Vec3>& points, std::vector<std::optional<doc::EdgeReference>>& edges, std::vector<ObjectId>& workPoints) {
    app.viewport.BeginPickingOriginal(session.model, featureIndex, render::PickTarget::Vertices);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const std::vector<ShownPoint> shown = WorkPoints(session, featureIndex);
    const ImVec2 mouse = ImGui::GetMousePos();
    const ShownPoint* hot = vf.hovered ? PointUnderCursor(app, vf, mouse, shown) : nullptr;
    DrawWorkPoints(app, dl, vf, shown, hot ? hot->id : ObjectId{});
    const ImU32 ink = ui::ToU32(app.ui.C().accent);
    for (std::size_t i = 0; i < points.size(); ++i) {
        const ImVec2 p = vf.Project(points[i]);
        dl->AddCircleFilled(p, app.ui.S(5), ink);
        const std::string n = std::to_string(i + 1);
        dl->AddText(app.ui.fonts.strong, app.ui.S(12), {p.x + app.ui.S(7), p.y - app.ui.S(16)}, ink, n.c_str());
    }
    if (!vf.hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return false;
    std::optional<Vec3> at;
    std::optional<doc::EdgeReference> edge;
    ObjectId work;
    if (hot) {
        at = hot->at;
        work = hot->id;
    } else if (const auto v = app.viewport.PickVertex(vf.ToLocal(mouse))) {
        at = v->point;
        edge = v->edge;
    }
    if (!at) return false;
    if (points.size() >= max) {   // one more click starts over
        points.clear();
        edges.clear();
        workPoints.clear();
    }
    edges.resize(points.size());
    workPoints.resize(points.size());
    points.push_back(*at);
    edges.push_back(edge);
    workPoints.push_back(work);
    return true;
}

} // namespace bz::app
