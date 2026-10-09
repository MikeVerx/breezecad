#include "app/features/SketchAxis.h"

#include "app/sketch/SketchOverlay.h"
#include "app/sketch/SketchPick.h"
#include "ui/Widgets.h"

namespace bz::app {

void DrawSketchAxis(AppContext& app, ImDrawList* dl, const SketchView& view, Vec2 a, Vec2 b, Color color, float width) {
    const Vec2 dir = (b - a).Normalized();
    const double reach = view.MmPerPixel() * 4000.0;   // well past the screen edges
    DrawSketchSegment(app.ui, dl, view, a - dir * reach, a + dir * reach, color, width, LinePattern::DashDot);
}

std::optional<ObjectId> PickSketchLine(AppContext& app, Session& session, const sketch::Sketch& sk, const SketchView& view) {
    if (!view.vf.hovered) return std::nullopt;
    const SketchHit hit = HitTest(sk, view, ImGui::GetMousePos(), session.doc.units);
    if (hit.kind != sketch::EntityKind::Line) return std::nullopt;
    const sketch::Line* line = sk.FindLine(hit.id);
    DrawSketchSegment(app.ui, ImGui::GetWindowDrawList(), view, sk.PositionOf(line->start), sk.PositionOf(line->end),
                      app.ui.C().sketchHover, 3.0f);
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return std::nullopt;
    return hit.id;
}

void SketchAxisChip(AppContext& app, const sketch::Line* line, bool legacyAxis, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const bool chosen = line || legacyAxis;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ui.S(34);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, {p.x + width, p.y + h}, ui::ToU32(chosen ? c.accentSoft : c.panel), ui.S(8));
    dl->AddRect(p, {p.x + width, p.y + h}, ui::ToU32(chosen ? c.accentSoftBorder : c.warning), ui.S(8));
    ui.icons.Draw(dl, chosen ? "ui-check" : "status-warning", {p.x + ui.S(10), p.y + (h - ui.S(14)) / 2}, ui.S(14), c.accent);
    const char* text = line ? (line->centerline ? "Осевая линия эскиза" : "Отрезок эскиза")
                            : legacyAxis ? "Ось из прежней версии файла" : "Ось не выбрана";
    dl->AddText(ui.fonts.medium, ui.S(13), {p.x + ui.S(32), p.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(c.text), text);
    ImGui::Dummy({width, h});
    ui::FontScope font(ui.fonts.caption);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textMuted));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::TextUnformatted("Щёлкните в окне модели осевую линию эскиза (отрезок с типом линии «Осевая»).");
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

ObjectId DefaultAxisLine(const sketch::Sketch& sk) {
    for (const sketch::Line& l : sk.lines)
        if (l.centerline) return l.id;
    for (const sketch::Line& l : sk.lines)
        if (l.construction) return l.id;
    return {};
}

} // namespace bz::app
