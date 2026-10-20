// The layout editor and the first-start setup of the phone layout
// (docs/dev/mockups 2a–2d and 5a–5b, approved by the author 17.10.2026).
//
// The editor is a full-screen layer over the layout as it is: "Выберите
// элемент управления" over the view until a control is tapped, then what
// can be done with it —
//   blocks 1 and 2   numbers on them, their content see-through; "1 ⇄ 2"
//                    swaps their sides; a pill on each one's edge toward the
//                    view drags its width (the edge lit with a thick line);
//   islands          (undo/redo, the tabs, the cube's button) dragged along
//                    the top or the bottom edge of the view; a slider in the
//                    middle of the view sizes them;
//   the hotbar       a pill on its edge: its width (never under the safe zone);
//   the projects     a pill on its top edge: its height.
// A red cross in the corner closes it. Everything is saved at once
// (MobileSettings), "Сбросить настройки" in the settings puts it back.
//
// The first start (setup) goes: 1 "Рабочий слой" — the example part on the
// whole width, the turn/move/zoom/cursor sliders on the left: turn the part
// and choose; 2 "Навигация" — the usual layout, live, the list and press
// sliders in the middle; then the layout editor. Its tabs stand where the
// tabs island is. The red cross skips the rest.
#include "app/mobile/MobileLayout.h"

#include "app/Operations.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "platform/Paths.h"
#include "ui/Widgets.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>

#if defined(__ANDROID__)
#include "platform/Android.h"
#endif

namespace bz::app::mobile {
namespace {

using Part = MobileState::Part;
using Rect = MobileState::Rect;

ImVec2 Centre(const Rect& r) { return {(r.min.x + r.max.x) / 2, (r.min.y + r.max.y) / 2}; }
bool   Empty(const Rect& r) { return r.max.x <= r.min.x || r.max.y <= r.min.y; }

/// A full-screen window on top of everything, without a background.
bool BeginLayer(const char* id) {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const bool open = ImGui::Begin(id, nullptr,
                                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar(2);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    return open;
}

/// The red cross in the top right corner.
bool CloseButton(ui::Ui& ui, const char* id) {
    const ImVec2 size = ui.S(40, 40);
    const ImVec2 p{ImGui::GetIO().DisplaySize.x - size.x - ui.S(10) - ui.S(0), ui.S(10)};
    ImGui::SetCursorScreenPos(p);
    const bool clicked = ImGui::InvisibleButton(id, size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled({p.x, p.y + ui.S(3)}, {p.x + size.x, p.y + size.y + ui.S(3)}, IM_COL32(0, 0, 0, 90), ui.S(12));
    dl->AddRectFilled(p, {p.x + size.x, p.y + size.y}, ImGui::IsItemActive() ? IM_COL32(200, 50, 55, 255) : IM_COL32(229, 72, 77, 255),
                      ui.S(12));
    ui.icons.Draw(dl, "ui-close", {p.x + (size.x - ui.S(20)) / 2, p.y + (size.y - ui.S(20)) / 2}, ui.S(20), {1, 1, 1, 1});
    return clicked;
}

/// A small blue label (what is chosen, its value).
void Tag(ui::Ui& ui, ImVec2 centre, const std::string& text) {
    const ui::ThemeColors& c = ui.C();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 ts = ui.fonts.bodySmall->CalcTextSizeA(ui.S(11.5f), FLT_MAX, 0, text.c_str());
    const ImVec2 a{centre.x - ts.x / 2 - ui.S(9), centre.y - ts.y / 2 - ui.S(4)}, b{centre.x + ts.x / 2 + ui.S(9), centre.y + ts.y / 2 + ui.S(4)};
    dl->AddRectFilled(a, b, ui::ToU32(c.accentSoft), ui.S(7));
    dl->AddRect(a, b, ui::ToU32(c.accentSoftBorder), ui.S(7));
    dl->AddText(ui.fonts.bodySmall, ui.S(11.5f), {a.x + ui.S(9), a.y + ui.S(4)}, ui::ToU32(c.selection), text.c_str());
}

/// The thick lit edge that moves.
void Edge(ui::Ui& ui, ImVec2 a, ImVec2 b, float alpha = 1.0f) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 glow = ui::ToU32(ui.C().selection.WithAlpha(0.35f * alpha));
    const ImU32 line = ui::ToU32(ui.C().selection.WithAlpha(alpha));
    dl->AddLine(a, b, glow, ui.S(9));
    dl->AddLine(a, b, line, ui.S(4));
}

/// The pill ‹ | | › (or ˄ = ˅ when `vertical`) on an edge; returns the drag this frame.
ImVec2 Pill(ui::Ui& ui, const char* id, ImVec2 centre, bool vertical, float alpha = 1.0f) {
    const ImVec2 size = vertical ? ui.S(34, 84) : ui.S(84, 34);
    const ImVec2 a{centre.x - size.x / 2, centre.y - size.y / 2};
    ImGui::SetCursorScreenPos(a);
    ImGui::InvisibleButton(id, size);
    const bool active = ImGui::IsItemActive();
    const ui::ThemeColors& c = ui.C();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b{a.x + size.x, a.y + size.y};
    const float r = std::min(size.x, size.y) / 2;
    dl->AddRectFilled({a.x, a.y + ui.S(3)}, {b.x, b.y + ui.S(3)}, IM_COL32(0, 0, 0, static_cast<int>(110 * alpha)), r);
    dl->AddRectFilled(a, b, ui::ToU32((active ? c.accentSoft : c.panel).WithAlpha(alpha)), r);
    dl->AddRect(a, b, ui::ToU32(c.selection.WithAlpha(alpha)), r, 0, ui.S(2));
    const ImU32 ink = ui::ToU32(c.selection.WithAlpha(alpha));
    if (vertical) {
        // Chevrons drawn as lines: ˄ ˅ are not in IBM Plex (they came out as "?").
        const float w = ui.S(4), t = a.y + ui.S(13), u = b.y - ui.S(13);
        const ImVec2 up[] = {{centre.x - w, t + w / 2}, {centre.x, t - w / 2}, {centre.x + w, t + w / 2}};
        const ImVec2 down[] = {{centre.x - w, u - w / 2}, {centre.x, u + w / 2}, {centre.x + w, u - w / 2}};
        dl->AddPolyline(up, 3, ink, ImDrawFlags_None, ui.S(1.6f));
        dl->AddPolyline(down, 3, ink, ImDrawFlags_None, ui.S(1.6f));
        dl->AddLine({centre.x - ui.S(7), centre.y - ui.S(4)}, {centre.x + ui.S(7), centre.y - ui.S(4)}, ink, ui.S(2));
        dl->AddLine({centre.x - ui.S(7), centre.y + ui.S(4)}, {centre.x + ui.S(7), centre.y + ui.S(4)}, ink, ui.S(2));
    } else {
        dl->AddText(ui.fonts.medium, ui.S(14), {a.x + ui.S(10), centre.y - ui.S(9)}, ink, "\xE2\x80\xB9");   // ‹
        dl->AddText(ui.fonts.medium, ui.S(14), {b.x - ui.S(16), centre.y - ui.S(9)}, ink, "\xE2\x80\xBA");   // ›
        dl->AddLine({centre.x - ui.S(4), centre.y - ui.S(7)}, {centre.x - ui.S(4), centre.y + ui.S(7)}, ink, ui.S(2));
        dl->AddLine({centre.x + ui.S(4), centre.y - ui.S(7)}, {centre.x + ui.S(4), centre.y + ui.S(7)}, ink, ui.S(2));
    }
    return active ? ImGui::GetIO().MouseDelta : ImVec2{0, 0};
}

/// A see-through veil over a control that is not the one being changed.
void Veil(const Rect& r, float alpha) {
    if (Empty(r)) return;
    ImGui::GetWindowDrawList()->AddRectFilled(r.min, r.max, IM_COL32(18, 20, 24, static_cast<int>(255 * alpha)));
}

void Number(ui::Ui& ui, ImVec2 c, const char* text, float alpha) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = ui.S(23);
    dl->AddCircleFilled(c, r + ui.S(4), ui::ToU32(ui.C().accent.WithAlpha(0.25f * alpha)));
    dl->AddCircleFilled(c, r, ui::ToU32(ui.C().accent.WithAlpha(alpha)));
    const ImVec2 ts = ui.fonts.display->CalcTextSizeA(ui.S(22), FLT_MAX, 0, text);
    dl->AddText(ui.fonts.display, ui.S(22), {c.x - ts.x / 2, c.y - ts.y / 2}, IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)), text);
}

/// A slider in a rounded bar: label · track · value. Returns true when changed.
bool BarSlider(ui::Ui& ui, const char* id, const char* label, double& value, double lo, double hi, const std::string& shown, ImVec2 at,
               float width) {
    const ui::ThemeColors& c = ui.C();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = ui.S(36);
    dl->AddRectFilled(at, {at.x + width, at.y + h}, ui::ToU32(c.panel.WithAlpha(0.96f)), h / 2);
    dl->AddRect(at, {at.x + width, at.y + h}, ui::ToU32(c.fieldBorder), h / 2);
    const float lw = ui.fonts.body->CalcTextSizeA(ui.S(12), FLT_MAX, 0, label).x;
    const float vw = ui.fonts.body->CalcTextSizeA(ui.S(12), FLT_MAX, 0, shown.c_str()).x;
    dl->AddText(ui.fonts.body, ui.S(12), {at.x + ui.S(16), at.y + (h - ui.S(13)) / 2}, ui::ToU32(c.textSecondary), label);
    dl->AddText(ui.fonts.body, ui.S(12), {at.x + width - ui.S(16) - vw, at.y + (h - ui.S(13)) / 2}, ui::ToU32(c.text), shown.c_str());
    const float sx = at.x + ui.S(28) + lw, sw = width - (sx - at.x) - vw - ui.S(30);
    ImGui::SetCursorScreenPos({sx, at.y + (h - ui.S(28)) / 2});
    float t = static_cast<float>((value - lo) / (hi - lo));
    if (!ui::Slider(ui, id, t, std::max(ui.S(40), sw))) return false;
    value = lo + static_cast<double>(t) * (hi - lo);
    return true;
}

IslandPlace* PlaceOf(MobileSettings& s, Part p) {
    switch (p) {
        case Part::Undo: return &s.undoIsland;
        case Part::Tabs: return &s.tabsIsland;
        case Part::Cube: return &s.cubeIsland;
        default: return nullptr;
    }
}

const char* NameOf(Part p) {
    switch (p) {
        case Part::Block1: return Tr("Блок 1 · дерево");
        case Part::Block2: return Tr("Блок 2 · инструменты");
        case Part::Undo:   return Tr("Отменить / Повторить");
        case Part::Tabs:   return Tr("Вкладки");
        case Part::Cube:   return Tr("Видовой куб");
        case Part::Hotbar: return Tr("Хотбар");
        case Part::Strip:  return Tr("Полоса проектов");
        case Part::None:   break;
    }
    return "";
}

/// Opens the example part for the setup or the editor when nothing is open (Android).
void EnsureDocument(AppContext& app) {
    MobileState& m = app.shell.mobile;
    if (app.workspace.Active() && !app.workspace.ShowsHome()) return;
#if defined(__ANDROID__)
    const std::filesystem::path example = platform::AssetsDir() / "examples" / "bracket.bzc";
    std::error_code ec;
    if (std::filesystem::exists(example, ec)) {
        OpenDocument(app, example);
        m.setupOpenedDoc = true;
        app.workspace.ShowHome(false);
        return;
    }
#endif
    if (!app.workspace.sessions.empty()) app.workspace.ShowHome(false);
}

/// The end of the setup or the editor: the example closed again if it was opened for it.
void Finish(AppContext& app) {
    MobileState& m = app.shell.mobile;
    const bool setup = m.setupStep != 0;
    m.layoutEditor = false;
    m.editing = Part::None;
    m.setupStep = 0;
    if (setup) app.settings.mobile.firstStartDone = true;
    if (m.setupOpenedDoc) {
        m.setupOpenedDoc = false;
        app.Later([&app] {
            const int i = app.workspace.ActiveIndex();
            if (i >= 0 && !app.workspace.sessions[static_cast<std::size_t>(i)]->IsModified()) app.workspace.Close(i);
            app.workspace.ShowHome(true);
        });
    }
    log::Info("Layout: {} closed", setup ? "the first-start setup" : "the layout editor");
}

// --- The editor --------------------------------------------------------------------

void EditBlocks(AppContext& app, float span) {
    ui::Ui& ui = app.ui;
    MobileState& m = app.shell.mobile;
    MobileSettings& s = app.settings.mobile;
    const bool lefty = s.leftHanded;
    // The content see-through, the numbers on it.
    Veil(m.rBlock1, 0.45f);
    Veil(m.rBlock2, 0.45f);
    Veil(m.rStrip, 0.6f);
    Number(ui, Centre(m.rBlock1), "1", m.editing == Part::Block1 ? 1.0f : 0.8f);
    Number(ui, Centre(m.rBlock2), "2", m.editing == Part::Block2 ? 1.0f : 0.8f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Rect& sel = m.editing == Part::Block1 ? m.rBlock1 : m.rBlock2;
    dl->AddRect(sel.min, sel.max, ui::ToU32(ui.C().selection), 0, 0, ui.S(2));
    // Each block's edge toward the view, with its pill (the chosen one bright).
    const auto edgeOf = [&](const Rect& r, bool viewOnRight) { return viewOnRight ? r.max.x - ui.S(1) : r.min.x + ui.S(1); };
    const bool b1ViewRight = !lefty, b2ViewRight = lefty;
    const float e1 = edgeOf(m.rBlock1, b1ViewRight), e2 = edgeOf(m.rBlock2, b2ViewRight);
    const float midY = (m.rBlock1.min.y + m.rBlock1.max.y) / 2;
    Edge(ui, {e1, m.rBlock1.min.y}, {e1, m.rBlock1.max.y}, m.editing == Part::Block1 ? 1.0f : 0.45f);
    Edge(ui, {e2, m.rBlock2.min.y}, {e2, m.rBlock2.max.y}, m.editing == Part::Block2 ? 1.0f : 0.45f);
    const float toPercent = 100.0f / std::max(1.0f, span);
    if (const ImVec2 d = Pill(ui, "##pill-1", {e1, midY}, false, m.editing == Part::Block1 ? 1.0f : 0.55f); d.x != 0) {
        m.editing = Part::Block1;
        static float acc1 = 0;
        acc1 += (b1ViewRight ? d.x : -d.x) * toPercent;
        const int step = static_cast<int>(acc1);
        if (step != 0) {
            s.leftWidth = std::clamp(s.leftWidth + step, 12, 40);
            acc1 -= static_cast<float>(step);
        }
    }
    if (const ImVec2 d = Pill(ui, "##pill-2", {e2, midY}, false, m.editing == Part::Block2 ? 1.0f : 0.55f); d.x != 0) {
        m.editing = Part::Block2;
        static float acc2 = 0;
        acc2 += (b2ViewRight ? d.x : -d.x) * toPercent;
        const int step = static_cast<int>(acc2);
        if (step != 0) {
            s.rightWidth = std::clamp(s.rightWidth + step, 8, 40);
            s.iconsOnly = false;
            acc2 -= static_cast<float>(step);
        }
    }
    // "1 ⇄ 2" in the middle of the view.
    const ImVec2 vc = Centre(m.rView);
    const ImVec2 size = ui.S(128, 42);
    const ImVec2 a{vc.x - size.x / 2, vc.y - size.y / 2 - ui.S(24)};
    ImGui::SetCursorScreenPos(a);
    if (ImGui::InvisibleButton("##swap-blocks", size)) s.leftHanded = !s.leftHanded;
    const ui::ThemeColors& c = ui.C();
    dl->AddRectFilled({a.x, a.y + ui.S(3)}, {a.x + size.x, a.y + size.y + ui.S(3)}, IM_COL32(0, 0, 0, 100), size.y / 2);
    dl->AddRectFilled(a, {a.x + size.x, a.y + size.y}, ui::ToU32(ImGui::IsItemActive() ? c.accentHover : c.accent), size.y / 2);
    const char* swap = "1  \xE2\x87\x84  2";   // 1 ⇄ 2
    const ImVec2 ts = ui.fonts.strong->CalcTextSizeA(ui.S(16), FLT_MAX, 0, swap);
    dl->AddText(ui.fonts.strong, ui.S(16), {a.x + (size.x - ts.x) / 2, a.y + (size.y - ts.y) / 2}, ui::ToU32(c.textOnAccent), swap);
    const int width = m.editing == Part::Block1 ? s.leftWidth : s.rightWidth;
    Tag(ui, {vc.x, a.y + size.y + ui.S(22)},
        Trf("{} · ширина {} %", NameOf(m.editing), m.editing == Part::Block2 && s.iconsOnly ? 10 : width));
}

void EditIsland(AppContext& app) {
    ui::Ui& ui = app.ui;
    MobileState& m = app.shell.mobile;
    IslandPlace* place = PlaceOf(app.settings.mobile, m.editing);
    if (!place) return;
    const Rect& r = m.editing == Part::Undo ? m.rUndo : m.editing == Part::Tabs ? m.rTabs : m.rCube;
    const Rect& v = m.rView;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ui::ThemeColors& c = ui.C();
    Veil(m.rBlock1, 0.55f);
    Veil(m.rBlock2, 0.55f);
    Veil(m.rStrip, 0.55f);
    // Where it can go: the top and the bottom edge of the view, dashed.
    const float h = r.max.y - r.min.y;
    for (const float y : {v.min.y + ui.S(4), v.max.y - ui.S(4) - h - ui.S(8)}) {
        const ImVec2 a{v.min.x + ui.S(4), y}, b{v.max.x - ui.S(4), y + h + ui.S(8)};
        for (float x = a.x; x < b.x; x += ui.S(10)) {
            dl->AddLine({x, a.y}, {std::min(x + ui.S(5), b.x), a.y}, ui::ToU32(c.selection.WithAlpha(0.55f)), ui.S(1.5f));
            dl->AddLine({x, b.y}, {std::min(x + ui.S(5), b.x), b.y}, ui::ToU32(c.selection.WithAlpha(0.55f)), ui.S(1.5f));
        }
    }
    // The island itself: drag it.
    ImGui::SetCursorScreenPos(r.min);
    ImGui::InvisibleButton("##drag-island", {std::max(1.0f, r.max.x - r.min.x), std::max(1.0f, h)});
    if (ImGui::IsItemActive()) {
        const ImVec2 mouse = ImGui::GetMousePos();
        const float w = r.max.x - r.min.x;
        const float free = std::max(1.0f, (v.max.x - v.min.x) - ui.S(16) - w);
        place->along = std::clamp((mouse.x - w / 2 - v.min.x - ui.S(8)) / free, 0.0f, 1.0f);
        place->bottom = mouse.y > (v.min.y + v.max.y) / 2;
    }
    dl->AddRect({r.min.x - ui.S(4), r.min.y - ui.S(4)}, {r.max.x + ui.S(4), r.max.y + ui.S(4)}, ui::ToU32(c.selection), ui.S(14), 0, ui.S(2.5f));
    const float tagY = place->bottom ? r.min.y - ui.S(22) : r.max.y + ui.S(22);
    Tag(ui, {std::clamp((r.min.x + r.max.x) / 2, v.min.x + ui.S(90), v.max.x - ui.S(90)), tagY},
        Tr("\xE2\x86\x90 тяни вдоль края \xE2\x86\x92"));   // ← … → (◀ ▶ are not in IBM Plex)
    // Its size: a slider in the middle of the view.
    const float sw = std::min(ui.S(330), v.max.x - v.min.x - ui.S(40));
    const ImVec2 at{(v.min.x + v.max.x - sw) / 2, (v.min.y + v.max.y) / 2 - ui.S(18)};
    BarSlider(ui, "##island-size", Tr("Размер"), place->scale, 0.8, 1.4, std::format("{:.0f} %", place->scale * 100.0), at, sw);
    place->scale = std::round(place->scale * 20.0) / 20.0;
    Tag(ui, {(v.min.x + v.max.x) / 2, at.y + ui.S(58)},
        Trf("{} · {}", NameOf(m.editing), place->bottom ? Tr("нижний край") : Tr("верхний край")));
}

void EditHotbar(AppContext& app) {
    ui::Ui& ui = app.ui;
    MobileState& m = app.shell.mobile;
    MobileSettings& s = app.settings.mobile;
    Veil(m.rBlock1, 0.55f);
    Veil(m.rBlock2, 0.55f);
    Veil(m.rStrip, 0.55f);
    const bool leftSide = m.cutoutSide < 0;
    const float edge = leftSide ? m.rHotbar.max.x - ui.S(1) : m.rHotbar.min.x + ui.S(1);
    Edge(ui, {edge, m.rHotbar.min.y}, {edge, m.rHotbar.max.y});
    const ImVec2 d = Pill(ui, "##pill-hotbar", {edge, m.rHotbar.max.y * 0.72f}, false);
    const float safe = std::max(ui.S(40), leftSide ? m.insets.left : m.insets.right);
    const float now = m.rHotbar.max.x - m.rHotbar.min.x;
    if (d.x != 0) {
        const float want = std::clamp(now + (leftSide ? d.x : -d.x), safe, ui.S(120));
        s.hotbarWidth = static_cast<int>(std::lround(want / ui.scale));
    }
    const ImVec2 vc = Centre(m.rView);
    Tag(ui, {vc.x, m.rView.min.y + ui.S(40)},
        Trf("Хотбар · {:.0f} dp (не уже безопасной зоны {:.0f})", now / ui.scale, safe / ui.scale));
}

void EditStrip(AppContext& app) {
    ui::Ui& ui = app.ui;
    MobileState& m = app.shell.mobile;
    MobileSettings& s = app.settings.mobile;
    Veil(m.rBlock1, 0.55f);
    Veil(m.rBlock2, 0.55f);
    const float y = m.rStrip.min.y + ui.S(1);
    Edge(ui, {m.rStrip.min.x, y}, {m.rStrip.max.x, y});
    const ImVec2 d = Pill(ui, "##pill-strip", {(m.rStrip.min.x + m.rStrip.max.x) / 2, y}, true);
    if (d.y != 0) {
        static float acc = 0;
        acc -= d.y / ui.scale;
        const int step = static_cast<int>(acc);
        if (step != 0) {
            s.stripHeight = std::clamp(s.stripHeight + step, 32, 80);
            acc -= static_cast<float>(step);
        }
    }
    Tag(ui, {(m.rStrip.min.x + m.rStrip.max.x) / 2 + ui.S(130), y - ui.S(26)}, Trf("Полоса проектов · {} dp", s.stripHeight));
}

/// What a tap at `p` chose: islands first (they lie over the view), then the rest.
Part HitTest(const MobileState& m, ImVec2 p) {
    if (m.rUndo.Contains(p)) return Part::Undo;
    if (m.rTabs.Contains(p)) return Part::Tabs;
    if (m.rCube.Contains(p)) return Part::Cube;
    if (m.rBlock1.Contains(p)) return Part::Block1;
    if (m.rBlock2.Contains(p)) return Part::Block2;
    if (m.rHotbar.Contains(p)) return Part::Hotbar;
    if (m.rStrip.Contains(p)) return Part::Strip;
    return Part::None;
}

// --- The setup ---------------------------------------------------------------------

float SetupTabsWidth(ui::Ui& ui) {
    float w = ui.S(8);
    for (const char* n : {Tr("Рабочий слой"), Tr("Навигация")}) w += ui.fonts.medium->CalcTextSizeA(ui.S(13), FLT_MAX, 0, n).x + ui.S(28);
    return w;
}

/// "Рабочий слой | Навигация" where the tabs island is.
void SetupTabs(AppContext& app, ImVec2 centreTop) {
    ui::Ui& ui = app.ui;
    MobileState& m = app.shell.mobile;
    const ui::ThemeColors& c = ui.C();
    const char* names[] = {Tr("Рабочий слой"), Tr("Навигация")};
    const float w = SetupTabsWidth(ui);
    const float h = ui.S(42);
    const ImVec2 at{centreTop.x - w / 2, centreTop.y};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(at, {at.x + w, at.y + h}, ui::ToU32(c.panel.WithAlpha(0.96f)), ui.S(12));
    dl->AddRect(at, {at.x + w, at.y + h}, ui::ToU32(c.divider), ui.S(12));
    float x = at.x + ui.S(4);
    for (int i = 0; i < 2; ++i) {
        const float tw = ui.fonts.medium->CalcTextSizeA(ui.S(13), FLT_MAX, 0, names[i]).x + ui.S(28);
        const bool on = m.setupStep == i + 1;
        ImGui::SetCursorScreenPos({x, at.y});
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##setup-tab", {tw, h})) m.setupStep = i + 1;
        ImGui::PopID();
        if (on) dl->AddRectFilled({x, at.y + ui.S(5)}, {x + tw, at.y + h - ui.S(5)}, ui::ToU32(c.accentSoft), ui.S(8));
        dl->AddText(on ? ui.fonts.strong : ui.fonts.medium, ui.S(13), {x + ui.S(14), at.y + (h - ui.S(13)) / 2 - ui.S(1)},
                    ui::ToU32(on ? c.accent : c.textSecondary), names[i]);
        x += tw;
    }
}

/// "Шаг k из 3" with three dashes.
void Steps(ui::Ui& ui, ImVec2 at, int step) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ui::ThemeColors& c = ui.C();
    for (int i = 0; i < 3; ++i) {
        const float x = at.x + static_cast<float>(i) * ui.S(24);
        dl->AddRectFilled({x, at.y + ui.S(6)}, {x + ui.S(18), at.y + ui.S(10)}, ui::ToU32(i < step ? c.accent : c.fieldBorder), ui.S(2));
    }
    dl->AddText(ui.fonts.caption, ui.S(11), {at.x + ui.S(76), at.y}, ui::ToU32(c.textMuted), Trf("Шаг {} из 3", step).c_str());
}

/// A labelled slider in a column: "Вращение … 150 %" over a track.
void ColumnSlider(ui::Ui& ui, const char* id, const char* label, double& value, double lo, double hi, const std::string& shown, float x,
                  float& y, float width) {
    const ui::ThemeColors& c = ui.C();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddText(ui.fonts.body, ui.S(12), {x, y}, ui::ToU32(c.textSecondary), label);
    const float vw = ui.fonts.strong->CalcTextSizeA(ui.S(12), FLT_MAX, 0, shown.c_str()).x;
    dl->AddText(ui.fonts.strong, ui.S(12), {x + width - vw, y}, ui::ToU32(c.text), shown.c_str());
    ImGui::SetCursorScreenPos({x - ui.S(7), y + ui.S(14)});
    float t = static_cast<float>((value - lo) / (hi - lo));
    if (ui::Slider(ui, id, t, width + ui.S(14))) value = lo + static_cast<double>(t) * (hi - lo);
    y += ui.S(50);
}

/// The big blue "next" button.
bool NextButton(ui::Ui& ui, const char* id, const char* label, ImVec2 at) {
    const ui::ThemeColors& c = ui.C();
    const ImVec2 ts = ui.fonts.strong->CalcTextSizeA(ui.S(14), FLT_MAX, 0, label);
    const ImVec2 size{ts.x + ui.S(54), ui.S(42)};
    const ImVec2 a{at.x - size.x, at.y - size.y};   // `at` is its bottom-right corner
    ImGui::SetCursorScreenPos(a);
    const bool clicked = ImGui::InvisibleButton(id, size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled({a.x, a.y + ui.S(3)}, {a.x + size.x, a.y + size.y + ui.S(3)}, IM_COL32(0, 0, 0, 100), ui.S(12));
    dl->AddRectFilled(a, {a.x + size.x, a.y + size.y}, ui::ToU32(ImGui::IsItemActive() ? c.accentHover : c.accent), ui.S(12));
    dl->AddText(ui.fonts.strong, ui.S(14), {a.x + ui.S(18), a.y + (size.y - ts.y) / 2}, ui::ToU32(c.textOnAccent), label);
    ui.icons.Draw(dl, "ui-chevron-right", {a.x + size.x - ui.S(30), a.y + (size.y - ui.S(14)) / 2}, ui.S(14), c.textOnAccent);
    return clicked;
}

/// A panel of the setup (its own window: touches on it are not the view's).
bool BeginPanel(ui::Ui& ui, const char* id, ImVec2 pos, ImVec2 size, bool clear = false) {
    const ui::ThemeColors& c = ui.C();
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ui.S(14));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::ToVec4(c.panel.WithAlpha(0.97f)));
    ImGui::PushStyleColor(ImGuiCol_Border, ui::ToVec4(c.fieldBorder));
    const bool open = ImGui::Begin(id, nullptr,
                                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse |
                                       (clear ? ImGuiWindowFlags_NoBackground : 0));
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    return open;
}

} // namespace

void OpenLayoutEditor(AppContext& app) {
    MobileState& m = app.shell.mobile;
    app.shell.showSettings = false;
    m.controlsHidden = false;
    m.flyGroup.clear();
    m.cubeOpen = false;
    EnsureDocument(app);
    m.layoutEditor = true;
    m.editing = Part::None;
    log::Info("Layout: the layout editor opened");
}

void StartSetup(AppContext& app) {
    MobileState& m = app.shell.mobile;
    m.controlsHidden = false;
    EnsureDocument(app);
    m.setupStep = 1;
    log::Info("Layout: the first-start setup began");
}

void DrawLayoutEditor(AppContext& app) {
    MobileState& m = app.shell.mobile;
    if (!m.layoutEditor) return;
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    if (BeginLayer("##layout-editor")) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // The view's place: not the model, the editor's own ground.
        if (!Empty(m.rView)) {
            dl->AddRectFilled(m.rView.min, m.rView.max, ui::ToU32(c.appBackground.WithAlpha(0.93f)));
            for (float x = m.rView.min.x - (m.rView.max.y - m.rView.min.y); x < m.rView.max.x; x += ui.S(20))
                dl->AddLine({std::max(x, m.rView.min.x), m.rView.max.y - std::max(0.0f, m.rView.min.x - x)},
                            {std::min(x + (m.rView.max.y - m.rView.min.y), m.rView.max.x),
                             m.rView.min.y + std::max(0.0f, x + (m.rView.max.y - m.rView.min.y) - m.rView.max.x)},
                            ui::ToU32(c.divider.WithAlpha(0.5f)), ui.S(1));
        }
        const float span = (m.rBlock1.max.x - m.rBlock1.min.x) + (m.rBlock2.max.x - m.rBlock2.min.x) + (m.rView.max.x - m.rView.min.x);
        switch (m.editing) {
            case Part::Block1:
            case Part::Block2: EditBlocks(app, span); break;
            case Part::Undo:
            case Part::Tabs:
            case Part::Cube: EditIsland(app); break;
            case Part::Hotbar: EditHotbar(app); break;
            case Part::Strip: EditStrip(app); break;
            case Part::None: {
                const ImVec2 vc = Centre(m.rView);
                const char* title = Tr("Выберите элемент управления");
                const char* hint = Tr("Тап по блоку, островку, хотбару или полосе проектов — появится то, что с ним можно сделать");
                const ImVec2 ts = ui.fonts.strong->CalcTextSizeA(ui.S(15), FLT_MAX, 0, title);
                dl->AddText(ui.fonts.strong, ui.S(15), {vc.x - ts.x / 2, vc.y - ui.S(28)}, ui::ToU32(c.textSecondary), title);
                const float wrap = std::min(ui.S(330), m.rView.max.x - m.rView.min.x - ui.S(30));
                const ImVec2 hs = ui.fonts.body->CalcTextSizeA(ui.S(12), FLT_MAX, wrap, hint);
                dl->AddText(ui.fonts.body, ui.S(12), {vc.x - hs.x / 2, vc.y}, ui::ToU32(c.textMuted), hint, nullptr, wrap);
                break;
            }
        }
        const bool closed = CloseButton(ui, "##editor-close");
        // A tap on nothing of the above chooses what is under it (or nothing).
        if (!closed && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) {
            const Part hit = HitTest(m, ImGui::GetMousePos());
            m.editing = hit;
            if (hit != Part::None) log::Info("Layout: editing {}", NameOf(hit));
        }
        if (closed) Finish(app);
    }
    ImGui::End();
}

void DrawSetup(AppContext& app) {
    MobileState& m = app.shell.mobile;
    if (m.setupStep != 1 && m.setupStep != 2) return;
    if (m.layoutEditor) return;
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    MobileSettings& s = app.settings.mobile;
    const ImGuiIO& io = ImGui::GetIO();
    const Rect v = m.rView;

    // The layer for the tabs, the cross and "next" (no background: the view takes the rest).
    // It is a window as big as the screen, so it is made only as tall as its row of tabs
    // and the corners' buttons are separate panels below.
    bool close = false, next = false;
    if (m.setupStep == 1) {
        // Step 1: the sliders on the left, the part on the whole width.
        const ImVec2 pp{v.min.x + ui.S(8), v.min.y + ui.S(8)}, ps{ui.S(206), v.max.y - v.min.y - ui.S(16)};
        if (BeginPanel(ui, "##setup-work", pp, ps)) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float x = pp.x + ui.S(14), w = ps.x - ui.S(28);
            float y = pp.y + ui.S(12);
            dl->AddText(ui.fonts.title, ui.S(14), {x, y}, ui::ToU32(c.text), Tr("Рабочий слой"));
            Steps(ui, {x, y + ui.S(20)}, 1);
            y += ui.S(44);
            ColumnSlider(ui, "##s-orbit", Tr("Вращение"), s.fingerOrbit, 0.25, 4.0, std::format("{:.0f} %", s.fingerOrbit * 100), x, y, w);
            ColumnSlider(ui, "##s-pan", Tr("Сдвиг"), s.fingerPan, 0.25, 3.0, std::format("{:.0f} %", s.fingerPan * 100), x, y, w);
            ColumnSlider(ui, "##s-zoom", Tr("Масштаб (щипок)"), s.fingerZoom, 0.25, 3.0, std::format("{:.0f} %", s.fingerZoom * 100), x, y, w);
            ColumnSlider(ui, "##s-cursor", Tr("Скорость курсора"), s.cursorSpeed, 0.1, 1.0, std::format("{:.0f} %", s.cursorSpeed * 100), x, y, w);
            double reach = s.cursorReach;
            ColumnSlider(ui, "##s-reach", Tr("Кончик над пальцем"), reach, 32, 80, std::format("{} dp", s.cursorReach), x, y, w);
            s.cursorReach = static_cast<int>(std::lround(reach));
            s.fingerOrbit = std::round(s.fingerOrbit * 20) / 20;
            s.fingerPan = std::round(s.fingerPan * 20) / 20;
            s.fingerZoom = std::round(s.fingerZoom * 20) / 20;
            s.cursorSpeed = std::round(s.cursorSpeed * 20) / 20;
        }
        ImGui::End();
    } else {
        // Step 2: the sliders in the middle, the blocks live around.
        const float pw = std::min(ui.S(330), v.max.x - v.min.x - ui.S(20));
        const ImVec2 pp{(v.min.x + v.max.x - pw) / 2, v.min.y + ui.S(62)};
        const float ph = std::min(ui.S(246), v.max.y - pp.y - ui.S(60));
        if (BeginPanel(ui, "##setup-nav", pp, {pw, ph})) {
            // Many sliders: the panel scrolls (a child list).
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float x = pp.x + ui.S(16), w = pw - ui.S(32);
            dl->AddText(ui.fonts.title, ui.S(14), {x, pp.y + ui.S(12)}, ui::ToU32(c.text), Tr("Навигация"));
            Steps(ui, {x + w - ui.S(130), pp.y + ui.S(14)}, 2);
            ImGui::SetCursorScreenPos({pp.x, pp.y + ui.S(40)});
            ImGui::BeginChild("##setup-nav-list", {pw, ph - ui.S(44)}, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
            float y = ImGui::GetCursorScreenPos().y;
            ColumnSlider(ui, "##s-scroll", Tr("Прокрутка списков"), s.scrollSpeed, 0.25, 3.0, std::format("{:.0f} %", s.scrollSpeed * 100), x,
                         y, w);
            ColumnSlider(ui, "##s-press", Tr("Долгое нажатие"), s.longPress, 0.2, 1.5, std::format("{:.1f} с", s.longPress), x, y, w);
            double slop = s.tapSlop;
            ColumnSlider(ui, "##s-slop", Tr("Порог касания"), slop, 4, 30, std::format("{} dp", s.tapSlop), x, y, w);
            s.tapSlop = static_cast<int>(std::lround(slop));
            s.scrollSpeed = std::round(s.scrollSpeed * 20) / 20;
            s.longPress = std::round(s.longPress * 20) / 20;
            ImGui::SetCursorScreenPos({x, y});
            ImGui::Dummy({w, ui.S(4)});
            ImGui::EndChild();
        }
        ImGui::End();
    }
    // The tabs, the cross and "next": small panels of their own.
    const float tabsW = SetupTabsWidth(ui);
    if (BeginPanel(ui, "##setup-tabs", {(v.min.x + v.max.x - tabsW) / 2, v.min.y + ui.S(8)}, {tabsW, ui.S(42)}, true)) {
        SetupTabs(app, {(v.min.x + v.max.x) / 2, v.min.y + ui.S(8)});
    }
    ImGui::End();
    {
        const ImVec2 cs = ui.S(40, 40);
        if (BeginPanel(ui, "##setup-close", {io.DisplaySize.x - cs.x - ui.S(10), ui.S(10)}, cs, true)) close = CloseButton(ui, "##setup-x");
        ImGui::End();
    }
    {
        const char* label = m.setupStep == 1 ? Tr("Дальше: навигация") : Tr("Дальше: раскладка");
        const float bw = ui.fonts.strong->CalcTextSizeA(ui.S(14), FLT_MAX, 0, label).x + ui.S(54);
        const ImVec2 corner = m.setupStep == 1 ? ImVec2{v.max.x - ui.S(12), v.max.y - ui.S(12)}
                                               : ImVec2{(v.min.x + v.max.x + bw) / 2, v.max.y - ui.S(12)};
        if (BeginPanel(ui, "##setup-next", {corner.x - bw, corner.y - ui.S(42)}, {bw, ui.S(42)}, true)) next = NextButton(ui, "##next", label, corner);
        ImGui::End();
    }
    if (m.setupStep == 1) {
        // What the fingers do, under the tabs.
        if (BeginPanel(ui, "##setup-hint", {(v.min.x + v.max.x) / 2 - ui.S(170), v.min.y + ui.S(56)}, {ui.S(340), ui.S(26)}, true))
            Tag(ui, {(v.min.x + v.max.x) / 2, v.min.y + ui.S(69)}, Tr("1 палец — вращение · 2 — сдвиг · щипок — масштаб"));
        ImGui::End();
    }
    if (close) {
        Finish(app);
    } else if (next) {
        if (m.setupStep == 1) {
            m.setupStep = 2;
        } else {
            m.setupStep = 3;   // the layout editor, as the last step
            m.layoutEditor = true;
            m.editing = Part::None;
        }
    }
}

} // namespace bz::app::mobile
