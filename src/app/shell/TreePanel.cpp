// "Дерево построения": the part, its origin, the features (sketches nested
// under the operations that use them, as in Inventor) and the end-of-part
// marker, which can be dragged to roll the part back.

#include "app/Format.h"
#include "app/Operations.h"
#include "app/shell/Shell.h"
#include "document/Commands.h"
#include "document/DraftFeature.h"
#include "document/EdgeFeature.h"
#include "document/ExtrudeFeature.h"
#include "document/HoleFeature.h"
#include "document/LoftFeature.h"
#include "document/PatternFeature.h"
#include "document/RevolveFeature.h"
#include "document/ShellFeature.h"
#include "document/WorkPlaneFeature.h"
#include "document/Origin.h"
#include "document/SketchFeature.h"
#include "document/SweepFeature.h"
#include "ui/Menus.h"
#include "ui/Widgets.h"

#include <unordered_map>

namespace bz::app {
namespace {

constexpr const char* kMarkerPayload = "BZ_END_MARKER";

struct RowStyle {
    int         depth = 0;
    bool        caret = false;
    bool        open = false;
    bool        selected = false;
    bool        muted = false;
    const char* icon = "";
    std::string meta;
    const char* statusIcon = nullptr;
    std::string statusText;
};

/// Draws a tree row; returns its interaction.
struct RowResult {
    bool clicked = false;
    bool doubleClicked = false;
    bool caretClicked = false;
    bool hovered = false;
};

RowResult Row(AppContext& app, const char* id, const std::string& name, const RowStyle& style, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    RowResult r;
    const float h = ui.S(30);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    r.clicked = ImGui::InvisibleButton("##row", {width, h}, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    r.hovered = ImGui::IsItemHovered();
    r.doubleClicked = r.hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (style.selected) {
        dl->AddRectFilled(p, {p.x + width, p.y + h}, ui::ToU32(c.selectedRow), ui.S(6));
        dl->AddRectFilled(p, {p.x + ui.S(2), p.y + h}, ui::ToU32(c.accent));
    } else if (r.hovered) {
        dl->AddRectFilled(p, {p.x + width, p.y + h}, ui::ToU32(c.hoverRow), ui.S(6));
    }

    float x = p.x + ui.S(8) + ui.S(16) * static_cast<float>(style.depth);
    if (style.caret) {
        ui.icons.Draw(dl, style.open ? "ui-chevron-down" : "ui-chevron-right", {x, p.y + (h - ui.S(11)) / 2}, ui.S(11), c.textMuted);
        const ImVec2 mouse = ImGui::GetMousePos();
        if (r.clicked && mouse.x >= x - ui.S(4) && mouse.x <= x + ui.S(14)) {
            r.caretClicked = true;
            r.clicked = false;
        }
    }
    x += ui.S(16);
    if (*style.icon) {
        ui.icons.Draw(dl, style.icon, {x, p.y + (h - ui.S(16)) / 2}, ui.S(16), {1, 1, 1, style.muted ? 0.45f : 1.0f});
        x += ui.S(24);
    }

    // Right side: status icon and meta text.
    float right = p.x + width - ui.S(8);
    if (style.statusIcon) {
        right -= ui.S(16);
        ui.icons.Draw(dl, style.statusIcon, {right, p.y + (h - ui.S(15)) / 2}, ui.S(15), {1, 1, 1, 1});
        right -= ui.S(6);
    }
    if (!style.meta.empty()) {
        const float mw = ui.fonts.mono->CalcTextSizeA(ui.S(11), FLT_MAX, 0, style.meta.c_str()).x;
        right -= mw;
        dl->AddText(ui.fonts.mono, ui.S(11), {right, p.y + (h - ui.S(11)) / 2}, ui::ToU32(c.textMuted), style.meta.c_str());
        right -= ui.S(8);
    }
    ImFont* font = style.selected ? ui.fonts.medium : ui.fonts.body;
    const Color tc = style.muted ? c.textDisabled : c.text;
    const ImVec4 clip{x, p.y, right, p.y + h};
    dl->AddText(font, ui.S(13), {x, p.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(tc), name.c_str(), nullptr, 0, &clip);

    if (r.hovered && !style.statusText.empty()) ui::ItemTooltip(ui, style.statusText.c_str());
    return r;
}

std::string FeatureMeta(const doc::Feature& f, const doc::Document& d) {
    if (const auto* ex = dynamic_cast<const doc::ExtrudeFeature*>(&f)) {
        if (ex->extent == doc::ExtentType::ThroughAll) return "насквозь";
        return FormatValue(ex->distance.value, Quantity::Length, d.units) + " " + UnitLabel(Quantity::Length, d.units);
    }
    if (const auto* fillet = dynamic_cast<const doc::FilletFeature*>(&f))
        return "R" + FormatValue(fillet->radius.value, Quantity::Length, d.units);
    if (const auto* chamfer = dynamic_cast<const doc::ChamferFeature*>(&f))
        return FormatValue(chamfer->distance.value, Quantity::Length, d.units) + "\xC3\x97" + "45\xC2\xB0";
    if (const auto* hole = dynamic_cast<const doc::HoleFeature*>(&f)) {
        std::string meta = "\xC3\x98" + FormatValue(hole->diameter.value, Quantity::Length, d.units);   // Ø6.6
        if (hole->centres.size() > 1) meta += " \xC3\x97" + std::to_string(hole->centres.size());
        return meta;
    }
    if (const auto* shell = dynamic_cast<const doc::ShellFeature*>(&f))
        return FormatValue(shell->thickness.value, Quantity::Length, d.units) + " " + UnitLabel(Quantity::Length, d.units);
    if (const auto* wp = dynamic_cast<const doc::WorkPlaneFeature*>(&f))
        return (wp->reversed ? "\xE2\x88\x92" : "") + FormatValue(wp->offset.value, Quantity::Length, d.units) + " " +
               UnitLabel(Quantity::Length, d.units);
    if (const auto* p = dynamic_cast<const doc::PatternFeature*>(&f)) {
        const auto copies = p->Copies();
        return copies ? "\xC3\x97" + std::to_string(copies->size() + 1) : std::string();   // ×N
    }
    if (const auto* rev = dynamic_cast<const doc::RevolveFeature*>(&f)) {
        if (rev->extent == doc::RevolveExtent::Full) return "360\xC2\xB0";
        return FormatValue(rev->angle.value, Quantity::Angle, d.units) + UnitLabel(Quantity::Angle, d.units);
    }
    if (const auto* sk = dynamic_cast<const doc::SketchFeature*>(&f); sk && sk->analysis.degreesOfFreedom > 0)
        return "\xE2\x89\x88" + std::to_string(sk->analysis.degreesOfFreedom);   // ≈N: not fully defined
    return {};
}

void ContextMenu(AppContext& app, Session& session, const doc::Feature& f, std::size_t index) {
    ui::Ui& ui = app.ui;
    ui::MenuStyle style(ui);   // before Begin: must outlive the End call
    if (!ImGui::BeginPopupContextItem("##feature-menu")) return;
    const ObjectId id = f.id;
    if (ui::MenuItem(ui, "Редактировать", nullptr, "ui-edit")) app.Later([&app, &session, id] { EditFeature(app, session, id); });
    if (const auto* op = dynamic_cast<const doc::SketchSolidFeature*>(&f)) {
        const ObjectId sk = op->sketch;
        if (ui::MenuItem(ui, "Редактировать эскиз", nullptr, "tree-sketch"))
            app.Later([&app, &session, sk] { EditFeature(app, session, sk); });
    }
    if (ui::MenuItem(ui, "Переименовать", "F2", "ui-rename")) {
        app.shell.renaming = id;
        app.shell.renameText = f.name;
        app.shell.renameFocus = true;
    }
    if (ui::MenuItem(ui, f.suppressed ? "Включить" : "Подавить", nullptr, f.suppressed ? "ui-eye" : "ui-eye-off")) {
        app.Later([&session, id] {
            const doc::Feature* current = session.doc.Find(id);
            if (!current) return;
            auto edited = current->Clone();
            edited->suppressed = !current->suppressed;
            const std::string label = (current->suppressed ? "Включить " : "Подавить ") + current->name;
            session.Execute(doc::ReplaceFeature(std::move(edited), label));
        });
    }
    if (ui::MenuItem(ui, "Переместить маркер конца сюда", nullptr, "ui-marker"))
        app.Later([&session, index] { session.Execute(doc::SetEndMarker(session.doc, index + 1)); });
    ImGui::Separator();
    if (ui::MenuItem(ui, "Удалить", "Del", "ui-trash")) app.Later([&app, &session, id] { DeleteFeature(app, session, id); });
    ImGui::EndPopup();
}

} // namespace

void DrawTreePanel(AppContext& app, Session& session, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    doc::Document& d = session.doc;

    // Header with a name filter.
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(40);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddText(ui.fonts.strong, ui.S(13), {p.x + ui.S(14), p.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(c.text),
                    "Дерево построения");
        dl->AddLine({p.x, p.y + h - 1}, {p.x + width, p.y + h - 1}, ui::ToU32(c.dividerSoft));
        ImGui::SetCursorScreenPos({p.x + width - ui.S(38), p.y + ui.S(7)});
        if (ui::IconButton(ui, "##tree-search", "ui-search", 26, 15, "Поиск в дереве", true, app.shell.treeSearch)) {
            app.shell.treeSearch = !app.shell.treeSearch;
            app.shell.treeFilter.clear();
        }
        ImGui::SetCursorScreenPos({p.x, p.y + h});
        if (app.shell.treeSearch) {
            ImGui::SetCursorScreenPos({p.x + ui.S(8), p.y + h + ui.S(6)});
            ui::TextField(ui, "##filter", app.shell.treeFilter, width - ui.S(16), "Имя операции", ImGui::IsWindowAppearing());
            ImGui::Dummy({0, ui.S(4)});
        }
    }

    const float rowW = width - ui.S(12);
    ImGui::SetCursorPosX(ui.S(6));
    ImGui::BeginGroup();
    ImGui::Dummy({0, ui.S(4)});
    app.shell.hoveredTreeItem = {};

    // Part.
    {
        RowStyle style;
        style.icon = "tree-part";
        Row(app, "##part", session.Title(), style, rowW);
    }

    // Origin folder.
    {
        RowStyle style;
        style.depth = 1;
        style.caret = true;
        style.open = app.shell.originExpanded;
        style.icon = "tree-origin";
        const RowResult r = Row(app, "##origin", "Начало координат", style, rowW);
        if (r.clicked || r.caretClicked) app.shell.originExpanded = !app.shell.originExpanded;
        if (app.shell.originExpanded) {
            const struct {
                ObjectId    id;
                const char* name;
                const char* icon;
            } items[] = {{doc::origin::kPlaneXY, "Плоскость XY", "tree-plane"}, {doc::origin::kPlaneXZ, "Плоскость XZ", "tree-plane"},
                         {doc::origin::kPlaneYZ, "Плоскость YZ", "tree-plane"}, {doc::origin::kAxisX, "Ось X", "tree-axis"},
                         {doc::origin::kAxisY, "Ось Y", "tree-axis"},           {doc::origin::kAxisZ, "Ось Z", "tree-axis"},
                         {doc::origin::kPoint, "Центральная точка", "tree-point"}};
            for (const auto& item : items) {
                RowStyle s;
                s.depth = 2;
                s.icon = item.icon;
                s.selected = session.selectedFeature == item.id;
                const RowResult rr = Row(app, item.name, item.name, s, rowW);
                if (rr.hovered) app.shell.hoveredTreeItem = item.id;
                if (rr.clicked) session.selectedFeature = item.id;
                if (rr.doubleClicked && doc::origin::PlaneFrame(item.id))
                    app.Later([&app] { StartSketch(app); });
            }
        }
    }

    // Which sketch each operation uses: those sketches nest under it.
    std::unordered_map<ObjectId, ObjectId> consumedBy;
    for (const auto& f : d.Features())
        if (const auto* op = dynamic_cast<const doc::SketchSolidFeature*>(f.get()))
            if (!consumedBy.count(op->sketch)) consumedBy[op->sketch] = op->id;

    const std::string& filter = app.shell.treeFilter;
    const auto draw = [&](const doc::Feature& f, std::size_t index, int depth, bool hasChildren) {
        if (!filter.empty() && f.name.find(filter) == std::string::npos) return false;
        RowStyle style;
        style.depth = depth;
        style.caret = hasChildren;
        style.open = app.shell.expanded.count(f.id) > 0;
        style.icon = FeatureIcon(f);
        style.selected = session.selectedFeature == f.id || (session.mode == EditMode::Sketch && session.activeSketch == f.id);
        style.muted = !d.IsActive(index) || f.suppressed;
        style.meta = FeatureMeta(f, d);
        if (f.status.state == doc::FeatureState::Error) style.statusIcon = "status-error";
        if (f.status.state == doc::FeatureState::Warning) style.statusIcon = "status-warning";
        style.statusText = f.status.message;

        ImGui::PushID(static_cast<int>(f.id.value));
        if (app.shell.renaming == f.id) {
            // Inline rename: Enter or clicking away applies, Esc cancels.
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ui.S(40) + ui.S(16) * static_cast<float>(depth));
            ui::TextField(ui, "##rename", app.shell.renameText, rowW - ui.S(48), nullptr, app.shell.renameFocus);
            app.shell.renameFocus = false;
            const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape);
            if (escape) {
                app.shell.renaming = {};
            } else if (ImGui::IsItemDeactivated() || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                const ObjectId id = f.id;
                const std::string name = app.shell.renameText;
                app.shell.renaming = {};
                app.Later([&session, id, name] {
                    const doc::Feature* current = session.doc.Find(id);
                    if (!current || name.empty() || name == current->name) return;
                    auto edited = current->Clone();
                    edited->name = name;
                    session.Execute(doc::ReplaceFeature(std::move(edited), "Переименование"));
                });
            }
            ImGui::Dummy({0, ui.S(2)});
            ImGui::PopID();
            return true;
        }
        const RowResult r = Row(app, "##feature", f.name, style, rowW);
        ContextMenu(app, session, f, index);
        // Rows accept the dragged end-of-part marker.
        if (ImGui::BeginDragDropTarget()) {
            if (ImGui::AcceptDragDropPayload(kMarkerPayload)) app.Later([&session, index] { session.Execute(doc::SetEndMarker(session.doc, index + 1)); });
            ImGui::EndDragDropTarget();
        }
        if (r.caretClicked) {
            if (style.open) app.shell.expanded.erase(f.id);
            else app.shell.expanded.insert(f.id);
        }
        if (r.clicked) session.selectedFeature = f.id;
        if (r.doubleClicked) {
            const ObjectId id = f.id;
            app.Later([&app, &session, id] { EditFeature(app, session, id); });
        }
        if (r.hovered && ImGui::IsKeyPressed(ImGuiKey_F2)) {
            app.shell.renaming = f.id;
            app.shell.renameText = f.name;
            app.shell.renameFocus = true;
        }
        ImGui::PopID();
        return style.open;
    };

    const auto marker = [&] {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(22);
        ImGui::InvisibleButton("##end-marker", {rowW, h});
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool hovered = ImGui::IsItemHovered();
        const float x0 = p.x + ui.S(28);
        const char* label = "Конец детали";
        const float lw = ui.fonts.medium->CalcTextSizeA(ui.S(11), FLT_MAX, 0, label).x;
        const float lineEnd = p.x + rowW - lw - ui.S(16);
        dl->AddRectFilled({x0, p.y + h / 2 - ui.S(1)}, {lineEnd, p.y + h / 2 + ui.S(1)}, ui::ToU32(c.accent), ui.S(1));
        dl->AddText(ui.fonts.medium, ui.S(11), {lineEnd + ui.S(8), p.y + (h - ui.S(11)) / 2 - ui.S(1)}, ui::ToU32(c.accent), label);
        if (hovered) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            ui::ItemTooltip(ui, "Перетащите, чтобы временно откатить деталь до нужной операции");
        }
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
            ImGui::SetDragDropPayload(kMarkerPayload, nullptr, 0);
            ImGui::EndDragDropSource();
        }
        // Dropping on the marker itself or above the first feature: the very start.
        if (ImGui::BeginDragDropTarget()) {
            ImGui::AcceptDragDropPayload(kMarkerPayload);
            ImGui::EndDragDropTarget();
        }
    };

    if (d.EndMarker() == 0) marker();
    for (std::size_t i = 0; i < d.FeatureCount(); ++i) {
        const doc::Feature& f = *d.Features()[i];
        const auto consumer = consumedBy.find(f.id);
        const bool nested = consumer != consumedBy.end() && d.IndexOf(consumer->second) > static_cast<int>(i);
        if (!nested) {
            // Children: sketches this operation consumes.
            std::vector<std::size_t> children;
            for (std::size_t j = 0; j < d.FeatureCount(); ++j) {
                const auto it = consumedBy.find(d.Features()[j]->id);
                if (it != consumedBy.end() && it->second == f.id && j < i) children.push_back(j);
            }
            const bool open = draw(f, i, 1, !children.empty());
            if (open)
                for (std::size_t j : children) draw(*d.Features()[j], j, 2, false);
        }
        if (i + 1 == d.EndMarker()) marker();
    }

    if (d.FeatureCount() == 0) {
        ImGui::Dummy({0, ui.S(10)});
        ui::FontScope font(ui.fonts.bodySmall);
        ImGui::SetCursorPosX(ui.S(20));
        ImGui::PushTextWrapPos(width - ui.S(20));
        ImGui::TextColored(ui::ToVec4(c.textMuted),
                           "Деталь пустая. Нажмите «Начать эскиз» на ленте (S) и выберите плоскость — с этого начинается любая деталь.");
        ImGui::PopTextWrapPos();
    }
    ImGui::EndGroup();
}

// Tree icon of a feature, also used by panels that list features.
const char* FeatureIcon(const doc::Feature& f) {
    if (dynamic_cast<const doc::SketchFeature*>(&f)) return "tree-sketch";
    if (dynamic_cast<const doc::ExtrudeFeature*>(&f)) return "tree-extrude";
    if (dynamic_cast<const doc::RevolveFeature*>(&f)) return "tree-revolve";
    if (dynamic_cast<const doc::FilletFeature*>(&f)) return "tree-fillet";
    if (dynamic_cast<const doc::ChamferFeature*>(&f)) return "tree-chamfer";
    if (dynamic_cast<const doc::HoleFeature*>(&f)) return "tree-hole";
    if (dynamic_cast<const doc::ShellFeature*>(&f)) return "tree-shell";
    if (dynamic_cast<const doc::WorkPlaneFeature*>(&f)) return "tree-plane";
    if (const auto* p = dynamic_cast<const doc::PatternFeature*>(&f))
        return p->kind == doc::PatternKind::Circular ? "tree-pattern-circular" : "tree-pattern-rect";
    if (dynamic_cast<const doc::MirrorFeature*>(&f)) return "tree-mirror";
    if (dynamic_cast<const doc::LoftFeature*>(&f)) return "tree-loft";
    if (dynamic_cast<const doc::SweepFeature*>(&f)) return "tree-sweep";
    if (dynamic_cast<const doc::DraftFeature*>(&f)) return "tree-draft";
    return "tree-unknown";
}

} // namespace bz::app
