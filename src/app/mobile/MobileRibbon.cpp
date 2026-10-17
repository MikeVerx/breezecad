// Block 2 of the phone layout (docs/ADR/0004-mobile-touch-ui.md and the
// approved mockup): the commands of the current tab as a list for the
// thumb — the same content as the desktop's ribbon (shell/Ribbon.cpp,
// FillRibbon). Groups become small capital headers ("СОЗДАНИЕ"); kindred
// commands fold under one row with a ˅ (Вращение ˅ — сдвиг, лофт,
// пружина); the ribbon's colour icons stay (the author liked them better
// than the mockup's line icons, 16.10.2026). A grid of icons (sketch
// constraints) stays a grid. "Icons only": the rows are icons, a small
// corner mark shows there is more under a long press.
#include "app/mobile/MobileLayout.h"

#include "app/shell/Shell.h"
#include "core/I18n.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace bz::app::mobile {
namespace {

constexpr float kRow = 44.0f;   ///< ≈7 mm on a phone: a thumb hits it

/// Commands shown under another one's ˅ rather than as rows of their own.
struct Kin {
    const char* id;
    const char* under;
};
constexpr Kin kKin[] = {
    {"feature.sweep", "feature.revolve"},           {"feature.loft", "feature.revolve"},
    {"feature.coil", "feature.revolve"},            {"feature.chamfer", "feature.fillet"},
    {"feature.shell", "feature.fillet"},            {"feature.patternCircular", "feature.patternRect"},
    {"feature.mirror", "feature.patternRect"},      {"feature.workAxis", "feature.workPlane"},
    {"feature.workPoint", "feature.workPlane"},     {"file.exportStl", "file.exportStep"},
    {"file.export3mf", "file.exportStep"},          {"sketch.patternCircular", "sketch.patternRect"},
    {"sketch.mirror", "sketch.patternRect"},        {"sketch.rotate", "sketch.move"},
    {"sketch.copy", "sketch.move"},                 {"sketch.scale", "sketch.move"},
    {"sketch.extend", "sketch.trim"},               {"sketch.split", "sketch.trim"},
    {"sketch.spline", "sketch.line"},               {"sketch.point", "sketch.line"},
    {"sketch.projectFixed", "sketch.project"},      {"sketch.centerline", "sketch.construction"},
    {"view.edges", "view.shaded"},                  {"view.wireframe", "view.shaded"},
    {"view.hidden", "view.shaded"},                 {"view.transparent", "view.shaded"},
    {"view.top", "view.front"},                     {"view.right", "view.front"},
    {"view.themeLight", "view.themeSystem"},        {"view.themeDark", "view.themeSystem"},
    {"assembly.jointSlider", "assembly.jointRotational"}, {"assembly.jointCylindrical", "assembly.jointRotational"},
    {"assembly.jointPlanar", "assembly.jointRotational"}, {"assembly.jointBall", "assembly.jointRotational"},
    {"assembly.jointRigid", "assembly.jointRotational"},  {"assembly.tangent", "assembly.angle"},
    {"assembly.axisMate", "assembly.angle"},        {"assembly.rotate", "assembly.move"},
    {"tools.interference", "tools.massProperties"},
};

const char* ParentOf(const std::string& id) {
    for (const Kin& k : kKin)
        if (id == k.id) return k.under;
    return nullptr;
}

/// Small capitals for a group's header: Cyrillic and Latin letters up.
std::string Upper(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto b = static_cast<unsigned char>(text[i]);
        if (b >= 'a' && b <= 'z') {
            out += static_cast<char>(b - 32);
        } else if (b == 0xD0 && i + 1 < text.size()) {   // а..п (D0 B0..BF) → А..П (D0 90..9F)
            const auto n = static_cast<unsigned char>(text[i + 1]);
            out += static_cast<char>(0xD0);
            out += static_cast<char>(n >= 0xB0 && n <= 0xBF ? n - 0x20 : n);
            ++i;
        } else if (b == 0xD1 && i + 1 < text.size()) {   // р..я (D1 80..8F) → Р..Я (D0 A0..AF), ё (D1 91) → Ё (D0 81)
            const auto n = static_cast<unsigned char>(text[i + 1]);
            if (n >= 0x80 && n <= 0x8F) {
                out += static_cast<char>(0xD0);
                out += static_cast<char>(n + 0x20);
            } else if (n == 0x91) {
                out += static_cast<char>(0xD0);
                out += static_cast<char>(0x81);
            } else {
                out += static_cast<char>(b);
                out += static_cast<char>(n);
            }
            ++i;
        } else {
            out += static_cast<char>(b);
        }
    }
    return out;
}

class ListSink final : public RibbonSink {
public:
    ListSink(AppContext& app, float width, bool iconsOnly) : myApp(app), myUi(app.ui), myWidth(width), myIconsOnly(iconsOnly) {}

    AppContext& App() const override { return myApp; }

    void BeginGroup(const char* caption) override {
        Flush();
        myCaption = caption;
        myHeaderDrawn = false;
    }

    void EndGroup() override {
        Flush();
        FinishGrid();
    }

    void Large(const char* actionId, const char* caption, bool active) override { Add(actionId, caption, active, false, nullptr); }
    void Small(const char* actionId, const char* label, bool active) override { Add(actionId, label, active, false, nullptr); }
    void LargeLater(const char* icon, const char* caption) override { Add("", caption, false, true, icon); }
    void SmallLater(const char* icon, const char* label) override { Add("", label, false, true, icon); }
    void BeginColumn() override {}
    void EndColumn() override {}

    void BeginGrid(int) override {
        Flush();
        Header();
        myGrid = true;
        myGridIndex = 0;
        myGridOrigin = ImGui::GetCursorScreenPos();
    }

    void Icon(const char* actionId, bool active) override {
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
    struct Item {
        std::string id;      ///< empty: a command that comes later (greyed)
        std::string label;
        std::string icon;
        bool        active = false;
        std::vector<Item> kin;   ///< folded under this one
    };

    void Add(const char* actionId, const char* label, bool active, bool later, const char* icon) {
        Item item;
        item.id = actionId;
        const Action* a = later ? nullptr : myApp.actions.Find(actionId);
        if (!later && !a) return;
        // A caption written for two ribbon lines reads as one here.
        std::string text = label ? label : a->Label();
        for (std::size_t at; (at = text.find("-\n")) != std::string::npos;) text.erase(at, 2);
        std::replace(text.begin(), text.end(), '\n', ' ');
        item.label = text;
        item.icon = later ? icon : a->icon;
        item.active = active;
        if (const char* parent = later ? nullptr : ParentOf(item.id)) {
            for (Item& p : myItems)
                if (p.id == parent) {
                    p.active = p.active || active;
                    p.kin.push_back(std::move(item));
                    return;
                }
        }
        myItems.push_back(std::move(item));
    }

    void Header() {
        if (myHeaderDrawn || !myCaption) return;
        myHeaderDrawn = true;
        const ui::ThemeColors& c = myUi.C();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (myIconsOnly) {
            const float h = myUi.S(10);
            dl->AddLine({p.x + myUi.S(10), p.y + h / 2}, {p.x + myWidth - myUi.S(10), p.y + h / 2}, ui::ToU32(c.divider));
            ImGui::Dummy({myWidth, h});
            return;
        }
        const float h = myUi.S(30);
        const std::string caps = Upper(myCaption);
        dl->AddText(myUi.fonts.strong, myUi.S(10.5f), {p.x + myUi.S(12), p.y + myUi.S(14)}, ui::ToU32(c.textMuted), caps.c_str());
        ImGui::Dummy({myWidth, h});
    }

    void Flush() {
        if (myItems.empty()) return;
        Header();
        auto& open = myApp.shell.mobile.collapsedGroups;   // here: the rows whose ˅ is open
        for (const Item& item : myItems) {
            const bool hasKin = !item.kin.empty();
            const std::string key = "kin:" + item.id;
            const bool expanded = std::find(open.begin(), open.end(), key) != open.end();
            if (DrawRow(item, hasKin, expanded, 0.0f)) {
                if (expanded) open.erase(std::remove(open.begin(), open.end(), key), open.end());
                else open.push_back(key);
            }
            if (hasKin && expanded)
                for (const Item& k : item.kin) DrawRow(k, false, false, myIconsOnly ? 0.0f : myUi.S(18));
        }
        myItems.clear();
    }

    void FinishGrid() {
        if (!myGrid) return;
        myGrid = false;
        const float cell = myUi.S(kRow);
        const int columns = std::max(1, static_cast<int>(myWidth / cell));
        const int rows = (myGridIndex + columns - 1) / columns;
        ImGui::SetCursorScreenPos({myGridOrigin.x, myGridOrigin.y + static_cast<float>(rows) * cell});
        ImGui::Dummy({myWidth, 0});
    }

    void Run(const std::string& actionId) {
        AppContext& app = myApp;
        app.Later([&app, actionId] { app.actions.Run(actionId); });
    }

    /// One row; true when its ˅ was pressed (or, icons only, a long press on it).
    bool DrawRow(const Item& item, bool hasKin, bool expanded, float indent) {
        const ui::ThemeColors& c = myUi.C();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const Action* a = item.id.empty() ? nullptr : myApp.actions.Find(item.id);
        const bool enabled = a && a->IsEnabled();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = myUi.S(kRow);
        const float chevronW = hasKin && !myIconsOnly ? myUi.S(36) : 0.0f;
        bool toggled = false;
        ImGui::PushID(item.id.empty() ? item.label.c_str() : item.id.c_str());
        const bool clicked = ImGui::InvisibleButton("##row", {myWidth - chevronW, h},
                                                    ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool pressed = ImGui::IsItemActive();
        if (clicked && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && enabled) Run(item.id);
        if (hasKin && ImGui::IsItemClicked(ImGuiMouseButton_Right)) toggled = true;   // a long press opens the kin
        if (chevronW > 0) {
            ImGui::SetCursorScreenPos({p.x + myWidth - chevronW, p.y});
            if (ImGui::InvisibleButton("##kin", {chevronW, h})) toggled = true;
        }
        ImGui::PopID();
        if (item.active || pressed) {
            dl->AddRectFilled(p, {p.x + myWidth, p.y + h}, ui::ToU32(item.active ? c.accentSoft : c.pressed));
            if (item.active) dl->AddRectFilled(p, {p.x + myUi.S(3), p.y + h}, ui::ToU32(c.accent));
        }
        if (chevronW > 0)
            myUi.icons.Draw(dl, expanded ? "ui-chevron-down" : "ui-chevron-right",
                            {p.x + myWidth - chevronW + (chevronW - myUi.S(12)) / 2, p.y + (h - myUi.S(12)) / 2}, myUi.S(12), c.textMuted);
        const Color ink = !enabled ? c.textDisabled : item.active ? c.accent : c.text;
        const float is = myUi.S(22);
        const float ix = myIconsOnly ? p.x + (myWidth - is) / 2 : p.x + myUi.S(12) + indent;
        myUi.icons.Draw(dl, item.icon, {ix, p.y + (h - is) / 2}, is, enabled ? Color{1, 1, 1, 1} : Color{1, 1, 1, 0.4f});
        if (myIconsOnly) {
            if (hasKin) {   // the corner mark: more under a long press
                const ImVec2 q{ix + is + myUi.S(4), p.y + (h + is) / 2 + myUi.S(2)};
                dl->AddTriangleFilled(q, {q.x - myUi.S(6), q.y}, {q.x, q.y - myUi.S(6)}, ui::ToU32(c.textMuted));
            }
        } else {
            const float fs = myUi.S(14);
            ImGui::PushClipRect(p, {p.x + myWidth - chevronW - myUi.S(4), p.y + h}, true);
            dl->AddText(myUi.fonts.body, fs, {ix + myUi.S(32), p.y + (h - fs) / 2 - myUi.S(1)}, ui::ToU32(ink), item.label.c_str());
            ImGui::PopClipRect();
        }
        ImGui::SetCursorScreenPos({p.x, p.y + h});
        ImGui::Dummy({myWidth, 0});
        return toggled;
    }

    AppContext&       myApp;
    ui::Ui&           myUi;
    float             myWidth;
    bool              myIconsOnly;
    const char*       myCaption = nullptr;
    bool              myHeaderDrawn = false;
    std::vector<Item> myItems;
    bool              myGrid = false;
    int               myGridIndex = 0;
    ImVec2            myGridOrigin{0, 0};
};

/// Block 2, compact (the author's default, 17.10.2026; mockup 3): a group is
/// one row of four equal buttons. With more than four commands the one
/// nearest the view is "›" — it opens the group as a strip over the view
/// (DrawToolStrip), which scrolls sideways.
class CompactSink final : public RibbonSink {
public:
    CompactSink(AppContext& app, float width, bool viewOnLeft) : myApp(app), myUi(app.ui), myWidth(width), myViewOnLeft(viewOnLeft) {}

    AppContext& App() const override { return myApp; }
    void BeginGroup(const char* caption) override {
        Flush();
        myCaption = caption ? caption : "";
    }
    void EndGroup() override { Flush(); }
    void Large(const char* id, const char* caption, bool active) override { Add(id, caption, active); }
    void Small(const char* id, const char* label, bool active) override { Add(id, label, active); }
    void LargeLater(const char*, const char*) override {}
    void SmallLater(const char*, const char*) override {}
    void BeginColumn() override {}
    void EndColumn() override {}
    void BeginGrid(int) override {}
    void Icon(const char* id, bool active) override { Add(id, nullptr, active); }
    void EndGrid() override {}

private:
    void Add(const char* actionId, const char* label, bool active) {
        const Action* a = myApp.actions.Find(actionId);
        if (!a) return;
        std::string text = label ? label : a->Label();
        for (std::size_t at; (at = text.find("-\n")) != std::string::npos;) text.erase(at, 2);
        std::replace(text.begin(), text.end(), '\n', ' ');
        myItems.push_back({actionId, text, a->icon, active});
    }

    void Flush() {
        if (myItems.empty()) return;
        MobileState& m = myApp.shell.mobile;
        const ui::ThemeColors& c = myUi.C();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // The header, as in the list.
        ImVec2 p = ImGui::GetCursorScreenPos();
        const std::string caps = Upper(myCaption);
        dl->AddText(myUi.fonts.strong, myUi.S(10.5f), {p.x + myUi.S(12), p.y + myUi.S(10)}, ui::ToU32(c.textMuted), caps.c_str());
        ImGui::Dummy({myWidth, myUi.S(26)});
        p = ImGui::GetCursorScreenPos();
        const bool more = myItems.size() > 4;
        if (more && m.flyGroup == "*") m.flyGroup = myCaption;   // a test run: the first group that has a strip
        const bool open = more && m.flyGroup == myCaption;
        if (open) {
            m.flyItems = myItems;
            m.flyY = p.y;
        }
        const float pad = myUi.S(8), gap = myUi.S(6);
        const float cell = (myWidth - 2 * pad - 3 * gap) / 4;
        const float h = cell;   // square, as IconButton draws them
        for (int slot = 0; slot < 4; ++slot) {
            // The "›" goes to the slot nearest the view; the commands fill the rest in order.
            const int chevronSlot = myViewOnLeft ? 0 : 3;
            const ImVec2 a{p.x + pad + static_cast<float>(slot) * (cell + gap), p.y};
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(slot);
            if (more && slot == chevronSlot) {
                const bool clicked = ImGui::InvisibleButton("##more", {cell, h});
                const bool held = ImGui::IsItemActive();
                dl->AddRectFilled(a, {a.x + cell, a.y + h}, ui::ToU32(open ? c.accent : held ? c.pressed : c.accentSoft), myUi.S(9));
                if (!open) dl->AddRect(a, {a.x + cell, a.y + h}, ui::ToU32(c.accentSoftBorder), myUi.S(9));
                const bool pointLeft = myViewOnLeft != open;   // toward the view to open, back to close
                const float is = myUi.S(16);
                myUi.icons.Draw(dl, pointLeft ? "ui-arrow-left" : "ui-chevron-right", {a.x + (cell - is) / 2, a.y + (h - is) / 2}, is,
                                open ? c.textOnAccent : c.accent);
                if (clicked) {
                    m.flyGroup = open ? std::string() : myCaption;
                    m.flyScroll = 0;
                }
            } else {
                const int index = more ? (myViewOnLeft ? slot - 1 : slot) : slot;
                if (index >= 0 && index < static_cast<int>(myItems.size())) {
                    const MobileState::FlyItem& it = myItems[static_cast<std::size_t>(index)];
                    const Action* act = myApp.actions.Find(it.id);
                    const bool enabled = act && act->IsEnabled();
                    if (ui::IconButton(myUi, it.id.c_str(), it.icon, cell / myUi.scale, 22, it.label.c_str(), enabled, it.active) && enabled) {
                        AppContext& app = myApp;
                        const std::string run = it.id;
                        app.Later([&app, run] { app.actions.Run(run); });
                    }
                    // A square button wider than tall: drawn by IconButton as a square of `cell`; keep the row height.
                }
            }
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos({p.x, p.y + std::max(h, cell) + myUi.S(4)});
        ImGui::Dummy({myWidth, 0});
        myItems.clear();
    }

    AppContext&                         myApp;
    ui::Ui&                             myUi;
    float                               myWidth;
    bool                                myViewOnLeft;
    std::string                         myCaption;
    std::vector<MobileState::FlyItem>   myItems;
};

} // namespace

void DrawToolList(AppContext& app, Session& session, float width, bool iconsOnly, bool compact, bool viewOnLeft) {
    if (compact && !iconsOnly) {
        CompactSink sink(app, width, viewOnLeft);
        FillRibbon(app, session, sink, false);
    } else {
        ListSink sink(app, width, iconsOnly);
        FillRibbon(app, session, sink, false);
    }
    ImGui::Dummy({width, app.ui.S(8)});
}

void DrawToolStrip(AppContext& app, ImVec2 viewMin, ImVec2 viewMax, bool blockOnRight) {
    MobileState& m = app.shell.mobile;
    if (m.flyGroup.empty()) return;
    if (m.flyItems.empty()) {   // the group is not in block 2 any more (another tab)
        m.flyGroup.clear();
        return;
    }
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    // Over the view, level with the group's row, from block 2 toward block 1 (never over it).
    const float h = ui.S(78);
    const float y = std::clamp(m.flyY - ui.S(16), viewMin.y + ui.S(56), std::max(viewMin.y + ui.S(56), viewMax.y - h - ui.S(4)));
    const ImVec2 pos{viewMin.x, y}, size{viewMax.x - viewMin.x, h};
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::ToVec4(c.panel.WithAlpha(0.97f)));
    const bool shown = ImGui::Begin("##tool-strip", nullptr,
                                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    bool inside = false;
    if (shown) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddLine(pos, {pos.x + size.x, pos.y}, ui::ToU32(c.accentSoftBorder));
        dl->AddLine({pos.x, pos.y + h - 1}, {pos.x + size.x, pos.y + h - 1}, ui::ToU32(c.accentSoftBorder));
        const float tw = ui.S(76), th = ui.S(64), gap = ui.S(4), pad = ui.S(8);
        const float content = pad * 2 + static_cast<float>(m.flyItems.size()) * (tw + gap);
        m.flyScroll = std::clamp(m.flyScroll, 0.0f, std::max(0.0f, content - size.x));
        // One surface for the whole strip: a drag scrolls it, a tap runs the command under it.
        ImGui::SetCursorScreenPos(pos);
        static float dragged = 0;
        const bool clicked = ImGui::InvisibleButton("##strip-surface", size);
        if (ImGui::IsItemActivated()) dragged = 0;
        if (ImGui::IsItemActive()) {
            const float dx = ImGui::GetIO().MouseDelta.x;
            dragged += std::abs(dx);
            m.flyScroll = std::clamp(m.flyScroll - dx, 0.0f, std::max(0.0f, content - size.x));
        }
        inside = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) || ImGui::IsItemActive();
        // Laid out from block 2's side: its first command nearest block 2.
        const ImVec2 mouse = ImGui::GetMousePos();
        dl->PushClipRect(pos, {pos.x + size.x, pos.y + size.y}, true);
        for (std::size_t i = 0; i < m.flyItems.size(); ++i) {
            const MobileState::FlyItem& it = m.flyItems[i];
            const float off = pad + static_cast<float>(i) * (tw + gap) - m.flyScroll;
            const float x = blockOnRight ? pos.x + size.x - off - tw : pos.x + off;
            const ImVec2 a{x, pos.y + (h - th) / 2}, b{x + tw, a.y + th};
            const Action* act = app.actions.Find(it.id);
            const bool enabled = act && act->IsEnabled();
            const bool under = ImGui::IsItemActive() && mouse.x >= a.x && mouse.x < b.x && dragged < ui.S(8);
            if (it.active || under) dl->AddRectFilled(a, b, ui::ToU32(it.active ? c.accentSoft : c.pressed), ui.S(10));
            const float is = ui.S(26);
            ui.icons.Draw(dl, it.icon, {x + (tw - is) / 2, a.y + ui.S(8)}, is, enabled ? Color{1, 1, 1, 1} : Color{1, 1, 1, 0.4f});
            ui::FittedText(dl, ui.fonts.caption, ui.S(10.5f),
                           {x + std::max(ui.S(4), (tw - ui.fonts.caption->CalcTextSizeA(ui.S(10.5f), FLT_MAX, 0, it.label.c_str()).x) / 2),
                            a.y + ui.S(40)},
                           x + tw - ui.S(3), ui::ToU32(enabled ? c.textSecondary : c.textDisabled), it.label.c_str());
            if (clicked && dragged < ui.S(8) && mouse.x >= a.x && mouse.x < b.x && enabled) {
                const std::string run = it.id;
                app.Later([&app, run] { app.actions.Run(run); });
                m.flyGroup.clear();   // the command chosen: the strip goes
            }
        }
        dl->PopClipRect();
    }
    ImGui::End();
    // A tap elsewhere (but not on block 2, whose "›" toggles it) closes it.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !inside && !m.rBlock2.Contains(ImGui::GetMousePos())) m.flyGroup.clear();
}

} // namespace bz::app::mobile
