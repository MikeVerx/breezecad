#include "app/mobile/MobileLayout.h"

#include "app/Operations.h"
#include "app/features/OperationEditors.h"
#include "app/mobile/PhoneHome.h"
#include "app/mobile/TouchInput.h"
#include "app/shell/Shell.h"
#include "app/sketch/SketchEditor.h"
#include "app/viewport/Measure.h"
#include "app/viewport/ViewportPanel.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "core/PathText.h"
#include "platform/Window.h"
#include "ui/Menus.h"
#include "ui/ViewCube.h"
#include "ui/Widgets.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>

#if defined(__ANDROID__)
#include "platform/Android.h"
#endif

namespace bz::app::mobile {
namespace {

// Sizes in dp (scaled by the interface scale through ui.S).
constexpr float kBrow = 44;    ///< the top row of a block
constexpr float kBeard = 26;   ///< the bottom line of a block
constexpr float kRow = 50;     ///< block 1's row: frame · cursor · cancel · OK
constexpr float kTabs = 38;    ///< block 1's tabs
constexpr float kStrip = 40;   ///< block 4, the projects
constexpr float kIsland = 42;  ///< the islands' height (blocks 3 and 5)

Color Dark() { return {0.05f, 0.05f, 0.06f, 1.0f}; }   ///< the camera panel: the colour of the cut-out

/// A floating panel over the model (blocks 3, 5, the cube): a window of its
/// own, so it is above the view and takes the touches there.
bool BeginIsland(ui::Ui& ui, const char* id, ImVec2 pos, ImVec2 size) {
    const ui::ThemeColors& c = ui.C();
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui.S(12));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::ToVec4(c.panel.WithAlpha(0.96f)));
    ImGui::PushStyleColor(ImGuiCol_Border, ui::ToVec4(c.divider));
    const bool open = ImGui::Begin(id, nullptr,
                                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
    return open;
}

void EndIsland() { ImGui::End(); }

void Run(AppContext& app, const char* id) {
    const std::string action = id;
    app.Later([&app, action] { app.actions.Run(action); });
}

/// An icon button running an action (greyed when it cannot run now).
void ActionButton(AppContext& app, const char* id, float size, float icon, bool active = false) {
    const Action* a = app.actions.Find(id);
    if (!a) return;
    if (ui::IconButton(app.ui, id, a->icon, size, icon, a->Label(), a->IsEnabled(), active)) Run(app, id);
}

// --- The camera panel ------------------------------------------------------------

/// Commands that can go to the hotbar ("+").
constexpr const char* kHotbarChoices[] = {
    "view.section",   "view.home",       "view.fit",      "view.shaded",       "view.edges",
    "view.wireframe", "view.transparent", "sketch.lookAt", "sketch.dimension", "sketch.line",     "sketch.circle",
    "sketch.rectangle", "feature.extrude", "feature.fillet", "file.exportStl", "tools.massProperties", "view.background",
    "view.appearance",
};

void DrawCameraPanel(AppContext& app, ImVec2 pos, ImVec2 size) {
    ui::Ui& ui = app.ui;
    MobileState& m = app.shell.mobile;
    ImDrawList* dl = BeginArea("##camera", pos, size, Dark());
    const float b = std::min(size.x - ui.S(4), ui.S(40));
    const float x = pos.x + (size.x - b) / 2;
    // Slots top to bottom; the camera's stretch stays empty.
    float y = pos.y + std::max(ui.S(6), m.insets.top);
    const auto clear = [&](float h) {
        if (m.cutoutTo > m.cutoutFrom && y < m.cutoutTo + ui.S(4) && y + h > m.cutoutFrom - ui.S(4)) y = m.cutoutTo + ui.S(6);
    };
    const auto place = [&] {
        clear(b);
        ImGui::SetCursorScreenPos({x, y});
        y += b + ui.S(4);
    };
    // The logo (the main menu) on top, "+" under it, the user's buttons, and
    // at the bottom "Скрыть управление" (the author, 17.10: it replaced the
    // pinned palette — the palette can be added with "+").
    place();
    const ImVec2 lp = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##logo", {b, b})) ImGui::OpenPopup("##main-menu");
    ui.icons.Draw(dl, "brand-mark", {lp.x + (b - ui.S(24)) / 2, lp.y + (b - ui.S(24)) / 2}, ui.S(24), {1, 1, 1, 1});
    MainMenuPopup(app);
    std::vector<std::string>& bar = app.settings.mobile.hotbar;
    if (!m.controlsHidden) {
        // Save, open and the ruler always sit under the logo (the author, 18.10: they left block 1's brow).
        for (const char* id : {"file.save", "file.open", "tools.measure"}) {
            place();
            ActionButton(app, id, b / ui.scale, 20, std::strcmp(id, "tools.measure") == 0 && app.shell.measuring);
        }
        y += ui.S(6);   // a gap: the fixed buttons, then the user's
        place();
        if (ui::IconButton(ui, "##hotbar-add", "ui-plus", b, 18, Tr("Добавить кнопку"))) ImGui::OpenPopup("##hotbar-pick");
    }
    // The user's buttons; a long press takes one away.
    const float hideY = pos.y + size.y - b - ui.S(6) - m.insets.bottom;
    for (std::size_t i = 0; i < bar.size(); ++i) {
        clear(b);
        if (y + b > hideY - ui.S(4)) break;
        ImGui::SetCursorScreenPos({x, y});
        ImGui::PushID(static_cast<int>(i));
        const Action* a = app.actions.Find(bar[i]);
        if (a && ui::IconButton(ui, "##hot", a->icon, b, 20, a->Label(), a->IsEnabled())) Run(app, a->id.c_str());
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            const std::string gone = bar[i];
            app.Later([&app, gone] {
                auto& list = app.settings.mobile.hotbar;
                list.erase(std::remove(list.begin(), list.end(), gone), list.end());
            });
        }
        ImGui::PopID();
        y += b + ui.S(4);
    }
    ImGui::SetCursorScreenPos({x, hideY});
    if (ui::IconButton(ui, "##hide-controls", m.controlsHidden ? "ui-eye" : "ui-eye-off", b, 20,
                       m.controlsHidden ? Tr("Показать управление") : Tr("Скрыть управление"), true, m.controlsHidden))
        m.controlsHidden = !m.controlsHidden;
    {
        ui::MenuStyle style(ui);
        if (ImGui::BeginPopup("##hotbar-pick")) {
            for (const char* id : kHotbarChoices) {
                const Action* a = app.actions.Find(id);
                if (!a) continue;
                const bool there = std::find(bar.begin(), bar.end(), id) != bar.end();
                if (ui::MenuItem(ui, a->Label(), nullptr, a->icon, there)) {
                    if (there) bar.erase(std::remove(bar.begin(), bar.end(), id), bar.end());
                    else bar.emplace_back(id);
                }
            }
            ImGui::EndPopup();
        }
    }
    EndArea();
}

// --- Block 1 ---------------------------------------------------------------------

bool HasToolPanel(AppContext& app, Session& session) {
    return app.ops.Active() || (app.shell.measuring && session.mode == EditMode::Model) ||
           (session.activeSketch && app.sketch.HasToolPanel());
}

void DrawRowButton(AppContext& app, const char* id, const char* icon, const char* tip, ImVec2 p, ImVec2 s, bool on, bool primary,
                   bool enabled, bool* clicked, bool* held, bool ready = false) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(p);
    const bool click = ImGui::InvisibleButton(id, s, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    if (clicked) *clicked = click && enabled && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    if (held) *held = enabled && ImGui::IsItemClicked(ImGuiMouseButton_Right);   // a long press (touch) or a right click
    const bool pressed = ImGui::IsItemActive();
    const ImVec2 a{p.x + ui.S(3), p.y + ui.S(4)}, b{p.x + s.x - ui.S(3), p.y + s.y - ui.S(4)};
    // ✓ turns green when the open operation has everything it needs: a tap applies it.
    const Color fill = primary ? (!enabled ? c.segmentTrack : ready ? c.success : c.accent) : on ? c.accentSoft : pressed ? c.pressed : c.segmentTrack;
    if (ready && primary) dl->AddRect({a.x - ui.S(2), a.y - ui.S(2)}, {b.x + ui.S(2), b.y + ui.S(2)}, ui::ToU32(c.success.WithAlpha(0.35f)), ui.S(12), 0, ui.S(3));
    dl->AddRectFilled(a, b, ui::ToU32(fill), ui.S(10));
    if (on && !primary) dl->AddRect(a, b, ui::ToU32(c.accentSoftBorder), ui.S(10));
    const float is = ui.S(22);
    const Color ink = !enabled ? c.textDisabled : primary ? c.textOnAccent : on ? c.accent : c.textSecondary;
    ui.icons.Draw(dl, icon, {p.x + (s.x - is) / 2, p.y + (s.y - is) / 2}, is, ink);
    (void)tip;
}

void DrawBlock1(AppContext& app, Session& session, ImVec2 pos, ImVec2 size, bool onLeft, bool logoHere) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    MobileState& m = app.shell.mobile;
    ImDrawList* dl = BeginArea("##block1", pos, size, c.panel);
    const float pad = onLeft ? std::max(0.0f, m.cornerRadius * 0.3f) : 0.0f;   // off the rounded corner
    const float edge = onLeft ? pos.x : pos.x + size.x;
    dl->AddLine({onLeft ? pos.x + size.x - 1 : pos.x, pos.y}, {onLeft ? pos.x + size.x - 1 : pos.x, pos.y + size.y}, ui::ToU32(c.divider));

    // Brow, only without the camera panel (no hotbar): logo · save · open · ruler.
    // With the hotbar they live there and block 1 starts with its tabs.
    float x = pos.x + ui.S(6) + pad;
    const float by = pos.y + std::max(0.0f, m.insets.top) + (ui.S(kBrow) - ui.S(36)) / 2;
    if (logoHere) {
        ImGui::SetCursorScreenPos({x, by});
        const ImVec2 lp = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##logo", ui.S(36, 36))) ImGui::OpenPopup("##main-menu");
        ui.icons.Draw(dl, "brand-app", {lp.x + ui.S(5), lp.y + ui.S(5)}, ui.S(26), {1, 1, 1, 1});
        MainMenuPopup(app);
        x += ui.S(40);
    }
    if (logoHere) {
        for (const char* id : {"file.save", "file.open", "tools.measure"}) {
            ImGui::SetCursorScreenPos({x, by});
            ActionButton(app, id, 36, 20, std::strcmp(id, "tools.measure") == 0 && app.shell.measuring);
            x += ui.S(40);
        }
    }
    float y = pos.y + std::max(0.0f, m.insets.top) + (logoHere ? ui.S(kBrow) : ui.S(4));

    // Tabs: tree / tool / dimensions. A new tool's panel comes forward by itself.
    const bool tool = HasToolPanel(app, session);
    if (tool && !m.hadToolPanel) m.block1Tab = 1;
    if (!tool && m.hadToolPanel && m.block1Tab == 1) m.block1Tab = 0;
    m.hadToolPanel = tool;
    {
        // The mockup: a strip of three; the chosen tab shows its name, the others an icon.
        const struct {
            const char* icon;
            const char* name;
        } tabs[] = {{"ui-tree", Tr("Дерево")}, {"ui-wrench", Tr("Инструмент")},
                    {"ui-dimension", session.assembly ? Tr("Свойства") : Tr("Размеры")}};
        const ImVec2 t0{pos.x + pad, y};
        const float th = ui.S(kTabs), iconW = ui.S(40);
        dl->AddRectFilled(t0, {pos.x + size.x - 1, y + th}, ui::ToU32(c.segmentTrack));
        const float wide = size.x - pad - 1 - ui.S(6) - 2 * iconW;
        float tx = t0.x + ui.S(3);
        for (int i = 0; i < 3; ++i) {
            const bool on = m.block1Tab == i;
            const float w = on ? wide : iconW;
            const ImVec2 a{tx, y + ui.S(3)}, bb{tx + w, y + th - ui.S(3)};
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##block1-tab", {w, bb.y - a.y})) m.block1Tab = i;
            ImGui::PopID();
            if (on) dl->AddRectFilled(a, bb, ui::ToU32(c.panel), ui.S(6));
            const float is = ui.S(16);
            if (on) {
                const ImVec2 ts = ui.fonts.medium->CalcTextSizeA(ui.S(12.5f), FLT_MAX, 0, tabs[i].name);
                const float total = is + ui.S(6) + ts.x;
                const float sx = a.x + std::max(ui.S(4), (w - total) / 2);
                ui.icons.Draw(dl, tabs[i].icon, {sx, (a.y + bb.y - is) / 2}, is, c.text);
                ui::FittedText(dl, ui.fonts.medium, ui.S(12.5f), {sx + is + ui.S(6), (a.y + bb.y - ts.y) / 2}, bb.x - ui.S(4),
                               ui::ToU32(c.text), tabs[i].name);
            } else {
                ui.icons.Draw(dl, tabs[i].icon, {a.x + (w - is) / 2, (a.y + bb.y - is) / 2}, is, c.textSecondary);
            }
            tx += w;
        }
    }
    y += ui.S(kTabs);

    // Content, then the numpad when a value is typed, the row, the beard.
    const float bottom = pos.y + size.y - m.insets.bottom;   // the row is the block's bottom (no beard)
    const float rowTop = bottom - ui.S(kRow);
    const float padH = m.numpad ? NumpadHeight(ui) : 0.0f;
    const float contentH = rowTop - padH - y;
    // The numpad takes the block when there is no room for both (a low phone
    // screen). The content is drawn anyway, at full height, under it: the value
    // being typed lives in a field there, and a field ImGui does not see for a
    // frame loses focus — the numpad (and ⌨) closed as soon as it opened. The
    // numpad is drawn over it on the foreground layer, opaque; its keys are
    // taken by the touch input before ImGui, so nothing reaches the panel below.
    const bool room = contentH >= ui.S(40);
    {
        const ImVec2 cp{pos.x + pad, y};
        BeginArea("##block1-content", cp, {size.x - pad - 1, room ? contentH : rowTop - y}, c.panel, true);
        const float width = size.x - pad - 1;
        switch (m.block1Tab) {
            case 0:
                if (session.assembly) DrawAssemblyTree(app, session, width);
                else DrawTreePanel(app, session, width);
                break;
            case 1:
                if (OperationEditor* op = app.ops.Active()) op->DrawPanel(app, session);
                else if (app.shell.measuring && session.mode == EditMode::Model) DrawMeasurePanel(app, session, width);
                else if (session.activeSketch && app.sketch.HasToolPanel()) app.sketch.DrawToolPanel(app, session, width);
                else {
                    ImGui::SetCursorScreenPos({cp.x + ui.S(12), cp.y + ui.S(14)});
                    ImGui::PushTextWrapPos(cp.x + width - ui.S(12));
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textMuted));
                    ImGui::TextWrapped("%s", Tr("Выберите инструмент в списке справа — здесь появятся его параметры."));
                    ImGui::PopStyleColor();
                    ImGui::PopTextWrapPos();
                }
                break;
            default:
                if (session.assembly) DrawPropertiesPanel(app, session, width);
                else DrawVariablesPanel(app, session, width);
                break;
        }
        EndArea();
    }
    if (m.numpad) DrawNumpad(app, {pos.x + ui.S(4) + pad, rowTop - padH + ui.S(2)}, {size.x - ui.S(8) - pad, padH - ui.S(4)});
    else m.numpadKeys.clear();

    // The row: from the screen's edge to the model — frame · cursor · cancel · OK.
    {
        const bool cursorOn = CursorActive(app);
        const bool sketch = session.mode == EditMode::Sketch;
        const float w = (size.x - pad - ui.S(8)) / 4.0f;
        const float h = ui.S(kRow);
        struct Slot {
            const char* id;
            const char* icon;
            const char* tip;
        };
        const Slot slots[4] = {{"##row-frame", "ui-frame", Tr("Выделение рамкой")},
                               {"##row-cursor", "ui-cursor", Tr("Курсор")},
                               {"##row-cancel", "ui-close", Tr("Отмена")},
                               {"##row-ok", "ui-check", Tr("ОК")}};
        for (int i = 0; i < 4; ++i) {
            // Left of the model: in this order from the left. Right of it (the left hand): mirrored.
            const int place = onLeft ? i : 3 - i;
            const ImVec2 p{pos.x + pad + ui.S(4) + static_cast<float>(place) * w, rowTop};
            bool clicked = false, held = false;
            switch (i) {
                case 0:
                    DrawRowButton(app, slots[i].id, slots[i].icon, slots[i].tip, p, {w, h}, m.boxMode, false, true, &clicked, nullptr);
                    if (clicked) {
                        m.boxMode = !m.boxMode;
                        m.boxHeld = false;
                    }
                    break;
                case 1:
                    // A sketch has its own switch; a drawing tool keeps the cursor on whatever it says.
                    DrawRowButton(app, slots[i].id, slots[i].icon, slots[i].tip, p, {w, h}, cursorOn, false, true, &clicked, nullptr);
                    if (clicked && sketch && SketchToolNeedsCursor(app)) {
                        app.notes.Info(Tr("Рисуют только с курсором. Без него — выбор, размеры, обрезка и зависимости."));
                        clicked = false;
                    }
                    if (clicked) {
                        bool& on = sketch ? m.cursorInSketch : m.cursorIn3d;
                        on = !on;
                        if (on && !m.cursorShown) {   // first time: in the middle of the model
                            m.cursorTip = {(m.viewMin.x + m.viewMax.x) / 2, (m.viewMin.y + m.viewMax.y) / 2};
                            m.cursorFinger = {m.cursorTip.x, m.cursorTip.y + CursorReach(app)};
                            m.cursorShown = true;
                        }
                    }
                    break;
                case 2:
                    DrawRowButton(app, slots[i].id, slots[i].icon, slots[i].tip, p, {w, h}, false, false, true, &clicked, nullptr);
                    if (clicked) m.cancelPressed = true;
                    break;
                default:
                {
                    const OperationEditor* op = app.ops.Active();
                    const bool ready = op && op->HasApply() && !m.boxMode && op->Ready(session);
                    DrawRowButton(app, slots[i].id, slots[i].icon, slots[i].tip, p, {w, h}, m.boxHeld, true, true, &clicked, &held, ready);
                    m.okDown = ImGui::IsItemActive();
                }
                    if (clicked) m.okPressed = true;
                    if (held) m.okLongPressed = true;   // a long press confirms the operation, cursor or not
                    break;
            }
        }
    }

    dl->AddLine({pos.x, rowTop}, {pos.x + size.x, rowTop}, ui::ToU32(c.divider));
    (void)edge;
    EndArea();
}

// --- Block 2 ---------------------------------------------------------------------

/// What block 2's status line says.
struct StatusText {
    const char* icon = nullptr;
    std::string main;   ///< the open tool, or the document
    std::string mode;   ///< "3D-модель", "Эскиз", "Вид", "Сборка"
    bool mono = false;  ///< coordinates
};

StatusText Status(AppContext& app, Session& session) {
    MobileState& m = app.shell.mobile;
    StatusText st;
    st.mode = session.assembly ? Tr("Сборка")
              : session.mode == EditMode::Sketch ? Tr("Эскиз")
              : session.mode == EditMode::PickPlane ? Tr("Выбор плоскости")
              : app.shell.ribbonTab == RibbonTab::View ? Tr("Вид") : Tr("3D-модель");
    if (CursorActive(app) && m.cursorShown && m.boxMode) {
        st.icon = "ui-frame";
        st.main = m.boxHeld ? Tr("Рамка: второй угол — ОК") : Tr("Рамка: первый угол — ОК");
        st.mode.clear();
        return st;
    }
    if (const OperationEditor* op = app.ops.Active()) {
        st.icon = op->Icon();
        st.main = op->Title();
    } else if (app.shell.measuring && session.mode == EditMode::Model) {
        st.icon = "measure";
        st.main = Tr("Измерение");
    } else if (session.mode == EditMode::Sketch && app.sketch.Tool() != SketchTool::Select) {
        const SketchToolInfo& info = InfoOf(app.sketch.Tool());
        st.icon = info.icon;
        st.main = Tr(info.label);
    } else if (CursorActive(app) && m.cursorShown && session.mode != EditMode::Sketch) {
        // The model point under the tip (read again only when the tip moved).
        static ImVec2 pickedAt{-1, -1};
        static std::optional<Vec3> picked;
        if (pickedAt.x != m.cursorTip.x || pickedAt.y != m.cursorTip.y) {
            pickedAt = m.cursorTip;
            const auto hit = app.viewport.Pick({m.cursorTip.x - m.viewMin.x, m.cursorTip.y - m.viewMin.y});
            picked = hit ? std::optional<Vec3>(hit->point) : std::nullopt;
        }
        if (picked) {
            st.main = std::format("X {:.1f}  Y {:.1f}  Z {:.1f}", picked->x, picked->y, picked->z);
            st.mono = true;
            st.mode.clear();
            return st;
        }
    }
    if (st.main.empty()) {
        st.icon = session.assembly ? "tree-assembly" : session.mode == EditMode::Sketch ? "tree-sketch" : "tree-part";
        st.main = session.Title();
    }
    return st;
}

void DrawSearch(AppContext& app, ImVec2 pos, float width, float height) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    ImGui::SetCursorScreenPos({pos.x + ui.S(6), pos.y + ui.S(4)});
    if (app.shell.focusPalette) {
        ImGui::SetKeyboardFocusHere();
        app.shell.focusPalette = false;
    }
    ui::TextField(ui, "##phone-search", app.shell.paletteQuery, (width - ui.S(12)) / ui.scale, Tr("Поиск команд"));
    BeginArea("##phone-search-results", {pos.x, pos.y + ui.S(44)}, {width, height - ui.S(44)}, c.panel, true);
    int shown = 0;
    for (const Action* a : app.actions.Search(app.shell.paletteQuery)) {
        if (shown++ >= 30) break;
        ImGui::PushID(a->id.c_str());
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool enabled = a->IsEnabled();
        if (ImGui::InvisibleButton("##found", {width, ui.S(44)}) && enabled) {
            Run(app, a->id.c_str());
            app.shell.paletteOpen = false;
            app.shell.paletteQuery.clear();
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (!a->icon.empty()) ui.icons.Draw(dl, a->icon, {p.x + ui.S(10), p.y + ui.S(11)}, ui.S(22), enabled ? Color{1, 1, 1, 1} : Color{1, 1, 1, 0.4f});
        dl->AddText(ui.fonts.body, ui.S(14), {p.x + ui.S(42), p.y + ui.S(13)}, ui::ToU32(enabled ? c.text : c.textDisabled), a->Label());
        ImGui::PopID();
    }
    EndArea();
}

void DrawBlock2(AppContext& app, Session& session, ImVec2 pos, ImVec2 size, bool onRight, bool iconsOnly) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    MobileState& m = app.shell.mobile;
    ImDrawList* dl = BeginArea("##block2", pos, size, c.panel);
    dl->AddLine({onRight ? pos.x : pos.x + size.x - 1, pos.y}, {onRight ? pos.x : pos.x + size.x - 1, pos.y + size.y}, ui::ToU32(c.divider));
    const float pad = onRight ? std::max(0.0f, m.cornerRadius * 0.3f) : 0.0f;
    const float inner = size.x - pad;
    const float top = pos.y + std::max(0.0f, m.insets.top);

    // Brow: frames per second · search (a field; icons only: a button).
    const std::string fps = iconsOnly ? std::format("{:.0f}", app.stats.fps) : std::format("{:.0f} fps", app.stats.fps);
    const float fpsW = ui.fonts.mono->CalcTextSizeA(ui.S(11), FLT_MAX, 0, fps.c_str()).x;
    dl->AddText(ui.fonts.mono, ui.S(11), {pos.x + ui.S(8), top + (ui.S(kBrow) - ui.S(11)) / 2}, ui::ToU32(c.success), fps.c_str());
    const auto toggleSearch = [&] {
        app.shell.paletteOpen = !app.shell.paletteOpen;
        app.shell.focusPalette = app.shell.paletteOpen;
        app.shell.paletteQuery.clear();
    };
    if (iconsOnly) {
        ImGui::SetCursorScreenPos({pos.x + inner - ui.S(40), top + ui.S(4)});
        if (ui::IconButton(ui, "##search", "ui-search", 36, 18, Tr("Поиск команд"), true, app.shell.paletteOpen)) toggleSearch();
    } else {
        const ImVec2 a{pos.x + ui.S(14) + fpsW, top + ui.S(7)}, b{pos.x + inner - ui.S(6), top + ui.S(kBrow) - ui.S(7)};
        ImGui::SetCursorScreenPos(a);
        if (ImGui::InvisibleButton("##search", {b.x - a.x, b.y - a.y})) toggleSearch();
        dl->AddRectFilled(a, b, ui::ToU32(c.field), ui.S(8));
        dl->AddRect(a, b, ui::ToU32(app.shell.paletteOpen ? c.accent : c.fieldBorder), ui.S(8));
        ui.icons.Draw(dl, "ui-search", {a.x + ui.S(8), (a.y + b.y - ui.S(13)) / 2}, ui.S(13), c.textMuted);
        dl->AddText(ui.fonts.body, ui.S(12.5f), {a.x + ui.S(27), (a.y + b.y - ui.S(13)) / 2}, ui::ToU32(c.textMuted), Tr("Поиск"));
    }
    float y = top + ui.S(kBrow);
    const float bottom = pos.y + size.y - ui.S(kBeard) - m.insets.bottom;

    if (app.shell.paletteOpen) {
        DrawSearch(app, {pos.x, y}, inner, bottom - y);
    } else {
        // "Завершить эскиз" pinned above the list (soft accent, ADR-0004).
        if (session.mode == EditMode::Sketch) {
            const Action* finish = app.actions.Find("sketch.finish");
            // Compact (the author's mockups, 18.10): one slim line, the list gets the room.
            const ImVec2 a{pos.x + ui.S(8), y + ui.S(2)}, b{pos.x + inner - ui.S(8), y + ui.S(38)};
            ImGui::SetCursorScreenPos(a);
            if (ImGui::InvisibleButton("##finish-sketch", {b.x - a.x, b.y - a.y}) && finish->IsEnabled()) Run(app, "sketch.finish");
            dl->AddRectFilled(a, b, ui::ToU32(ImGui::IsItemActive() ? c.pressed : c.accentSoft), ui.S(10));
            dl->AddRect(a, b, ui::ToU32(c.accentSoftBorder), ui.S(10));
            const float is = ui.S(20);
            if (iconsOnly) {
                ui.icons.Draw(dl, finish->icon, {(a.x + b.x - is) / 2, (a.y + b.y - is) / 2}, is, {1, 1, 1, 1});
            } else {
                ui.icons.Draw(dl, finish->icon, {a.x + ui.S(8), (a.y + b.y - is) / 2}, is, {1, 1, 1, 1});
                ui::FittedText(dl, ui.fonts.medium, ui.S(13), {a.x + ui.S(36), (a.y + b.y - ui.S(13)) / 2 - ui.S(1)}, b.x - ui.S(6),
                               ui::ToU32(c.accent), Tr("Завершить эскиз"));
            }
            y += ui.S(42);
        }
        BeginArea("##block2-list", {pos.x + (onRight ? pad : 0), y}, {inner, bottom - y}, c.panel, true);
        m.flyItems.clear();   // refilled by the open group, if it is still there
        DrawToolList(app, session, inner, iconsOnly, !app.settings.mobile.toolsList, onRight);
        EndArea();
    }

    // The status line (the author, 18.10): what is open and in which mode — "▭ Прямоугольник · Эскиз";
    // a rebuild's progress ("74/100") on the right while it runs. With the cursor in 3D: the point under the tip.
    {
        dl->AddRectFilled({pos.x, bottom}, {pos.x + size.x, pos.y + size.y}, ui::ToU32(c.statusBar));
        dl->AddLine({pos.x, bottom}, {pos.x + size.x, bottom}, ui::ToU32(c.divider));
        const StatusText st = Status(app, session);
        const float fy = bottom + ui.S(kBeard) / 2;
        float tx = pos.x + ui.S(8) + (onRight ? pad : 0.0f);
        const float right = pos.x + size.x - ui.S(8) - (onRight ? 0.0f : pad);
        if (st.icon) {
            ui.icons.Draw(dl, st.icon, {tx, fy - ui.S(7)}, ui.S(14), c.accent);
            tx += ui.S(19);
        }
        const kernel::Model::Progress p = session.Rebuilding() ? session.RebuildProgress() : kernel::Model::Progress{};
        float textEnd = right;
        if (p.total > 0 && !iconsOnly) {
            const std::string prog = std::format("{}/{}", p.done, p.total);
            const float pw = ui.fonts.mono->CalcTextSizeA(ui.S(10.5f), FLT_MAX, 0, prog.c_str()).x;
            dl->AddText(ui.fonts.mono, ui.S(10.5f), {right - pw, fy - ui.S(6)}, ui::ToU32(c.accent), prog.c_str());
            textEnd = right - pw - ui.S(6);
        }
        if (!iconsOnly && textEnd > tx) {
            ImGui::PushClipRect({tx, bottom}, {textEnd, pos.y + size.y}, true);
            ImFont* font = st.mono ? ui.fonts.mono : ui.fonts.bodySmall;
            const float fs = ui.S(st.mono ? 10.5f : 11.5f);
            dl->AddText(font, fs, {tx, fy - fs * 0.6f}, ui::ToU32(c.text), st.main.c_str());
            if (!st.mode.empty()) {
                const float mw = font->CalcTextSizeA(fs, FLT_MAX, 0, st.main.c_str()).x;
                const std::string tail = " · " + st.mode;
                dl->AddText(ui.fonts.bodySmall, ui.S(11.5f), {tx + mw, fy - ui.S(11.5f) * 0.6f}, ui::ToU32(c.textMuted), tail.c_str());
            }
            ImGui::PopClipRect();
        }
    }
    EndArea();
}

// --- Blocks 3 and 5, the cube --------------------------------------------------------

/// The tabs island's width (as DrawTabsIsland lays it out).
float TabsIslandWidth(AppContext& app, Session& session, float maxWidth) {
    ui::Ui& ui = app.ui;
    if (app.shell.mobile.tabsFolded) return ui.S(40);
    const char* part[] = {Tr("3D-модель"), Tr("Эскиз"), Tr("Вид")};
    const char* assembly[] = {Tr("Сборка"), Tr("Вид")};
    float width = ui.S(34);
    if (session.assembly)
        for (const char* t : assembly) width += ui.fonts.medium->CalcTextSizeA(ui.S(13), FLT_MAX, 0, t).x + ui.S(24);
    else
        for (const char* t : part) width += ui.fonts.medium->CalcTextSizeA(ui.S(13), FLT_MAX, 0, t).x + ui.S(24);
    return std::min(width + ui.S(4), maxWidth);
}

void DrawTabsIsland(AppContext& app, Session& session, ImVec2 at, float maxWidth) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    MobileState& m = app.shell.mobile;
    struct Tab {
        const char* label;
        RibbonTab   tab;
    };
    const Tab partTabs[] = {{Tr("3D-модель"), RibbonTab::Model}, {Tr("Эскиз"), RibbonTab::Sketch}, {Tr("Вид"), RibbonTab::View}};
    const Tab asmTabs[] = {{Tr("Сборка"), RibbonTab::Model}, {Tr("Вид"), RibbonTab::View}};
    const std::span<const Tab> tabs = session.assembly ? std::span<const Tab>(asmTabs) : std::span<const Tab>(partTabs);
    if (session.assembly && app.shell.ribbonTab == RibbonTab::Sketch) app.shell.ribbonTab = RibbonTab::Model;
    const float h = ui.S(kIsland), arrow = ui.S(34);
    if (m.tabsFolded) {
        // Folded: only the arrow, its own island before undo/redo (the mockup).
        if (BeginIsland(ui, "##island-tabs", at, {ui.S(36) + ui.S(4), h})) {
            ImGui::SetCursorScreenPos({at.x + ui.S(2), at.y + ui.S(4)});
            if (ui::IconButton(ui, "##fold-tabs", "ui-chevron-right", 34, 14, Tr("Показать вкладки"))) m.tabsFolded = false;
        }
        EndIsland();
        return;
    }
    float width = arrow;
    if (!m.tabsFolded)
        for (const Tab& t : tabs) width += ui.fonts.medium->CalcTextSizeA(ui.S(13), FLT_MAX, 0, t.label).x + ui.S(24);
    width = std::min(width + ui.S(4), maxWidth);
    if (!BeginIsland(ui, "##island-tabs", at, {width, h})) {
        EndIsland();
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos({at.x + ui.S(2), at.y + ui.S(4)});
    if (ui::IconButton(ui, "##fold-tabs", "ui-arrow-left", 32, 14, Tr("Свернуть вкладки"))) m.tabsFolded = true;
    dl->AddLine({at.x + arrow + ui.S(1), at.y + ui.S(8)}, {at.x + arrow + ui.S(1), at.y + h - ui.S(8)}, ui::ToU32(c.dividerSoft));
    {
        float x = at.x + arrow;
        for (const Tab& t : tabs) {
            const bool on = app.shell.ribbonTab == t.tab;
            ImFont* font = on ? ui.fonts.strong : ui.fonts.medium;
            const float w = font->CalcTextSizeA(ui.S(13), FLT_MAX, 0, t.label).x + ui.S(24);
            ImGui::SetCursorScreenPos({x, at.y});
            ImGui::PushID(t.label);
            if (ImGui::InvisibleButton("##tab", {w, h})) app.shell.ribbonTab = t.tab;
            ImGui::PopID();
            if (on) dl->AddRectFilled({x + ui.S(2), at.y + ui.S(5)}, {x + w - ui.S(2), at.y + h - ui.S(5)}, ui::ToU32(c.accentSoft), ui.S(8));
            dl->AddText(font, ui.S(13), {x + ui.S(12), at.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(on ? c.accent : c.textSecondary),
                        t.label);
            x += w;
        }
    }
    EndIsland();
}

/// The open tool's choices over the view, instead of the tabs (the author's mockups 7a/7c, 18.10):
/// groups side by side, a thin divider between them; icons, or short text where a choice has
/// no picture yet. Too wide: it scrolls sideways (a drag), the far edge fades, a thin bar under it.
/// A tap sets the choice for ToolChoice to take next frame. Returns the island's width.
float ChoiceIslandWidth(ui::Ui& ui, const std::vector<const MobileState::ChoiceGroup*>& groups) {
    float w = ui.S(6);
    for (std::size_t g = 0; g < groups.size(); ++g) {
        if (g) w += ui.S(9);
        for (const auto& [icon, label] : groups[g]->items)
            w += icon.empty() ? ui.fonts.medium->CalcTextSizeA(ui.S(12), FLT_MAX, 0, label.c_str()).x + ui.S(18) : ui.S(34);
    }
    return w;
}

void DrawChoiceIsland(AppContext& app, const char* id, const std::vector<const MobileState::ChoiceGroup*>& groups, ImVec2 at,
                      float width, bool scrolls) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    MobileState& m = app.shell.mobile;
    const float h = ui.S(kIsland);
    const float content = ChoiceIslandWidth(ui, groups);
    const float maxScroll = scrolls ? std::max(0.0f, content - width) : 0.0f;
    float& scroll = m.choiceScroll;
    if (!scrolls) scroll = 0;
    scroll = std::clamp(scroll, 0.0f, maxScroll);
    if (!BeginIsland(ui, id, at, {width, h})) {
        EndIsland();
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(at);
    static float dragged = 0;
    const bool released = ImGui::InvisibleButton("##choices", {width, h});
    if (ImGui::IsItemActivated()) dragged = 0;
    if (ImGui::IsItemActive()) {
        const float dx = ImGui::GetIO().MouseDelta.x;
        dragged += std::abs(dx);
        scroll = std::clamp(scroll - dx, 0.0f, maxScroll);
    }
    const bool held = ImGui::IsItemActive();
    const ImVec2 mouse = ImGui::GetMousePos();
    dl->PushClipRect({at.x + ui.S(2), at.y}, {at.x + width - ui.S(2), at.y + h}, true);
    float x = at.x + ui.S(3) - scroll;
    // For the emulator's check: where the island is, and the first visible item that can be
    // chosen (written again whenever that changes — after a scroll, say).
    static std::string loggedIsland, logged;
    bool candidateLogged = false;
    if (const std::string place = std::format("{} {:.0f},{:.0f} {:.0f}", id, at.x, at.y, width); place != loggedIsland && !held) {
        loggedIsland = place;
        log::Info("Touch: choices {} at {:.0f},{:.0f} size {:.0f}x{:.0f}", id, at.x, at.y, width, h);
    }
    for (std::size_t g = 0; g < groups.size(); ++g) {
        const MobileState::ChoiceGroup& grp = *groups[g];
        if (g) {
            dl->AddLine({x + ui.S(4), at.y + ui.S(10)}, {x + ui.S(4), at.y + h - ui.S(10)}, ui::ToU32(c.divider));
            x += ui.S(9);
        }
        for (std::size_t i = 0; i < grp.items.size(); ++i) {
            const auto& [icon, label] = grp.items[i];
            const float w = icon.empty() ? ui.fonts.medium->CalcTextSizeA(ui.S(12), FLT_MAX, 0, label.c_str()).x + ui.S(18) : ui.S(34);
            const ImVec2 a{x + ui.S(1), at.y + ui.S(5)}, b{x + w - ui.S(1), at.y + h - ui.S(5)};
            const bool on = static_cast<int>(i) == grp.current;
            const bool off = (grp.disabled >> i) & 1;
            const bool under = held && dragged < ui.S(8) && mouse.x >= a.x && mouse.x < b.x;
            if (on || under) dl->AddRectFilled(a, b, ui::ToU32(under ? c.pressed : c.accentSoft), ui.S(8));
            if (on) dl->AddRect(a, b, ui::ToU32(c.accentSoftBorder), ui.S(8));
            if (icon.empty()) {
                dl->AddText(ui.fonts.medium, ui.S(12), {x + ui.S(9), at.y + (h - ui.S(12)) / 2 - ui.S(1)},
                            ui::ToU32(off ? c.textDisabled : on ? c.accent : c.textSecondary), label.c_str());
            } else {
                const float is = ui.S(22);
                ui.icons.Draw(dl, icon, {x + (w - is) / 2, at.y + (h - is) / 2}, is, off ? Color{1, 1, 1, 0.3f} : Color{1, 1, 1, 1});
            }
            if (!candidateLogged && !held && !on && !off && a.x >= at.x && b.x <= at.x + width) {   // a visible one
                candidateLogged = true;
                if (const std::string key = std::format("{} {} {:.0f}", grp.id, i, a.x); key != logged) {
                    logged = key;
                    log::Info("Touch: choice {} {} at {:.0f},{:.0f} size {:.0f}", grp.id, i, a.x, a.y, w);
                }
            }
            if (released && dragged < ui.S(8) && mouse.x >= a.x && mouse.x < b.x && !off) {
                m.pickedChoice = grp.id;
                m.pickedIndex = static_cast<int>(i);
                m.block1Tab = 1;   // the panel takes the choice: it must be drawn
                log::Info("Choice: {} {}", grp.id, i);
            }
            x += w;
        }
    }
    dl->PopClipRect();
    if (maxScroll > 0) {
        // The far end fades (and the near one once scrolled); a thin bar shows where we are.
        const float fw = ui.S(30);
        const ImU32 solid = ui::ToU32(c.panel), clear = ui::ToU32(c.panel.WithAlpha(0.0f));
        if (scroll < maxScroll - 0.5f)
            dl->AddRectFilledMultiColor({at.x + width - fw, at.y + 1}, {at.x + width - 1, at.y + h - 1}, clear, solid, solid, clear);
        if (scroll > 0.5f) dl->AddRectFilledMultiColor({at.x + 1, at.y + 1}, {at.x + fw, at.y + h - 1}, solid, clear, clear, solid);
        const float barW = width - ui.S(20);
        const float knob = std::max(ui.S(16), barW * width / content);
        const float kx = at.x + ui.S(10) + (barW - knob) * (scroll / maxScroll);
        dl->AddRectFilled({at.x + ui.S(10), at.y + h - ui.S(4)}, {at.x + ui.S(10) + barW, at.y + h - ui.S(2)}, ui::ToU32(c.divider), ui.S(1));
        dl->AddRectFilled({kx, at.y + h - ui.S(4)}, {kx + knob, at.y + h - ui.S(2)}, ui::ToU32(c.accent), ui.S(1));
    }
    EndIsland();
}

float DrawUndoIsland(AppContext& app, ImVec2 at) {
    ui::Ui& ui = app.ui;
    const float h = ui.S(kIsland), w = ui.S(4 + 38 + 38 + 4);
    if (BeginIsland(ui, "##island-undo", at, {w, h})) {
        ImGui::SetCursorScreenPos({at.x + ui.S(4), at.y + ui.S(3)});
        ActionButton(app, "edit.undo", 36, 18);
        ImGui::SetCursorScreenPos({at.x + ui.S(42), at.y + ui.S(3)});
        ActionButton(app, "edit.redo", 36, 18);
    }
    EndIsland();
    return w;
}

bool cubeLogged = false;   ///< the big cube's place written to the log since it opened

void DrawCube(AppContext& app, ImVec2 viewMin, ImVec2 viewMax, ImVec2 at) {
    ui::Ui& ui = app.ui;
    MobileState& m = app.shell.mobile;
    const float b = ui.S(44);
    // Folded: a button (the triad shows the orientation). A tap opens the
    // big cube; a long press goes straight home.
    if (BeginIsland(ui, "##island-cube", at, {b, b})) {
        ImGui::SetCursorScreenPos(at);
        if (ui::IconButton(ui, "##cube", "ui-cube", 44, 24, Tr("Видовой куб"), true, m.cubeOpen)) {
            m.cubeOpen = !m.cubeOpen;
            log::Info("Touch: cube {}", m.cubeOpen ? "opened" : "folded");
            if (m.cubeOpen) cubeLogged = false;
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            HomeView(app);
            m.cubeOpen = false;
        }
    }
    EndIsland();
    if (!m.cubeOpen) return;

    // Open: a big cube for the finger (bigger regions for edges and
    // corners), turned with one finger. As on the desktop: one "home" — the
    // cube's own house icon (the author: no second button, no hints). It
    // folds itself after a choice or a tap elsewhere.
    const float s = std::max(ui.S(120), std::min(ui.S(200), std::min(viewMax.x - viewMin.x, viewMax.y - viewMin.y) - ui.S(70)));
    // Under the button (or over it when the button is at the bottom), inside the view.
    const bool below = at.y + b / 2 < (viewMin.y + viewMax.y) / 2;
    const ImVec2 p{std::clamp(at.x + b - s, viewMin.x + ui.S(8), viewMax.x - s - ui.S(8)), below ? at.y + b + ui.S(6) : at.y - s - ui.S(6)};
    bool inside = false;
    if (!cubeLogged) log::Info("Touch: cube panel at {:.0f},{:.0f} size {:.0f}", p.x, p.y, s);   // the emulator's check taps a face
    cubeLogged = true;
    if (BeginIsland(ui, "##island-cube-big", p, {s, s})) {
        ui::Ui big = ui;
        big.scale = ui.scale * s / ui.S(172);   // the cube is laid out in a 172-unit corner
        const ui::ViewCubeAction cube = ui::DrawViewCube(big, app.viewport.Projection(), p, {p.x + s, p.y + s}, false);
        if (cube.lookFrom) {
            log::Info("Touch: cube view chosen");
            LookFrom(app, cube.lookFrom->first, cube.lookFrom->second);
            m.cubeOpen = false;
        }
        if (cube.home) {
            HomeView(app);
            m.cubeOpen = false;
        }
        if (cube.dragYaw != 0 || cube.dragPitch != 0)
            app.viewport.Orbit(cube.dragYaw, cube.dragPitch, app.viewport.ModelCenter(), app.settings.turntableOrbit);
        inside = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) || ImGui::IsAnyItemActive();
    }
    EndIsland();
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool onButton = mouse.x >= at.x && mouse.y >= at.y && mouse.x < at.x + b && mouse.y < at.y + b;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !inside && !onButton) {
        m.cubeOpen = false;
        log::Info("Touch: cube folded by a tap at {:.0f},{:.0f}", mouse.x, mouse.y);
    }
}

// --- Block 4: the projects ---------------------------------------------------------

void DrawStrip(AppContext& app, ImVec2 pos, ImVec2 size) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    MobileState& m = app.shell.mobile;
    ImDrawList* dl = BeginArea("##strip", pos, size, c.appBackground);
    dl->AddLine(pos, {pos.x + size.x, pos.y}, ui::ToU32(c.divider));
    const float h = size.y;
    ImGui::SetCursorScreenPos({pos.x + ui.S(6), pos.y + (h - ui.S(34)) / 2});
    if (ui::IconButton(ui, "##strip-home", "ui-home", 34, 18, Tr("Главная страница"), true, app.workspace.ShowsHome()))
        app.workspace.ShowHome(true);
    const float x0 = pos.x + ui.S(46);
    dl->AddLine({x0 - ui.S(3), pos.y + ui.S(8)}, {x0 - ui.S(3), pos.y + h - ui.S(8)}, ui::ToU32(c.fieldBorder));

    // Swiped sideways, no scroll bar: a drag that started on it moves it,
    // and a tab is chosen only by a tap.
    static bool dragged = false;
    const bool over = ImGui::IsMouseHoveringRect({x0, pos.y}, {pos.x + size.x, pos.y + h});
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) dragged = false;
    if (over || dragged) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 8.0f) && over) dragged = true;
        if (dragged && ImGui::IsMouseDown(ImGuiMouseButton_Left)) m.stripScroll -= ImGui::GetIO().MouseDelta.x;
        if (over) m.stripScroll -= ImGui::GetIO().MouseWheelH * ui.S(40);
    }
    float content = 0;
    for (const auto& s : app.workspace.sessions)
        content += ui.fonts.body->CalcTextSizeA(ui.S(13), FLT_MAX, 0, s->Title().c_str()).x + ui.S(40);
    m.stripScroll = std::clamp(m.stripScroll, 0.0f, std::max(0.0f, content - (pos.x + size.x - x0)));

    ImGui::PushClipRect({x0, pos.y}, {pos.x + size.x, pos.y + h}, true);
    float x = x0 - m.stripScroll;
    for (std::size_t i = 0; i < app.workspace.sessions.size(); ++i) {
        Session& s = *app.workspace.sessions[i];
        const bool active = static_cast<int>(i) == app.workspace.ActiveIndex() && !app.workspace.ShowsHome();
        const std::string title = s.Title();
        ImFont* font = active ? ui.fonts.medium : ui.fonts.body;
        const float tw = font->CalcTextSizeA(ui.S(13), FLT_MAX, 0, title.c_str()).x;
        const float w = tw + ui.S(s.IsModified() ? 40 : 30);
        ImGui::SetCursorScreenPos({x, pos.y});
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::InvisibleButton("##project", {w, h}) && !dragged) {
            const int index = static_cast<int>(i);
            app.Later([&app, index] { app.workspace.Activate(index); });
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {   // a long press: close it (asks about changes)
            const int index = static_cast<int>(i);
            app.Later([&app, index] { RequestClose(app, index); });
        }
        ImGui::PopID();
        // Every project a pill; the open one tinted (the mockup).
        const ImVec2 a{x + ui.S(2), pos.y + ui.S(5)}, bb{x + w - ui.S(2), pos.y + h - ui.S(5)};
        dl->AddRectFilled(a, bb, ui::ToU32(active ? c.accentSoft : c.panel), ui.S(8));
        dl->AddRect(a, bb, ui::ToU32(active ? c.accentSoftBorder : c.fieldBorder), ui.S(8));
        dl->AddText(font, ui.S(13), {x + ui.S(14), pos.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(active ? c.accent : c.text),
                    title.c_str());
        if (s.IsModified()) dl->AddCircleFilled({x + ui.S(14) + tw + ui.S(10), pos.y + h / 2}, ui.S(3), ui::ToU32(c.warning));
        x += w + ui.S(2);
    }
    ImGui::PopClipRect();
    EndArea();
}

} // namespace

bool WantsPhoneLayout(const Settings& settings, const platform::Window& window) {
#if defined(__ANDROID__)
    if (settings.mobile.layout == 1) return false;
    if (settings.mobile.layout == 2) return true;
    int w = 0, h = 0;
    window.WindowSize(w, h);
    return w > 0 && h > 0 && std::min(w, h) < 600;   // dp: Android's own line between phones and tablets
#else
    // Test hook: BZ_PHONE_LAYOUT=1 shows the phone layout on the desktop (Xvfb
    // screenshots of it, scripts/ui-test); never set for a person.
    (void)settings;
    (void)window;
    static const bool forced = std::getenv("BZ_PHONE_LAYOUT") != nullptr;
    return forced;
#endif
}

void DrawPhoneRoot(AppContext& app) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    MobileState& m = app.shell.mobile;
#if defined(__ANDROID__)
    // The first start on a phone: the sensitivity, then the layout (the author, 17.10.2026).
    // Not in the emulator's test runs (their commands drive the program themselves).
    static bool setupChecked = false;
    if (!setupChecked) {
        setupChecked = true;
        if (!app.settings.mobile.firstStartDone && !m.lastCrash.open && platform::android::TestCommands().empty()) StartSetup(app);
    }
#endif
    const ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::ToVec4(c.appBackground));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // The camera's side gets the dark panel (as wide as the user set, not
    // narrower than its safe zone); the other side only its inset.
    const MobileSettings& ms = app.settings.mobile;
    const float safeW = m.cutoutSide != 0 ? std::max(ui.S(40), m.cutoutSide < 0 ? m.insets.left : m.insets.right) : 0.0f;
    const float cameraW = safeW > 0 ? std::max(safeW, ui.S(static_cast<float>(ms.hotbarWidth))) : 0.0f;
    float x0 = m.cutoutSide < 0 ? cameraW : m.insets.left;
    float x1 = W - (m.cutoutSide > 0 ? cameraW : m.insets.right);
    m.rHotbar = {};
    if (cameraW > 0) {
        const ImVec2 cp{m.cutoutSide < 0 ? 0.0f : W - cameraW, 0};
        DrawCameraPanel(app, cp, {cameraW, H});
        m.rHotbar = {cp, {cp.x + cameraW, H}};
    }
    const bool logoInBrow = cameraW == 0;

    Session* session = app.workspace.Active();
    const float stripH = ui.S(static_cast<float>(ms.stripHeight));
    const float stripBottom = H - m.insets.bottom;
    m.rBlock1 = m.rBlock2 = m.rUndo = m.rTabs = m.rCube = m.rStrip = m.rView = {};
    if ((app.workspace.ShowsHome() || !session) && m.setupStep == 0 && !m.layoutEditor) {
        // The home page: a slim brow with the logo when there is no camera panel.
        float top = m.insets.top;
        if (logoInBrow) {
            ImGui::SetCursorScreenPos({x0 + ui.S(8), top + ui.S(4)});
            const ImVec2 lp = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##logo", ui.S(36, 36))) ImGui::OpenPopup("##main-menu");
            ui.icons.Draw(ImGui::GetWindowDrawList(), "brand-mark", {lp.x + ui.S(7), lp.y + ui.S(7)}, ui.S(22), {1, 1, 1, 1});
            MainMenuPopup(app);
            top += ui.S(44);
        }
        DrawPhoneHome(app, {x0, top}, {x1 - x0, stripBottom - stripH - top});
        DrawStrip(app, {x0, stripBottom - stripH}, {x1 - x0, stripH});
        m.rStrip = {{x0, stripBottom - stripH}, {x1, stripBottom}};
        ImGui::End();
        DrawLayoutEditor(app);
        return;
    }
    if (!session) {
        ImGui::End();
        DrawLayoutEditor(app);
        return;
    }

    // Everything hidden ("Скрыть управление") or the first step of the setup:
    // the view takes the whole width, nothing over it.
    if (m.controlsHidden || m.setupStep == 1) {
        const ImVec2 viewPos{x0, m.insets.top};
        DrawViewport(app, *session, viewPos, {x1 - x0, stripBottom - m.insets.top});
        m.rView = {viewPos, {x1, stripBottom}};
        ImGui::End();
        if (m.controlsHidden && cameraW == 0) {
            // No camera panel to hold the button: a small one in the corner brings it all back.
            const ImVec2 at{x0 + ui.S(8), m.insets.top + ui.S(8)};
            if (BeginIsland(ui, "##island-show", at, ui.S(44, 44))) {
                ImGui::SetCursorScreenPos(at);
                if (ui::IconButton(ui, "##show-controls", "ui-eye", 44, 20, Tr("Показать управление"), true, true)) m.controlsHidden = false;
            }
            EndIsland();
        }
        DrawSetup(app);
        return;
    }

    // The blocks: 20 / 60 / 20 by default; block 1 widens while a value is
    // typed (the numpad's keys must fit a finger), block 2 narrows to icons.
    const float span = x1 - x0;
    float w1 = std::max(ui.S(170), span * static_cast<float>(ms.leftWidth) / 100.0f);
    if (m.numpad) w1 = std::max(w1, ui.S(300));
    float w2 = ms.iconsOnly ? std::max(ui.S(60), span * 0.10f) : std::max(ui.S(150), span * static_cast<float>(ms.rightWidth) / 100.0f);
    w1 = std::min(w1, span * 0.45f);
    w2 = std::min(w2, span * 0.35f);
    const bool lefty = ms.leftHanded;
    const float b1x = lefty ? x1 - w1 : x0;
    const float b2x = lefty ? x0 : x1 - w2;
    const float vx0 = lefty ? x0 + w2 : x0 + w1;
    const float vx1 = lefty ? x1 - w1 : x1 - w2;

    const ImVec2 viewPos{vx0, m.insets.top};
    const ImVec2 viewSize{vx1 - vx0, stripBottom - stripH - m.insets.top};
    m.choices.clear();   // the tool panel (block 1) fills them again this frame
    DrawViewport(app, *session, viewPos, viewSize);
    DrawStrip(app, {vx0, stripBottom - stripH}, {vx1 - vx0, stripH});
    DrawBlock1(app, *session, {b1x, 0}, {w1, H}, !lefty, logoInBrow);
    DrawBlock2(app, *session, {b2x, 0}, {w2, H}, !lefty, ms.iconsOnly);
    m.rView = {viewPos, {vx1, viewPos.y + viewSize.y}};
    m.rStrip = {{vx0, stripBottom - stripH}, {vx1, stripBottom}};
    m.rBlock1 = {{b1x, 0}, {b1x + w1, H}};
    m.rBlock2 = {{b2x, 0}, {b2x + w2, H}};
    ImGui::End();

    // Over the model: undo/redo (5), the tabs (3), the cube — where the user
    // put them (the layout editor): stuck to the top or the bottom edge, at a
    // part of the free length, at a size. The setup's second step puts its
    // own tabs where block 3 is.
    const float viewBottom = viewPos.y + viewSize.y;
    const auto place = [&](const IslandPlace& p, float w, float h, float usual) {
        const float free = std::max(0.0f, viewSize.x - ui.S(16) - w);
        const float along = p.along < 0 ? usual : static_cast<float>(p.along);
        return ImVec2{vx0 + ui.S(8) + free * std::clamp(along, 0.0f, 1.0f), p.bottom ? viewBottom - ui.S(8) - h : viewPos.y + ui.S(8)};
    };
    const float baseScale = ui.scale;
    const auto scaled = [&](const IslandPlace& p) { ui.scale = baseScale * static_cast<float>(p.scale); };
    {
        scaled(ms.undoIsland);
        const float uw = ui.S(84), uh = ui.S(kIsland);
        const ImVec2 at = place(ms.undoIsland, uw, uh, 0.0f);
        DrawUndoIsland(app, at);
        m.rUndo = {at, {at.x + uw, at.y + uh}};
    }
    // The open tool's choices replace the tabs while it is open (from the panel drawn just now,
    // or — when block 1 shows another tab — the ones it drew last).
    std::string owner;
    if (const OperationEditor* op = app.ops.Active()) owner = op->Title();
    else if (session->mode == EditMode::Sketch && app.sketch.Tool() != SketchTool::Select) owner = InfoOf(app.sketch.Tool()).label;
    if (owner.empty()) {
        m.lastChoices.clear();
        m.choicesOwner.clear();
        m.choiceScroll = 0;
    } else if (!m.choices.empty()) {
        if (m.choicesOwner != owner) m.choiceScroll = 0;
        m.lastChoices = m.choices;
        m.choicesOwner = owner;
    }
    const std::vector<MobileState::ChoiceGroup>& shown = m.choices.empty() && owner == m.choicesOwner ? m.lastChoices : m.choices;
    std::vector<const MobileState::ChoiceGroup*> toolGroups, lineGroups;
    if (!owner.empty())
        for (const auto& g : shown) (g.lineType ? lineGroups : toolGroups).push_back(&g);
    if (m.setupStep != 2 && !toolGroups.empty()) {
        scaled(ms.tabsIsland);
        const float x = m.rUndo.max.x + ui.S(6);
        const float right = vx1 - ui.S(8) - ui.S(44) - ui.S(6) - (lineGroups.empty() ? 0.0f : ChoiceIslandWidth(ui, lineGroups) + ui.S(6));
        const float content = ChoiceIslandWidth(ui, toolGroups);
        const float w = std::min(content, std::max(ui.S(80), right - x));
        const ImVec2 at{x, m.rUndo.min.y};
        DrawChoiceIsland(app, "##island-choices", toolGroups, at, w, true);
        m.rTabs = {at, {at.x + w, at.y + ui.S(kIsland)}};
    } else if (m.setupStep != 2) {   // the setup's second step has its own tabs there
        scaled(ms.tabsIsland);
        const float tw = TabsIslandWidth(app, *session, viewSize.x - ui.S(80));
        // Usually right after undo/redo.
        const float free = std::max(1.0f, viewSize.x - ui.S(16) - tw);
        const float usual = ms.tabsIsland.along < 0 && ms.undoIsland.along < 0 && ms.undoIsland.bottom == ms.tabsIsland.bottom
                                ? std::min(1.0f, (m.rUndo.max.x - vx0 - ui.S(2)) / free)
                                : 0.5f;
        const ImVec2 at = place(ms.tabsIsland, tw, ui.S(kIsland), usual);
        DrawTabsIsland(app, *session, at, viewSize.x - ui.S(80));
        m.rTabs = {at, {at.x + tw, at.y + ui.S(kIsland)}};
    }
    {
        scaled(ms.cubeIsland);
        const float cb = ui.S(44);
        const ImVec2 at = place(ms.cubeIsland, cb, cb, 1.0f);
        DrawCube(app, viewPos, {vx1, viewBottom}, at);
        m.rCube = {at, {at.x + cb, at.y + cb}};
        if (!lineGroups.empty()) {   // the sketch's line type: an island of its own, left of the cube
            const float lw = ChoiceIslandWidth(ui, lineGroups);
            DrawChoiceIsland(app, "##island-line", lineGroups, {at.x - ui.S(6) - lw, at.y}, lw, false);
        }
    }
    ui.scale = baseScale;
    DrawToolStrip(app, viewPos, {vx1, viewBottom}, !lefty);
    DrawSetup(app);
    DrawLayoutEditor(app);
}

void DrawLastCrash(AppContext& app) {
    MobileState::LastCrash& c = app.shell.mobile.lastCrash;
    if (!c.open) return;
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& colors = ui.C();
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::OpenPopup("##last-crash");
    const float w = std::min(io.DisplaySize.x - ui.S(40), ui.S(560));
    ImGui::SetNextWindowPos({io.DisplaySize.x / 2, io.DisplaySize.y / 2}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({w, 0});
    ImGui::SetNextWindowSizeConstraints({w, 0}, {w, io.DisplaySize.y - ui.S(30)});
    ui::FontScope font(ui.fonts.body);
    if (!ImGui::BeginPopupModal("##last-crash", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::PushFont(ui.fonts.strong);
    ImGui::TextUnformatted(c.hang ? Tr("В прошлый раз BreezeCAD перестал отвечать") : Tr("В прошлый раз BreezeCAD закрылся из-за ошибки"));
    ImGui::PopFont();
    ImGui::Spacing();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - ui.S(30));
    ImGui::TextWrapped("%s", Tr("Несохранённую работу можно восстановить на главной странице. Код ниже помогает найти причину — "
                                "скопируйте его или поделитесь отчётом с разработчиком. Отчёт никуда не отправляется сам."));
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::PushFont(ui.fonts.mono);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(colors.accent));
    ImGui::TextUnformatted(c.code.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    if (!c.reason.empty()) ImGui::TextDisabled("%s", Trf("Система: {}", c.reason).c_str());
    ImGui::Spacing();
    if (ui::SecondaryButton(ui, "##copy-code", Tr("Скопировать код"), nullptr, ui.S(170, 38))) ImGui::SetClipboardText(c.code.c_str());
    ImGui::SameLine(0, ui.S(8));
#if defined(__ANDROID__)
    if (!c.file.empty() && ui::SecondaryButton(ui, "##share-report", Tr("Поделиться отчётом"), nullptr, ui.S(190, 38)))
        platform::android::Share(PathFromUtf8(c.file), "text/plain", "BreezeCAD " + c.code);
    ImGui::SameLine(0, ui.S(8));
#endif
    if (ui::PrimaryButton(ui, "##close-crash", Tr("Понятно"), nullptr, ui.S(120, 38))) {
        c.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void PhoneFileMenu(AppContext& app) {
#if defined(__ANDROID__)
    ui::Ui& ui = app.ui;
    if (ui::MenuItem(ui, Tr("Папка проектов…"), nullptr, "ui-folder")) platform::android::ChooseProjectsFolder();
    Session* s = app.Doc();
    if (ui::BeginSubmenu(ui, Tr("Поделиться"), "ui-share", s != nullptr)) {
        struct Choice {
            const char* label;
            int         format;   ///< -1 the document itself, else ExportFormat
        };
        const Choice choices[] = {{Tr("Документ BreezeCAD"), -1}, {"STL", static_cast<int>(ExportFormat::Stl)},
                                  {"3MF", static_cast<int>(ExportFormat::ThreeMf)}, {"STEP", static_cast<int>(ExportFormat::Step)}};
        for (const Choice& ch : choices) {
            if (!ui::MenuItem(ui, ch.label)) continue;
            const int format = ch.format;
            app.Later([&app, format] {
                Session* doc = app.Doc();
                if (!doc) return;
                const std::filesystem::path dir = platform::android::CacheDir() / "share";
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                const char* ext = format < 0 ? (doc->assembly ? ".bza" : ".bzc")
                                : format == static_cast<int>(ExportFormat::Stl) ? ".stl"
                                : format == static_cast<int>(ExportFormat::Step) ? ".step" : ".3mf";
                const std::filesystem::path file = dir / PathFromUtf8(doc->Title() + ext);
                bool ok = false;
                std::string error;
                if (format < 0) ok = static_cast<bool>(doc->WriteFile(file, "BreezeCAD"));
                else ok = app.workspace.ExportTo(*doc, static_cast<ExportFormat>(format), file, app.notes, &error);
                if (!ok) return;
                const char* mime = format < 0 ? "application/octet-stream"
                                 : format == static_cast<int>(ExportFormat::Stl) ? "model/stl"
                                 : format == static_cast<int>(ExportFormat::Step) ? "model/step" : "model/3mf";
                platform::android::Share(file, mime, doc->Title());
            });
        }
        ImGui::EndMenu();
    }
#else
    (void)app;
#endif
}

} // namespace bz::app::mobile
