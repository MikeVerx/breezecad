#include "app/features/PointPick.h"

#include "app/viewport/Planes.h"
#include "core/I18n.h"
#include "ui/Widgets.h"

#include <algorithm>

#include <string>

namespace bz::app {

namespace {

/// A valid slot: an existing point or the next one to add.
std::size_t ClampSlot(std::size_t active, std::size_t count, std::size_t max) {
    return std::min({active, count, max - 1});
}

} // namespace

bool PickPoints(AppContext& app, Session& session, const ViewportFrame& vf, std::size_t featureIndex, std::size_t max,
                std::size_t& active, std::vector<Vec3>& points, std::vector<std::optional<doc::EdgeReference>>& edges,
                std::vector<ObjectId>& workPoints) {
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
    edges.resize(points.size());
    workPoints.resize(points.size());
    active = ClampSlot(active, points.size(), max);
    if (active < points.size()) {
        points[active] = *at;
        edges[active] = edge;
        workPoints[active] = work;
    } else {
        points.push_back(*at);
        edges.push_back(edge);
        workPoints.push_back(work);
    }
    // The next empty field; with all filled, the one just set stays chosen.
    if (points.size() < max) active = points.size();
    return true;
}

bool PointSlots(AppContext& app, std::size_t max, std::size_t& active, std::vector<Vec3>& points,
                std::vector<std::optional<doc::EdgeReference>>& edges, std::vector<ObjectId>& workPoints, float width) {
    edges.resize(points.size());
    workPoints.resize(points.size());
    active = ClampSlot(active, points.size(), max);
    bool changed = false;
    std::optional<std::size_t> remove;
    for (std::size_t i = 0; i < max; ++i) {
        const std::string value = i >= points.size() ? std::string() : workPoints[i] ? std::string(Tr("рабочая точка"))
                                                                                       : std::string(Tr("вершина"));
        bool cleared = false;
        const std::string id = "##point-slot-" + std::to_string(i);
        // Fields after the next empty one wait for it.
        if (ui::SelectionSlot(app.ui, id.c_str(), Trf("Точка {}", i + 1), value, Tr("ожидает выбора…"), active == i, width, &cleared))
            active = std::min(i, points.size());
        if (cleared) remove = i;
    }
    if (remove && *remove < points.size()) {
        const auto at = static_cast<std::ptrdiff_t>(*remove);
        points.erase(points.begin() + at);
        edges.erase(edges.begin() + at);
        workPoints.erase(workPoints.begin() + at);
        active = points.size();
        changed = true;
    }
    return changed;
}

} // namespace bz::app
