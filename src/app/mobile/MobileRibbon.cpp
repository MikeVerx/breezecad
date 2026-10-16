// Block 2 of the phone layout (docs/ADR/0004-mobile-touch-ui.md): the
// commands of the current tab as a list for the thumb — the same content
// as the desktop's ribbon (shell/Ribbon.cpp, FillRibbon), laid out as
// rows. A group is a header that folds; a grid of icons (sketch
// constraints) stays a grid.
#include "app/mobile/MobileLayout.h"

#include "app/shell/Shell.h"
#include "core/I18n.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace bz::app::mobile {
namespace {

constexpr float kRow = 44.0f;   ///< ≈7 mm on a phone: a thumb hits it

class ListSink final : public RibbonSink {
public:
    ListSink(AppContext& app, float width, bool iconsOnly) : myApp(app), myUi(app.ui), myWidth(width), myIconsOnly(iconsOnly) {}

    AppContext& App() const override { return myApp; }

    void BeginGroup(const char* caption) override {
        auto& folded = myApp.shell.mobile.collapsedGroups;
        myFolded = std::find(folded.begin(), folded.end(), caption) != folded.end();
        const ui::ThemeColors& c = myUi.C();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = myUi.S(myIconsOnly ? 10 : 30);
        ImGui::PushID(caption);
        if (ImGui::InvisibleButton("##group", {myWidth, h})) {
            if (myFolded) folded.erase(std::remove(folded.begin(), folded.end(), caption), folded.end());
            else folded.emplace_back(caption);
        }
        ImGui::PopID();
        if (myIconsOnly) {
            dl->AddLine({p.x + myUi.S(8), p.y + h / 2}, {p.x + myWidth - myUi.S(8), p.y + h / 2}, ui::ToU32(c.divider));
            myFolded = false;   // no headers to unfold in "icons only"
            return;
        }
        dl->AddText(myUi.fonts.caption, myUi.S(11), {p.x + myUi.S(12), p.y + myUi.S(10)}, ui::ToU32(c.textMuted), caption);
        myUi.icons.Draw(dl, myFolded ? "ui-chevron-right" : "ui-chevron-down", {p.x + myWidth - myUi.S(22), p.y + myUi.S(10)},
                        myUi.S(10), c.textMuted);
    }

    void EndGroup() override {
        FinishGrid();
        myFolded = false;
    }

    void Large(const char* actionId, const char* caption, bool active) override { Row(actionId, caption, active); }
    void Small(const char* actionId, const char* label, bool active) override { Row(actionId, label, active); }
    void LargeLater(const char* icon, const char* caption) override { LaterRow(icon, caption); }
    void SmallLater(const char* icon, const char* label) override { LaterRow(icon, label); }
    void BeginColumn() override {}
    void EndColumn() override {}

    void BeginGrid(int) override {
        myGrid = true;
        myGridIndex = 0;
        myGridOrigin = ImGui::GetCursorScreenPos();
    }

    void Icon(const char* actionId, bool active) override {
        if (myFolded) return;
        const Action* a = myApp.actions.Find(actionId);
        if (!a) return;
        const float cell = myUi.S(kRow);
        const int columns = std::max(1, static_cast<int>(myWidth / cell));
        const int col = myGridIndex % columns, row = myGridIndex / columns;
        ImGui::SetCursorScreenPos({myGridOrigin.x + static_cast<float>(col) * cell, myGridOrigin.y + static_cast<float>(row) * cell});
        if (ui::IconButton(myUi, actionId, a->icon, kRow, 22, a->Label(), a->IsEnabled(), active)) Run(actionId);
        ++myGridIndex;
    }

    void EndGrid() override { FinishGrid(); }

private:
    void FinishGrid() {
        if (!myGrid) return;
        myGrid = false;
        const float cell = myUi.S(kRow);
        const int columns = std::max(1, static_cast<int>(myWidth / cell));
        const int rows = (myGridIndex + columns - 1) / columns;
        ImGui::SetCursorScreenPos({myGridOrigin.x, myGridOrigin.y + static_cast<float>(rows) * cell});
        ImGui::Dummy({myWidth, 0});
    }

    void Run(const char* actionId) {
        const std::string id = actionId;
        AppContext& app = myApp;
        app.Later([&app, id] { app.actions.Run(id); });
    }

    void Row(const char* actionId, const char* label, bool active) {
        if (myFolded) return;
        const Action* a = myApp.actions.Find(actionId);
        if (!a) return;
        // A caption written for two ribbon lines reads as one here.
        std::string text = label ? label : a->Label();
        // "Выдав-\nливание" → "Выдавливание"; "Начать\nэскиз" → "Начать эскиз".
        for (std::size_t at; (at = text.find("-\n")) != std::string::npos;) text.erase(at, 2);
        std::replace(text.begin(), text.end(), '\n', ' ');
        if (DrawRow(actionId, a->icon, text.c_str(), a->IsEnabled(), active)) Run(actionId);
    }

    void LaterRow(const char* icon, const char* label) {
        if (myFolded) return;
        ImGui::PushID(label);
        DrawRow("##later", icon, label, false, false);
        ImGui::PopID();
    }

    bool DrawRow(const char* id, std::string_view icon, const char* text, bool enabled, bool active) {
        const ui::ThemeColors& c = myUi.C();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = myUi.S(kRow);
        const bool clicked = ImGui::InvisibleButton(id, {myWidth, h}) && enabled;
        const bool pressed = ImGui::IsItemActive();
        if (active || pressed)
            dl->AddRectFilled({p.x + myUi.S(4), p.y + myUi.S(2)}, {p.x + myWidth - myUi.S(4), p.y + h - myUi.S(2)},
                              ui::ToU32(active ? c.accentSoft : c.pressed), myUi.S(8));
        const Color ink = !enabled ? c.textDisabled : active ? c.accent : c.text;
        const float icon_ = myUi.S(22);
        const float ix = myIconsOnly ? p.x + (myWidth - icon_) / 2 : p.x + myUi.S(12);
        myUi.icons.Draw(dl, icon, {ix, p.y + (h - icon_) / 2}, icon_, enabled ? Color{1, 1, 1, 1} : Color{1, 1, 1, 0.4f});
        if (!myIconsOnly) {
            const float fs = myUi.S(14);
            ImGui::PushClipRect(p, {p.x + myWidth - myUi.S(6), p.y + h}, true);
            dl->AddText(myUi.fonts.body, fs, {p.x + myUi.S(44), p.y + (h - fs) / 2 - myUi.S(1)}, ui::ToU32(ink), text);
            ImGui::PopClipRect();
        } else if (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ui::ItemTooltip(myUi, text);
        }
        return clicked;
    }

    AppContext& myApp;
    ui::Ui&     myUi;
    float       myWidth;
    bool        myIconsOnly;
    bool        myFolded = false;
    bool        myGrid = false;
    int         myGridIndex = 0;
    ImVec2      myGridOrigin{0, 0};
};

} // namespace

void DrawToolList(AppContext& app, Session& session, float width, bool iconsOnly) {
    ListSink sink(app, width, iconsOnly);
    FillRibbon(app, session, sink, false);
    ImGui::Dummy({width, app.ui.S(8)});
}

} // namespace bz::app::mobile
