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

#include "app/Session.h"
#include "app/shell/Shell.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "ui/Menus.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <array>
#include <format>
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

/// Collects what the ribbon would show: every command once, with its "on" state.
class CollectSink final : public RibbonSink {
public:
    explicit CollectSink(AppContext& app) : myApp(app) {}
    AppContext& App() const override { return myApp; }
    void BeginGroup(const char*) override {}
    void EndGroup() override {}
    void Large(const char* id, const char*, bool active) override { Add(id, active); }
    void Small(const char* id, const char*, bool active) override { Add(id, active); }
    void LargeLater(const char*, const char*) override {}
    void SmallLater(const char*, const char*) override {}
    void BeginColumn() override {}
    void EndColumn() override {}
    void BeginGrid(int) override {}
    void Icon(const char* id, bool active) override { Add(id, active); }
    void EndGrid() override {}

    struct Entry {
        std::string id;
        bool active = false;
        bool used = false;
    };
    std::vector<Entry> entries;

    Entry* Find(const std::string& id) {
        for (Entry& e : entries)
            if (e.id == id) return &e;
        return nullptr;
    }

private:
    void Add(const char* id, bool active) {
        if (!myApp.actions.Find(id)) return;
        if (Entry* e = Find(id)) {
            e->active = e->active || active;
            return;
        }
        entries.push_back({id, active, false});
    }
    AppContext& myApp;
};

/// The phone's groups (the author's mockups 13, 14, F1–F9, 18.10.2026): the ribbon's
/// commands regrouped for a thumb — the most used first, kindred groups merged. A folding
/// group shows its first three commands and an opener; the rest come out under them.
/// "phone.export" stands for the export commands: one button, the format in a menu.
struct PhoneGroup {
    const char* title;
    std::vector<const char*> ids;
    bool folds = false;
};

const std::vector<PhoneGroup>& PhoneGroups(AppContext& app, Session& session) {
    static const std::vector<PhoneGroup> sketch{
        {N_("Эскиз"), {"sketch.start"}},
        {N_("Размеры"), {"sketch.dimension", "sketch.arrangeDimensions"}},
        {N_("Создать"), {"sketch.line", "sketch.circle", "sketch.arc", "sketch.rectangle", "sketch.point", "sketch.spline", "sketch.text"}},
        {N_("Зависимости"),
         {"sketch.coincident", "sketch.horizontal", "sketch.vertical", "sketch.fix", "sketch.parallel", "sketch.perpendicular", "sketch.equal",
          "sketch.midpoint", "sketch.tangent", "sketch.concentric", "sketch.symmetric", "sketch.showConstraints", "sketch.hideConstraints"},
         true},
        {N_("Изменить"),
         {"sketch.trim", "sketch.extend", "sketch.offset", "sketch.fillet", "sketch.move", "sketch.rotate", "sketch.copy", "sketch.scale",
          "sketch.split", "edit.delete"},
         true},
        {N_("Массив и модель"), {"sketch.patternRect", "sketch.patternCircular", "sketch.mirror", "sketch.project", "sketch.projectFixed"}},
        {N_("Формат и вид"), {"sketch.construction", "sketch.centerline", "sketch.ortho", "sketch.lookAt"}},
    };
    static const std::vector<PhoneGroup> model{
        {N_("Эскиз и раб. элементы"), {"sketch.start", "feature.workPlane", "feature.workAxis", "feature.workPoint"}},
        {N_("Создать"), {"feature.extrude", "feature.revolve", "feature.sweep", "feature.loft", "feature.coil"}},
        {N_("Изменить"), {"feature.hole", "feature.fillet", "feature.chamfer", "feature.thread", "feature.shell", "feature.section"}, true},
        {N_("Массив"), {"feature.patternRect", "feature.patternCircular", "feature.mirror"}},
        {N_("Диагностика"), {"tools.measure", "view.section", "tools.massProperties", "tools.interference"}},
        {N_("Изделия и экспорт"), {"parts.library", "phone.export"}},
    };
    static const std::vector<PhoneGroup> assembly{
        {N_("Компонент"), {"assembly.place", "assembly.placeStandard", "assembly.pattern"}},
        {N_("Зависимости"), {"assembly.mate", "assembly.insert", "assembly.axisMate", "assembly.angle", "assembly.tangent"}, true},
        {N_("Соединения"),
         {"assembly.jointRotational", "assembly.jointSlider", "assembly.jointRigid", "assembly.jointCylindrical", "assembly.jointPlanar",
          "assembly.jointBall"},
         true},
        {N_("Положение"), {"assembly.move", "assembly.rotate", "assembly.ground"}},
        {N_("Диагностика и экспорт"), {"tools.measure", "view.section", "tools.interference", "phone.export"}},
    };
    // «Оформление» (the theme) is not here: on the phone it lives in the settings (the author, 18.10).
    static const std::vector<PhoneGroup> view{
        {N_("Стиль отображения"), {"view.shaded", "view.edges", "view.wireframe", "view.hidden", "view.transparent"}},
        {N_("Цвета"), {"view.appearance", "view.background"}},
        {N_("Ориентация"), {"view.home", "view.fit", "view.front", "view.top", "view.right"}},
    };
    if (app.shell.ribbonTab == RibbonTab::View) return view;
    if (session.assembly) return assembly;
    return app.shell.ribbonTab == RibbonTab::Sketch ? sketch : model;
}

constexpr const char* kExports[] = {"file.exportStep", "file.exportStl", "file.export3mf"};
constexpr const char* kHidden[] = {"view.themeSystem", "view.themeLight", "view.themeDark", "sketch.finish"};

/// One command button of the grid. Front, Top and Right share an icon: a letter tells them apart.
void CommandCell(AppContext& app, const char* id, bool active, ImVec2 at, ImVec2 size) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool exportCell = std::strcmp(id, "phone.export") == 0;
    const Action* a = exportCell ? nullptr : app.actions.Find(id);
    const bool enabled = exportCell || (a && a->IsEnabled());
    ImGui::SetCursorScreenPos(at);
    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton("##cell", size);
    const bool held = ImGui::IsItemActive();
    const Color bg = active ? c.accent : held ? c.pressed : c.field;
    dl->AddRectFilled(at, {at.x + size.x, at.y + size.y}, ui::ToU32(bg), ui.S(8));
    const float is = ui.S(22);
    const std::string icon = exportCell ? std::string("export") : std::string(a->icon);
    ui.icons.Draw(dl, icon, {at.x + (size.x - is) / 2, at.y + (size.y - is) / 2}, is, enabled ? Color{1, 1, 1, 1} : Color{1, 1, 1, 0.35f});
    const char* letter = std::strcmp(id, "view.front") == 0 ? "Ф" : std::strcmp(id, "view.top") == 0 ? "В" : std::strcmp(id, "view.right") == 0 ? "П" : nullptr;
    if (letter) {
        const char* shown = Tr(letter);
        const float fs = ui.S(10);
        const float lw = ui.fonts.strong->CalcTextSizeA(fs, FLT_MAX, 0, shown).x;
        dl->AddText(ui.fonts.strong, fs, {at.x + size.x - lw - ui.S(4), at.y + size.y - fs - ui.S(2)}, ui::ToU32(c.text), shown);
    }
    if (exportCell) {
        if (clicked) ImGui::OpenPopup("##export-menu");
        ui::MenuStyle style(ui);
        if (ImGui::BeginPopup("##export-menu")) {
            for (const char* e : kExports)
                if (const Action* ea = app.actions.Find(e); ea && ui::MenuItem(ui, ea->Label(), nullptr, ea->icon, false, ea->IsEnabled())) {
                    const std::string run = e;
                    app.Later([&app, run] { app.actions.Run(run); });
                }
            ImGui::EndPopup();
        }
    } else {
        ui::ItemTooltip(ui, a->Label());
        if (clicked && enabled) {
            const std::string run = id;
            app.Later([&app, run] { app.actions.Run(run); });
        }
    }
    ImGui::PopID();
}

/// Block 2, compact (the default; the author's mockups 13–14, 18.10.2026): small buttons without
/// captions, four in a row; «Зависимости», «Изменить», «Соединения» fold to one row — the opener on
/// the view's side, then three commands; the rest comes out under them. What is open is remembered.
void DrawPhoneGrid(AppContext& app, Session& session, float width, bool viewOnLeft) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    CollectSink all(app);
    FillRibbon(app, session, all, false);
    for (const char* h : kHidden)
        if (auto* e = all.Find(h)) e->used = true;

    const float pad = ui.S(8), gap = ui.S(4);
    const float cell = (width - 2 * pad - 3 * gap) / 4;
    const float h = std::min(cell, ui.S(40));
    std::vector<std::string>& opened = app.settings.mobile.openGroups;
    const std::string tabKey = session.assembly ? "asm" : app.shell.ribbonTab == RibbonTab::Sketch ? "sketch"
                               : app.shell.ribbonTab == RibbonTab::View ? "view" : "model";
    static bool loggedOpener = false;

    const auto drawGroup = [&](const char* title, const std::vector<std::pair<std::string, bool>>& items, bool folds) {
        if (items.empty()) return;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        const std::string caps = Upper(Tr(title));
        dl->AddText(ui.fonts.strong, ui.S(9.5f), {p.x + pad + ui.S(2), p.y + ui.S(7)}, ui::ToU32(c.textMuted), caps.c_str());
        p.y += ui.S(22);
        const bool canFold = folds && items.size() > 3;
        const std::string key = tabKey + ":" + title;
        const bool open = canFold && std::find(opened.begin(), opened.end(), key) != opened.end();
        // Slots in reading order; the opener takes the slot nearest the view.
        std::vector<int> order;   // command index per slot, -1 = the opener
        if (canFold) {
            const int shown = open ? static_cast<int>(items.size()) : 3;
            for (int i = 0; i < 3; ++i) order.push_back(i);
            order.insert(viewOnLeft ? order.begin() : order.end(), -1);
            for (int i = 3; i < shown; ++i) order.push_back(i);
        } else {
            for (int i = 0; i < static_cast<int>(items.size()); ++i) order.push_back(i);
        }
        ImGui::PushID(key.c_str());
        for (std::size_t k = 0; k < order.size(); ++k) {
            const int col = static_cast<int>(k % 4), row = static_cast<int>(k / 4);
            const ImVec2 at{p.x + pad + static_cast<float>(col) * (cell + gap), p.y + static_cast<float>(row) * (h + gap)};
            if (order[k] < 0) {
                if (!loggedOpener) {
                    loggedOpener = true;   // once: the emulator's check taps the first opener
                    log::Info("Touch: fold {} at {:.0f},{:.0f} size {:.0f}", key, at.x, at.y, h);
                }
                ImGui::SetCursorScreenPos(at);
                const bool clicked = ImGui::InvisibleButton("##opener", {cell, h});
                const bool held = ImGui::IsItemActive();
                dl->AddRectFilled(at, {at.x + cell, at.y + h}, ui::ToU32(open ? c.accent : held ? c.pressed : c.accentSoft), ui.S(8));
                if (!open) dl->AddRect(at, {at.x + cell, at.y + h}, ui::ToU32(c.accentSoftBorder), ui.S(8));
                const float is = ui.S(14);
                const Color ink = open ? c.textOnAccent : c.accent;
                if (open) {
                    ui.icons.Draw(dl, "ui-chevron-down", {at.x + (cell - is) / 2, at.y + (h - is) / 2}, is, ink);   // drawn as "up" below
                } else {
                    ui.icons.Draw(dl, "ui-chevron-down", {at.x + (cell - is) / 2, at.y + h / 2 - is + ui.S(2)}, is, ink);
                    const std::string more = std::format("+{}", items.size() - 3);
                    const float fs = ui.S(9.5f);
                    const float tw = ui.fonts.strong->CalcTextSizeA(fs, FLT_MAX, 0, more.c_str()).x;
                    dl->AddText(ui.fonts.strong, fs, {at.x + (cell - tw) / 2, at.y + h / 2 + ui.S(2)}, ui::ToU32(ink), more.c_str());
                }
                if (open) {   // an up arrow: the chevron's triangle flipped
                    const ImVec2 m{at.x + cell / 2, at.y + h / 2};
                    dl->AddRectFilled({m.x - is / 2, m.y - is / 2}, {m.x + is / 2, m.y + is / 2}, ui::ToU32(c.accent));
                    dl->AddPolyline(std::array<ImVec2, 3>{ImVec2{m.x - ui.S(5), m.y + ui.S(2.5f)}, ImVec2{m.x, m.y - ui.S(2.5f)},
                                                          ImVec2{m.x + ui.S(5), m.y + ui.S(2.5f)}}
                                        .data(),
                                    3, ui::ToU32(ink), ImDrawFlags_None, ui.S(1.8f));
                }
                if (clicked) {
                    if (open) opened.erase(std::remove(opened.begin(), opened.end(), key), opened.end());
                    else opened.push_back(key);
                    log::Info("Fold: {} {}", open ? "closed" : "open", key);
                }
            } else {
                const auto& [id, active] = items[static_cast<std::size_t>(order[k])];
                CommandCell(app, id.c_str(), active, at, {cell, h});
            }
        }
        ImGui::PopID();
        const int rows = static_cast<int>((order.size() + 3) / 4);
        ImGui::SetCursorScreenPos({p.x, p.y + static_cast<float>(rows) * (h + gap)});
        ImGui::Dummy({width, 0});
    };

    for (const PhoneGroup& g : PhoneGroups(app, session)) {
        std::vector<std::pair<std::string, bool>> items;
        for (const char* id : g.ids) {
            if (std::strcmp(id, "phone.export") == 0) {
                bool any = false;
                for (const char* e : kExports)
                    if (auto* x = all.Find(e); x && !x->used) {
                        x->used = true;
                        any = true;
                    }
                if (any) items.emplace_back(id, false);
                continue;
            }
            if (auto* e = all.Find(id); e && !e->used) {
                e->used = true;
                items.emplace_back(e->id, e->active);
            }
        }
        drawGroup(g.title, items, g.folds);
    }
    // Anything the ribbon has that the table does not know (a module's command): at the end.
    std::vector<std::pair<std::string, bool>> rest;
    for (const auto& e : all.entries)
        if (!e.used) rest.emplace_back(e.id, e.active);
    drawGroup(N_("Ещё"), rest, false);
}

} // namespace

void DrawToolList(AppContext& app, Session& session, float width, bool iconsOnly, bool compact, bool viewOnLeft) {
    if (compact && !iconsOnly) {
        DrawPhoneGrid(app, session, width, viewOnLeft);
    } else {
        ListSink sink(app, width, iconsOnly);
        FillRibbon(app, session, sink, false);
    }
    ImGui::Dummy({width, app.ui.S(8)});
}

void DrawToolStrip(AppContext& app, ImVec2 viewMin, ImVec2 viewMax, bool blockOnRight) {
    MobileState& m = app.shell.mobile;
    static std::string shownGroup;   // for the log: the strip opened / closed
    if (m.flyGroup != shownGroup && !(m.flyGroup.empty() && shownGroup.empty())) {
        if (!m.flyGroup.empty() && !m.flyItems.empty()) log::Info("Strip: open {} ({} commands)", m.flyGroup, m.flyItems.size());
        else if (m.flyGroup.empty()) log::Info("Strip: closed");
        shownGroup = m.flyGroup;
    }
    if (m.flyGroup.empty()) return;
    if (m.flyItems.empty()) {   // the group is not in block 2 any more (another tab)
        log::Info("Strip: no commands for {}, closed", m.flyGroup);
        m.flyGroup.clear();
        shownGroup.clear();
        return;
    }
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    // "Ящик" (mockup 6c, chosen 17.10): block 2 pulled out over the view — its colour,
    // no gap on its side, the group's name and ✕ on top. Level with the group's row,
    // from block 2 toward block 1 (never over it); the commands scroll sideways.
    const float headH = ui.S(30), tileW = ui.S(74), tileH = ui.S(66), gap = ui.S(4), pad = ui.S(8);
    const float h = headH + tileH + 2 * ui.S(6);
    const float y = std::clamp(m.flyY - ui.S(30), viewMin.y + ui.S(56), std::max(viewMin.y + ui.S(56), viewMax.y - h - ui.S(4)));
    const ImVec2 pos{viewMin.x, y}, size{viewMax.x - viewMin.x, h};
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{0, 0, 0, 0});
    const bool shown = ImGui::Begin("##tool-strip", nullptr,
                                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    bool inside = false;
    if (shown) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 end{pos.x + size.x, pos.y + size.y};
        // Rounded only away from block 2; a soft shadow on that side and below.
        const ImDrawFlags farCorners = blockOnRight ? ImDrawFlags_RoundCornersLeft : ImDrawFlags_RoundCornersRight;
        const float r = ui.S(14);
        for (int i = 3; i >= 1; --i) {
            const float sp = ui.S(3.0f * static_cast<float>(i));
            dl->AddRectFilled({pos.x - (blockOnRight ? sp : 0), pos.y + sp * 0.6f}, {end.x + (blockOnRight ? 0 : sp), end.y + sp},
                              ui::ToU32(Color{0, 0, 0, 0.10f}), r + sp, farCorners);
        }
        dl->AddRectFilled(pos, end, ui::ToU32(c.panel), r, farCorners);
        // The outline, open on block 2's side: it reads as one panel with it.
        const float x0 = pos.x + 0.5f, x1 = end.x - 0.5f, y0 = pos.y + 0.5f, y1 = end.y - 0.5f;
        if (blockOnRight) {
            dl->PathLineTo({x1, y0});
            dl->PathArcTo({x0 + r, y0 + r}, r, -1.5707963f, -3.1415926f, 8);
            dl->PathArcTo({x0 + r, y1 - r}, r, 3.1415926f, 1.5707963f, 8);
            dl->PathLineTo({x1, y1});
        } else {
            dl->PathLineTo({x0, y0});
            dl->PathArcTo({x1 - r, y0 + r}, r, -1.5707963f, 0.0f, 8);
            dl->PathArcTo({x1 - r, y1 - r}, r, 0.0f, 1.5707963f, 8);
            dl->PathLineTo({x0, y1});
        }
        dl->PathStroke(ui::ToU32(c.divider), ImDrawFlags_None, ui.S(1));

        // The head: the group's name, ✕ on block 2's side.
        const std::string caps = Upper(m.flyGroup);
        dl->AddText(ui.fonts.strong, ui.S(10.5f), {blockOnRight ? pos.x + ui.S(14) : pos.x + ui.S(44), pos.y + (headH - ui.S(11)) / 2},
                    ui::ToU32(c.textMuted), caps.c_str());
        const float xs = ui.S(34);
        const ImVec2 xa{blockOnRight ? end.x - xs - ui.S(4) : pos.x + ui.S(4), pos.y}, xb{xa.x + xs, pos.y + headH};
        ImGui::SetCursorScreenPos(xa);
        const bool closeClicked = ImGui::InvisibleButton("##strip-close", {xb.x - xa.x, xb.y - xa.y});
        if (ImGui::IsItemActive()) dl->AddRectFilled({xa.x + ui.S(3), xa.y + ui.S(3)}, {xb.x - ui.S(3), xb.y - ui.S(3)}, ui::ToU32(c.pressed), ui.S(6));
        const float cs = ui.S(14);
        ui.icons.Draw(dl, "ui-close", {(xa.x + xb.x - cs) / 2, (xa.y + xb.y - cs) / 2}, cs, c.textSecondary);
        const bool closeHovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
        dl->AddLine({pos.x + (blockOnRight ? r : 0), pos.y + headH}, {end.x - (blockOnRight ? 0 : r), pos.y + headH}, ui::ToU32(c.divider), ui.S(1));

        // The row: one surface — a drag scrolls it, a tap runs the command under it.
        const ImVec2 rowA{pos.x, pos.y + headH}, rowB{end.x, end.y};
        const float rowW = rowB.x - rowA.x;
        const float content = pad * 2 + static_cast<float>(m.flyItems.size()) * (tileW + gap) - gap;
        const float maxScroll = std::max(0.0f, content - rowW);
        m.flyScroll = std::clamp(m.flyScroll, 0.0f, maxScroll);
        ImGui::SetCursorScreenPos(rowA);
        static float dragged = 0;
        const bool clicked = ImGui::InvisibleButton("##strip-surface", {rowW, rowB.y - rowA.y});
        if (ImGui::IsItemActivated()) dragged = 0;
        if (ImGui::IsItemActive()) {
            const float dx = ImGui::GetIO().MouseDelta.x;
            dragged += std::abs(dx);
            m.flyScroll = std::clamp(m.flyScroll + (blockOnRight ? dx : -dx), 0.0f, maxScroll);
        }
        const bool rowActive = ImGui::IsItemActive();
        inside = closeHovered || ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) || rowActive;
        // Laid out from block 2's side: its first command nearest block 2.
        const ImVec2 mouse = ImGui::GetMousePos();
        dl->PushClipRect(rowA, rowB, true);
        for (std::size_t i = 0; i < m.flyItems.size(); ++i) {
            const MobileState::FlyItem& it = m.flyItems[i];
            const float off = pad + static_cast<float>(i) * (tileW + gap) - m.flyScroll;
            const float x = blockOnRight ? rowB.x - off - tileW : rowA.x + off;
            const ImVec2 a{x, rowA.y + ui.S(6)}, b{x + tileW, a.y + tileH};
            const Action* act = app.actions.Find(it.id);
            const bool enabled = act && act->IsEnabled();
            const bool under = rowActive && mouse.x >= a.x && mouse.x < b.x && dragged < ui.S(8);
            dl->AddRectFilled(a, b, ui::ToU32(under ? c.pressed : it.active ? c.accentSoft : c.field), ui.S(10));
            const float is = ui.S(26);
            ui.icons.Draw(dl, it.icon, {x + (tileW - is) / 2, a.y + ui.S(9)}, is, enabled ? Color{1, 1, 1, 1} : Color{1, 1, 1, 0.4f});
            const float fs = ui.S(10.5f);
            ui::FittedText(dl, ui.fonts.caption, fs,
                           {x + std::max(ui.S(4), (tileW - ui.fonts.caption->CalcTextSizeA(fs, FLT_MAX, 0, it.label.c_str()).x) / 2),
                            a.y + ui.S(42)},
                           x + tileW - ui.S(3), ui::ToU32(!enabled ? c.textDisabled : it.active ? c.text : c.textSecondary), it.label.c_str());
            if (clicked && dragged < ui.S(8) && mouse.x >= a.x && mouse.x < b.x && enabled) {
                const std::string run = it.id;
                app.Later([&app, run] { app.actions.Run(run); });
                m.flyGroup.clear();   // the command chosen: the strip goes
            }
        }
        // More this way: the far end fades into the panel (and the near end once scrolled).
        const float fw = ui.S(40);
        const ImU32 solid = ui::ToU32(c.panel), clear = ui::ToU32(c.panel.WithAlpha(0.0f));
        const bool moreFar = m.flyScroll < maxScroll - 0.5f, moreNear = m.flyScroll > 0.5f;
        const auto fade = [&](bool atLeft) {
            const ImVec2 fa{atLeft ? rowA.x : rowB.x - fw, rowA.y}, fb{atLeft ? rowA.x + fw : rowB.x, rowB.y};
            dl->AddRectFilledMultiColor(fa, fb, atLeft ? solid : clear, atLeft ? clear : solid, atLeft ? clear : solid, atLeft ? solid : clear);
        };
        if (moreFar) fade(blockOnRight);
        if (moreNear) fade(!blockOnRight);
        dl->PopClipRect();
        if (closeClicked) m.flyGroup.clear();
    }
    ImGui::End();
    // A tap elsewhere (but not on block 2, whose "›" toggles it) closes it.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !inside && !m.rBlock2.Contains(ImGui::GetMousePos())) m.flyGroup.clear();
}

} // namespace bz::app::mobile
