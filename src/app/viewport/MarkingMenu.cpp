#include "app/viewport/MarkingMenu.h"

#include "app/features/OperationEditors.h"
#include "app/sketch/SketchEditor.h"
#include "core/Monitor.h"
#include "core/I18n.h"
#include "ui/Widgets.h"

#include <array>
#include <cmath>
#include <optional>

namespace bz::app {
namespace {

constexpr double kHoldSeconds = 0.35;   ///< how long the right button is held still before the menu opens
// A flick (as SolidWorks' mouse gestures): the button pressed and the cursor
// pulled quickly and straight; the command of that direction runs at once,
// without the menu. Off unless chosen in the settings.
constexpr double kFlickSeconds = 0.22;  ///< the pull must cover the distance within this
constexpr float  kFlickPixels = 64.0f;  ///< design px from the press
constexpr double kFlickStraight = 0.85; ///< distance from the press / path length
constexpr float  kStillPixels = 5.0f;   ///< moving more than this is a move, not a hold
constexpr float  kOpenPixels = 9.0f;    ///< a press moved this far (design px) opens the menu
constexpr float  kRadius = 112.0f;      ///< from the centre to the items, design px
constexpr float  kDeadZone = 28.0f;     ///< inside it nothing is chosen

struct Item {
    std::string action;          ///< action id; empty: an empty slot
    const char* label = nullptr; ///< overrides the action's label
    const char* icon = nullptr;  ///< overrides the action's icon
};

/// Eight slots, clockwise from the top.
using Items = std::array<Item, 8>;

HotEnvironment EnvironmentOf(const Session& session) {
    if (session.mode == EditMode::Sketch) return HotEnvironment::Sketch;
    return session.assembly ? HotEnvironment::Assembly : HotEnvironment::Part;
}

Items ItemsFor(AppContext& app, const Session& session) {
    if (app.ops.Active())
        return {{{"edit.confirm", Tr("Готово"), "ui-check"},
                 {"view.home"},
                 {""},
                 {"view.fit"},
                 {"edit.escape", Tr("Отмена"), "ui-close"},
                 {""},
                 {"tools.measure"},
                 {""}}};
    Items out;
    const auto ids = HotMenuItems(app.settings, EnvironmentOf(session));
    for (std::size_t i = 0; i < 8; ++i) out[i].action = ids[i];
    return out;
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

const Action* Usable(AppContext& app, const Item& item) {
    const Action* a = item.action.empty() ? nullptr : app.actions.Find(item.action);
    return a && a->IsEnabled() ? a : nullptr;
}

void Run(AppContext& app, const Item& item) {
    const Action* a = Usable(app, item);
    if (!a) return;
    mon::Action("hot menu", a->id);
    const std::string id = a->id;
    app.Later([&app, id] { app.actions.Run(id); });
}

void Close(ShellState::MarkingMenu& m) { m = {}; }

/// The press is the menu's: no orbit, pan or zoom started by it, and no
/// context menu on its release; the view goes back to where it was.
void TakePress(AppContext& app, ShellState::MarkingMenu& m) {
    ShellState::Navigation& nav = app.shell.nav;
    if (nav.orbiting || nav.panning || nav.zooming) app.viewport.SetCamera(m.cameraAtPress);
    nav.orbiting = nav.panning = nav.zooming = false;
    if (m.button == ImGuiMouseButton_Right) nav.button = -2;   // swallow the release
}

} // namespace

std::array<std::string, 8> HotMenuDefault(HotEnvironment env) {
    switch (env) {
        case HotEnvironment::Sketch:
            return {"sketch.line", "sketch.rectangle", "sketch.circle", "sketch.arc",
                    "sketch.dimension", "sketch.trim", "sketch.construction", "sketch.finish"};
        case HotEnvironment::Assembly:
            return {"assembly.place", "assembly.mate", "assembly.move", "assembly.rotate",
                    "assembly.create", "tools.measure", "view.fit", "view.home"};
        case HotEnvironment::Part: break;
    }
    return {"sketch.start", "feature.extrude", "feature.revolve", "feature.hole",
            "feature.fillet", "tools.measure", "view.fit", "view.home"};
}

std::array<std::string, 8> HotMenuItems(const Settings& settings, HotEnvironment env) {
    const std::vector<std::string>& own = env == HotEnvironment::Sketch ? settings.hotMenu.sketch
                                        : env == HotEnvironment::Assembly ? settings.hotMenu.assembly
                                                                          : settings.hotMenu.part;
    if (own.size() != 8) return HotMenuDefault(env);
    std::array<std::string, 8> out;
    std::copy(own.begin(), own.end(), out.begin());
    return out;
}

HotTrigger HotMenuTrigger(const Settings& settings) {
    switch (settings.hotMenu.trigger) {
        case 1: return HotTrigger::Right;
        case 2: return HotTrigger::ShiftLeft;
        case 3: return HotTrigger::Off;
        default: break;
    }
    // By the mouse set: the right button where nothing is bound to it alone.
    const MouseNavigation& m = settings.mouse;
    for (const MouseGesture* g : {&m.orbit, &m.pan, &m.zoom})
        if (g->button == MouseButtonChoice::Right && g->modifier == ModifierChoice::None) return HotTrigger::ShiftLeft;
    return HotTrigger::Right;
}

bool UpdateMarkingMenu(AppContext& app, Session& session, ViewportFrame& vf) {
    ShellState::MarkingMenu& m = app.shell.marking;
    ui::Ui& ui = app.ui;
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = ImGui::GetMousePos();
    const HotTrigger trigger = HotMenuTrigger(app.settings);

    // A Shift-click given back after it turned out not to be a menu gesture.
    if (m.replaying > 0) {
        --m.replaying;
        return false;
    }

    if (!m.open) {
        if (trigger == HotTrigger::Off) {
            m.armed = false;
            return false;
        }
        const bool rightPress = ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !io.KeyShift && !io.KeyCtrl && !io.KeyAlt;
        // The right button holds still for the menu in any case (as before);
        // Shift + left is a gesture only when it is the chosen way.
        const bool shiftPress = trigger == HotTrigger::ShiftLeft && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && io.KeyShift &&
                                !io.KeyCtrl && !io.KeyAlt;
        if (vf.hovered && (rightPress || shiftPress) && !app.shell.handleHot) {
            m.armed = true;
            m.button = shiftPress ? ImGuiMouseButton_Left : ImGuiMouseButton_Right;
            m.pressAt = mouse;
            m.pressTime = ImGui::GetTime();
            m.pathLength = 0.0;
            m.cameraAtPress = app.viewport.Camera();
        }
        if (!m.armed) return false;
        const bool left = m.button == ImGuiMouseButton_Left;
        // While Shift + left is undecided the overlays do not get the press:
        // a sketch tool would take it as its first point.
        if (left) vf.hovered = false;
        const float dx = mouse.x - m.pressAt.x, dy = mouse.y - m.pressAt.y;
        const double since = ImGui::GetTime() - m.pressTime;
        const ImVec2 step = io.MouseDelta;
        m.pathLength += std::sqrt(static_cast<double>(step.x * step.x + step.y * step.y));
        if (!ImGui::IsMouseDown(m.button)) {
            m.armed = false;
            if (left) {
                // A plain Shift-click (add to the selection): given back as it was.
                ImGuiIO& w = ImGui::GetIO();
                w.AddMousePosEvent(m.pressAt.x, m.pressAt.y);
                w.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                w.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                w.AddMousePosEvent(mouse.x, mouse.y);
                m.replaying = 3;
            }
            return left;   // a right click: the context menu
        }
        const double away = std::sqrt(static_cast<double>(dx * dx + dy * dy));
        const bool gesture = (m.button == ImGuiMouseButton_Right) == (trigger == HotTrigger::Right) || left;
        if (away > kStillPixels) {
            if (!gesture) {   // the right button turns the view here: just an orbit
                m.armed = false;
                return false;
            }
            // A flick runs the command of that direction at once (if chosen).
            if (app.settings.flickGestures && since <= kFlickSeconds && away >= ui.S(kFlickPixels) &&
                away >= kFlickStraight * std::max(m.pathLength, away)) {
                m.armed = false;
                const Items items = ItemsFor(app, session);
                const std::optional<int> slot = PointedSlot(ui, m.pressAt, mouse);
                if (!slot || !Usable(app, items[static_cast<std::size_t>(*slot)])) return left;
                TakePress(app, m);
                Run(app, items[static_cast<std::size_t>(*slot)]);
                vf.hovered = false;
                return true;
            }
            if (app.settings.flickGestures && since <= kFlickSeconds) return left;   // could still be a flick
            if (away < ui.S(kOpenPixels)) return left;
            // Moved: the menu, at once and in sight (Inventor makes you aim blind).
            m.open = true;
            m.centre = m.pressAt;
            m.held = true;
            TakePress(app, m);
        } else {
            if (since < kHoldSeconds || left) return left;
            // The right button held still: the menu too.
            m.open = true;
            m.centre = m.pressAt;
            m.held = true;
            TakePress(app, m);
        }
    }

    vf.hovered = false;   // the menu owns the mouse now
    const Items items = ItemsFor(app, session);
    const std::optional<int> slot = PointedSlot(ui, m.centre, mouse);
    if (m.held && ImGui::IsMouseReleased(m.button)) {
        m.held = false;
        if (slot && !items[static_cast<std::size_t>(*slot)].action.empty()) {   // released toward a command
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
        const Action* a = item.action.empty() ? nullptr : app.actions.Find(item.action);
        if (!a) continue;
        const bool enabled = a->IsEnabled();
        const bool hot = slot == i && enabled;
        const char* label = item.label ? Tr(item.label) : a->Label();
        // Both sides as views: a mixed `?:` would copy a->icon into a temporary
        // string, and the view would outlive it.
        const std::string_view icon = item.icon ? std::string_view(item.icon) : std::string_view(a->icon);

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
