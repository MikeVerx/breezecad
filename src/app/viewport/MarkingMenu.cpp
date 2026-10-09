#include "app/viewport/MarkingMenu.h"

#include "app/features/OperationEditors.h"
#include "app/sketch/SketchEditor.h"
#include "core/I18n.h"
#include "ui/Widgets.h"

#include <array>
#include <cmath>
#include <optional>

namespace bz::app {
namespace {

constexpr double kHoldSeconds = 0.35;   ///< how long the button is held still before the menu opens
constexpr float  kStillPixels = 5.0f;   ///< moving more than this is a drag, not a hold
constexpr float  kRadius = 112.0f;      ///< from the centre to the items, design px
constexpr float  kDeadZone = 28.0f;     ///< inside it nothing is chosen

struct Item {
    const char* action;          ///< action id; nullptr: an empty slot
    const char* label = nullptr; ///< overrides the action's label
    const char* icon = nullptr;  ///< overrides the action's icon
};

/// Eight slots, clockwise from the top.
using Items = std::array<Item, 8>;

Items ItemsFor(AppContext& app, const Session& session) {
    if (app.ops.Active())
        return {{{"edit.confirm", Tr("Готово"), "ui-check"},
                 {"view.home"},
                 {nullptr},
                 {"view.fit"},
                 {"edit.escape", Tr("Отмена"), "ui-close"},
                 {nullptr},
                 {"tools.measure"},
                 {nullptr}}};
    if (session.mode == EditMode::Sketch)
        return {{{"sketch.line"},
                 {"sketch.rectangle"},
                 {"sketch.circle"},
                 {"sketch.arc"},
                 {"sketch.dimension"},
                 {"sketch.trim"},
                 {"sketch.construction"},
                 {"sketch.finish"}}};
    return {{{"sketch.start"},
             {"feature.extrude"},
             {"feature.revolve"},
             {"feature.hole"},
             {"feature.fillet"},
             {"tools.measure"},
             {"view.fit"},
             {"view.home"}}};
}

/// Direction of slot `i` on screen (y down).
ImVec2 SlotDirection(int i) {
    const double a = -kPi / 2 + i * kPi / 4;
    return {static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a))};
}

/// The slot the cursor points at, if it is out of the dead zone.
std::optional<int> PointedSlot(ui::Ui& ui, ImVec2 centre, ImVec2 mouse) {
    const float dx = mouse.x - centre.x, dy = mouse.y - centre.y;
    if (dx * dx + dy * dy < ui.S(kDeadZone) * ui.S(kDeadZone)) return std::nullopt;
    double a = std::atan2(dy, dx) + kPi / 2;   // 0 at the top, clockwise
    if (a < 0) a += 2 * kPi;
    return static_cast<int>(std::lround(a / (kPi / 4))) % 8;
}

void Run(AppContext& app, const Item& item) {
    const Action* a = item.action ? app.actions.Find(item.action) : nullptr;
    if (!a || !a->IsEnabled()) return;
    const std::string id = a->id;
    app.Later([&app, id] { app.actions.Run(id); });
}

void Close(ShellState::MarkingMenu& m) { m = {}; }

} // namespace

bool UpdateMarkingMenu(AppContext& app, Session& session, ViewportFrame& vf) {
    ShellState::MarkingMenu& m = app.shell.marking;
    ui::Ui& ui = app.ui;
    const ImVec2 mouse = ImGui::GetMousePos();

    if (!m.open) {
        if (vf.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !app.shell.handleHot) {
            m.armed = true;
            m.pressAt = mouse;
            m.pressTime = ImGui::GetTime();
        }
        if (!m.armed) return false;
        const float dx = mouse.x - m.pressAt.x, dy = mouse.y - m.pressAt.y;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || dx * dx + dy * dy > kStillPixels * kStillPixels) {
            m.armed = false;   // a click or a drag: not for the menu
            return false;
        }
        // The point tool has already made its point on the press: no menu there.
        if (session.mode == EditMode::Sketch && app.sketch.Tool() == SketchTool::Point) return false;
        if (ImGui::GetTime() - m.pressTime < kHoldSeconds) return false;
        m.open = true;
        m.centre = m.pressAt;
        m.held = true;
        // A drawing tool took the press as its first click: take it back.
        if (session.mode == EditMode::Sketch) app.sketch.DropPressClick();
    }

    vf.hovered = false;   // the menu owns the mouse now
    const Items items = ItemsFor(app, session);
    const std::optional<int> slot = PointedSlot(ui, m.centre, mouse);
    if (m.held && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m.held = false;
        if (slot && items[static_cast<std::size_t>(*slot)].action) {   // a flick toward a command
            Run(app, items[static_cast<std::size_t>(*slot)]);
            Close(m);
        }
        return true;   // released in the middle: stays open for a click
    }
    if (!m.held && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
        if (slot && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) Run(app, items[static_cast<std::size_t>(*slot)]);
        Close(m);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) Close(m);
    return true;
}

void DrawMarkingMenu(AppContext& app, Session& session) {
    ShellState::MarkingMenu& m = app.shell.marking;
    if (!m.open) return;
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const Items items = ItemsFor(app, session);
    const std::optional<int> slot = PointedSlot(ui, m.centre, ImGui::GetMousePos());

    // Hub: a ring with the pointer's direction.
    dl->AddCircleFilled(m.centre, ui.S(kDeadZone), ui::ToU32(c.floatingButton), 32);
    dl->AddCircle(m.centre, ui.S(kDeadZone), ui::ToU32(c.floatingBorder), 32, ui.S(1.2f));
    if (slot) {
        const ImVec2 d = SlotDirection(*slot);
        dl->AddLine({m.centre.x + d.x * ui.S(10), m.centre.y + d.y * ui.S(10)},
                    {m.centre.x + d.x * ui.S(kDeadZone - 4), m.centre.y + d.y * ui.S(kDeadZone - 4)}, ui::ToU32(c.accent), ui.S(3));
    }

    for (int i = 0; i < 8; ++i) {
        const Item& item = items[static_cast<std::size_t>(i)];
        const Action* a = item.action ? app.actions.Find(item.action) : nullptr;
        if (!a) continue;
        const bool enabled = a->IsEnabled();
        const bool hot = slot == i && enabled;
        const char* label = item.label ? Tr(item.label) : a->Label();
        const std::string_view icon = item.icon ? item.icon : a->icon;

        const ImVec2 d = SlotDirection(i);
        const ImVec2 p{m.centre.x + d.x * ui.S(kRadius), m.centre.y + d.y * ui.S(kRadius)};
        const ImVec2 ts = ui.fonts.medium->CalcTextSizeA(ui.S(13), FLT_MAX, 0, label);
        const float w = ui.S(14 + 18 + 8 + 14) + ts.x, h = ui.S(34);
        // Pills grow away from the centre: left ones end at their slot, right ones start there.
        float x = p.x - w / 2;
        if (d.x > 0.3f) x = p.x - ui.S(20);
        else if (d.x < -0.3f) x = p.x - w + ui.S(20);
        const ImVec2 lo{x, p.y - h / 2}, hi{x + w, p.y + h / 2};
        dl->AddRectFilled({lo.x, lo.y + ui.S(2)}, {hi.x, hi.y + ui.S(2)}, ui::ToU32(c.floatingBorder.WithAlpha(0.35f)), h / 2);
        dl->AddRectFilled(lo, hi, ui::ToU32(hot ? c.accent : c.floatingButton), h / 2);
        dl->AddRect(lo, hi, ui::ToU32(hot ? c.accent : c.floatingBorder), h / 2, 0, ui.S(1));
        const float alpha = enabled ? 1.0f : 0.38f;
        if (!icon.empty())
            ui.icons.Draw(dl, icon, {lo.x + ui.S(12), lo.y + (h - ui.S(18)) / 2}, ui.S(18),
                          (hot ? c.textOnAccent : c.textSecondary).WithAlpha(alpha));
        dl->AddText(ui.fonts.medium, ui.S(13), {lo.x + ui.S(38), lo.y + (h - ui.S(13)) / 2 - ui.S(1)},
                    ui::ToU32((hot ? c.textOnAccent : c.text).WithAlpha(alpha)), label);
    }
}

} // namespace bz::app
