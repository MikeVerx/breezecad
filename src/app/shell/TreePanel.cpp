// "Дерево построения": the part, its origin, the features (sketches nested
// under the operations that use them, as in Inventor) and the end-of-part
// marker, which can be dragged to roll the part back.

#include "app/Format.h"
#include "app/Operations.h"
#include "app/shell/Shell.h"
#include "core/I18n.h"
#include "document/CoilFeature.h"
#include "document/Commands.h"
#include "document/EdgeFeature.h"
#include "document/ExtrudeFeature.h"
#include "document/HoleFeature.h"
#include "document/LoftFeature.h"
#include "document/Origin.h"
#include "document/PatternFeature.h"
#include "document/RevolveFeature.h"
#include "document/SectionFeature.h"
#include "document/ShellFeature.h"
#include "document/WorkPointFeature.h"
#include "document/SketchFeature.h"
#include "document/SweepFeature.h"
#include "document/ThreadFeature.h"
#include "document/WorkAxisFeature.h"
#include "document/WorkPlaneFeature.h"
#include "app/assembly/AssemblyUi.h"
#include "assembly/AssemblyFile.h"
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
    bool        hideable = false;   ///< has an eye at the right: hide or show it
    bool        hidden = false;
};

/// Draws a tree row; returns its interaction.
struct RowResult {
    bool clicked = false;
    bool doubleClicked = false;
    bool caretClicked = false;
    bool eyeClicked = false;
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

    // Right side: the eye, status icon and meta text.
    float right = p.x + width - ui.S(8);
    if (style.hideable) {
        right -= ui.S(16);
        // Always there for a hidden item, so it can be found again; else on hover.
        if (style.hidden || r.hovered)
            ui.icons.Draw(dl, style.hidden ? "ui-eye-off" : "ui-eye", {right, p.y + (h - ui.S(15)) / 2}, ui.S(15),
                          style.hidden ? c.textMuted : c.textSecondary);
        const ImVec2 mouse = ImGui::GetMousePos();
        if (r.clicked && mouse.x >= right - ui.S(4) && mouse.x <= right + ui.S(20)) {
            r.eyeClicked = true;
            r.clicked = false;
        }
        right -= ui.S(6);
    }
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
        if (ex->extent == doc::ExtentType::ThroughAll) return Tr("насквозь");
        if (ex->extent == doc::ExtentType::Asymmetric)   // 10 / 5 мм
            return FormatValue(ex->distance.value, Quantity::Length, d.units) + " / " +
                   FormatValue(ex->distance2.value, Quantity::Length, d.units) + " " + UnitLabel(Quantity::Length, d.units);
        return FormatValue(ex->distance.value, Quantity::Length, d.units) + " " + UnitLabel(Quantity::Length, d.units);
    }
    if (const auto* fillet = dynamic_cast<const doc::FilletFeature*>(&f))
        return "R" + FormatValue(fillet->radius.value, Quantity::Length, d.units);
    if (const auto* chamfer = dynamic_cast<const doc::ChamferFeature*>(&f)) {
        const std::string first = FormatValue(chamfer->distance.value, Quantity::Length, d.units);
        switch (chamfer->mode) {
            case doc::ChamferMode::EqualDistance: return first + "\xC3\x97" + "45\xC2\xB0";   // 2×45°
            case doc::ChamferMode::DistanceAngle:
                return first + "\xC3\x97" + FormatValue(chamfer->angle.value, Quantity::Angle, d.units) + "\xC2\xB0";
            case doc::ChamferMode::TwoDistances:
                return first + "\xC3\x97" + FormatValue(chamfer->distance2.value, Quantity::Length, d.units);   // 2×5
        }
    }
    if (const auto* shell = dynamic_cast<const doc::ShellFeature*>(&f))
        return FormatValue(shell->thickness.value, Quantity::Length, d.units) + " " + UnitLabel(Quantity::Length, d.units);
    if (const auto* hole = dynamic_cast<const doc::HoleFeature*>(&f)) {
        std::string meta = "\xC3\x98" + FormatValue(hole->diameter.value, Quantity::Length, d.units);   // Ø6.6
        if (hole->centres.size() > 1) meta += " \xC3\x97" + std::to_string(hole->centres.size());
        return meta;
    }
    if (const auto* thread = dynamic_cast<const doc::ThreadFeature*>(&f); thread && thread->site)
        return "M" + FormatValue(thread->site->nominal, Quantity::Length, d.units) + "\xC3\x97" +
               FormatValue(thread->pitch.value, Quantity::Length, d.units);   // M8×1.25
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

/// What the eye can hide: a body (of the operation that made it), a sketch,
/// a work plane, axis or point. Other operations change a body, not own one.
bool Hideable(const Session& session, const doc::Feature& f) {
    if (dynamic_cast<const doc::SketchFeature*>(&f) || dynamic_cast<const doc::WorkPlaneFeature*>(&f) ||
        dynamic_cast<const doc::WorkAxisFeature*>(&f) || dynamic_cast<const doc::WorkPointFeature*>(&f))
        return true;
    for (const kernel::BodyInfo& b : session.model.Bodies())
        if (b.id == f.id) return true;
    return f.hidden;   // hidden while it made a body: it can always be shown again
}

void ToggleHidden(Session& session, ObjectId id) {
    const doc::Feature* f = session.doc.Find(id);
    if (!f) return;
    session.Execute(doc::SetHidden(id, !f->hidden, f->hidden ? Trf("Показать {}", f->name) : Trf("Скрыть {}", f->name)));
}

void ContextMenu(AppContext& app, Session& session, const doc::Feature& f, std::size_t index) {
    ui::Ui& ui = app.ui;
    ui::MenuStyle style(ui);   // before Begin: must outlive the End call
    if (!ImGui::BeginPopupContextItem("##feature-menu")) return;
    const ObjectId id = f.id;
    if (ui::MenuItem(ui, Tr("Редактировать"), nullptr, "ui-edit")) app.Later([&app, &session, id] { EditFeature(app, session, id); });
    if (const auto* op = dynamic_cast<const doc::SketchSolidFeature*>(&f)) {
        const ObjectId sk = op->sketch;
        if (ui::MenuItem(ui, Tr("Редактировать эскиз"), nullptr, "tree-sketch"))
            app.Later([&app, &session, sk] { EditFeature(app, session, sk); });
    }
    if (ui::MenuItem(ui, Tr("Переименовать"), "F2", "ui-rename")) {
        app.shell.renaming = id;
        app.shell.renameText = f.name;
        app.shell.renameFocus = true;
    }
    if (Hideable(session, f) &&
        ui::MenuItem(ui, f.hidden ? Tr("Показать") : Tr("Скрыть"), nullptr, f.hidden ? "ui-eye" : "ui-eye-off"))
        app.Later([&session, id] { ToggleHidden(session, id); });
    if (ui::MenuItem(ui, f.suppressed ? Tr("Включить") : Tr("Подавить"), nullptr, "ui-suppress")) {
        app.Later([&session, id] {
            const doc::Feature* current = session.doc.Find(id);
            if (!current) return;
            auto edited = current->Clone();
            edited->suppressed = !current->suppressed;
            const std::string label = current->suppressed ? Trf("Включить {}", current->name) : Trf("Подавить {}", current->name);
            session.Execute(doc::ReplaceFeature(std::move(edited), label));
        });
    }
    if (ui::MenuItem(ui, Tr("Переместить маркер конца сюда"), nullptr, "ui-marker"))
        app.Later([&session, index] { session.Execute(doc::SetEndMarker(session.doc, index + 1)); });
    ImGui::Separator();
    if (ui::MenuItem(ui, Tr("Удалить"), "Del", "ui-trash")) app.Later([&app, &session, id] { DeleteFeature(app, session, id); });
    ImGui::EndPopup();
}

} // namespace

void DrawTreePanel(AppContext& app, Session& session, float width) {
    if (session.assembly) {
        DrawAssemblyTree(app, session, width);
        return;
    }
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    doc::Document& d = session.doc;

    // Header with a name filter.
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(40);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddText(ui.fonts.strong, ui.S(13), {p.x + ui.S(14), p.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(c.text),
                    Tr("Дерево построения"));
        dl->AddLine({p.x, p.y + h - 1}, {p.x + width, p.y + h - 1}, ui::ToU32(c.dividerSoft));
        ImGui::SetCursorScreenPos({p.x + width - ui.S(38), p.y + ui.S(7)});
        if (ui::IconButton(ui, "##tree-search", "ui-search", 26, 15, Tr("Поиск в дереве"), true, app.shell.treeSearch)) {
            app.shell.treeSearch = !app.shell.treeSearch;
            app.shell.treeFilter.clear();
        }
        ImGui::SetCursorScreenPos({p.x, p.y + h});
        if (app.shell.treeSearch) {
            ImGui::SetCursorScreenPos({p.x + ui.S(8), p.y + h + ui.S(6)});
            ui::TextField(ui, "##filter", app.shell.treeFilter, width - ui.S(16), Tr("Имя операции"), ImGui::IsWindowAppearing());
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
        const RowResult r = Row(app, "##origin", Tr("Начало координат"), style, rowW);
        if (r.clicked || r.caretClicked) app.shell.originExpanded = !app.shell.originExpanded;
        if (app.shell.originExpanded) {
            const struct {
                ObjectId    id;
                const char* name;
                const char* icon;
            } items[] = {{doc::origin::kPlaneXY, Tr("Плоскость XY"), "tree-plane"}, {doc::origin::kPlaneXZ, Tr("Плоскость XZ"), "tree-plane"},
                         {doc::origin::kPlaneYZ, Tr("Плоскость YZ"), "tree-plane"}, {doc::origin::kAxisX, Tr("Ось X"), "tree-axis"},
                         {doc::origin::kAxisY, Tr("Ось Y"), "tree-axis"},           {doc::origin::kAxisZ, Tr("Ось Z"), "tree-axis"},
                         {doc::origin::kPoint, Tr("Центральная точка"), "tree-point"}};
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
        style.statusText = Tr(f.status.message);
        style.hideable = Hideable(session, f);
        style.hidden = f.hidden;

        ImGui::PushID(static_cast<int>(f.id.value));
        if (app.shell.renaming == f.id) {
            // Inline rename: Enter or clicking away applies, Esc cancels.
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ui.S(40) + ui.S(16) * static_cast<float>(depth));
            ui::TextField(ui, "##rename", app.shell.renameText, rowW - ui.S(48), nullptr, app.shell.renameFocus);
            app.shell.renameFocus = false;
            const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape);
            if (escape) {
                app.shell.renaming = {};
            } else if (ImGui::IsItemDeactivated() || ui::EnterPressed()) {
                const ObjectId id = f.id;
                const std::string name = app.shell.renameText;
                app.shell.renaming = {};
                app.Later([&session, id, name] {
                    const doc::Feature* current = session.doc.Find(id);
                    if (!current || name.empty() || name == current->name) return;
                    auto edited = current->Clone();
                    edited->name = name;
                    session.Execute(doc::ReplaceFeature(std::move(edited), Tr("Переименование")));
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
        if (r.eyeClicked) {
            const ObjectId id = f.id;
            app.Later([&session, id] { ToggleHidden(session, id); });
        }
        if (r.doubleClicked && !r.eyeClicked) {
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
        const char* label = Tr("Конец детали");
        const float lw = ui.fonts.medium->CalcTextSizeA(ui.S(11), FLT_MAX, 0, label).x;
        const float lineEnd = p.x + rowW - lw - ui.S(16);
        dl->AddRectFilled({x0, p.y + h / 2 - ui.S(1)}, {lineEnd, p.y + h / 2 + ui.S(1)}, ui::ToU32(c.accent), ui.S(1));
        dl->AddText(ui.fonts.medium, ui.S(11), {lineEnd + ui.S(8), p.y + (h - ui.S(11)) / 2 - ui.S(1)}, ui::ToU32(c.accent), label);
        if (hovered) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            ui::ItemTooltip(ui, Tr("Перетащите, чтобы временно откатить деталь до нужной операции"));
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
        ImGui::TextColored(ui::ToVec4(c.textMuted), "%s",
                           Tr("Деталь пустая. Нажмите «Начать эскиз» на ленте (S) и выберите плоскость — с этого начинается любая деталь."));
        ImGui::PopTextWrapPos();
    }
    ImGui::EndGroup();
}

// --- assembly -------------------------------------------------------------------

const char* ConstraintIcon(assembly::ConstraintType type) {
    switch (type) {
        case assembly::ConstraintType::Mate:     return "a-mate";
        case assembly::ConstraintType::Insert:   return "a-insert";
        case assembly::ConstraintType::Angle:    return "a-angle";
        case assembly::ConstraintType::Tangent:  return "a-tangent";
        case assembly::ConstraintType::AxisMate: return "a-axis";
        case assembly::ConstraintType::Rotational:  return "j-rotational";
        case assembly::ConstraintType::Slider:      return "j-slider";
        case assembly::ConstraintType::Cylindrical: return "j-cylindrical";
        case assembly::ConstraintType::Planar:      return "j-planar";
        case assembly::ConstraintType::Ball:        return "j-ball";
        case assembly::ConstraintType::Rigid:       return "j-rigid";
    }
    return "a-mate";
}

void DrawAssemblyTree(AppContext& app, Session& session, float width) {
    if (session.assembly->creating.active) {
        DrawConstraintPanel(app, session, width);
        return;
    }
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    AssemblyState& st = *session.assembly;
    assembly::Assembly& a = st.assembly;
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(40);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddText(ui.fonts.strong, ui.S(13), {p.x + ui.S(14), p.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(c.text),
                    Tr("Дерево сборки"));
        dl->AddLine({p.x, p.y + h - 1}, {p.x + width, p.y + h - 1}, ui::ToU32(c.dividerSoft));
        ImGui::Dummy({width, h});
    }
    const float rowW = width - ui.S(12);
    ImGui::SetCursorPosX(ui.S(6));
    ImGui::BeginGroup();
    ImGui::Dummy({0, ui.S(4)});
    app.shell.hoveredTreeItem = {};
    {
        RowStyle style;
        style.icon = "tree-assembly";
        if (Row(app, "##assembly", session.Title(), style, rowW).clicked) st.selected = {};
    }

    // Components, each with the constraints that tie it.
    for (const assembly::Component& comp : a.components) {
        const ObjectId id = comp.id;
        if (a.PatternOfInstance(id)) continue;   // listed under its pattern
        RowStyle style;
        style.depth = 1;
        style.icon = "tree-component";
        style.selected = st.selected == id;
        style.muted = !comp.visible;
        const auto constraints = a.ConstraintsOf(id);
        style.caret = !constraints.empty();
        style.open = app.shell.expanded.contains(id);
        if (const std::string problem = st.geometry.ProblemOf(id); !problem.empty()) {
            style.statusIcon = "status-error";
            style.statusText = Tr(problem);
        } else if (comp.grounded) {
            style.meta = Tr("закреплён");
            style.statusText = Tr("Закреплён: зависимости и перетаскивание его не двигают");
        } else if (const auto f = st.freedom.find(id); f != st.freedom.end()) {
            // Remaining degrees of freedom, as Inventor shows them.
            style.meta = f->second.Total() == 0 ? std::string(Tr("определён")) : Trf("{} ст.св.", f->second.Total());
            style.statusText = Trf("Свободы: перемещений {}, вращений {}", f->second.translations, f->second.rotations);
        }
        ImGui::PushID(static_cast<int>(id.value));
        const RowResult r = Row(app, "##comp", comp.name, style, rowW);
        if (r.hovered) app.shell.hoveredTreeItem = id;
        if (r.caretClicked) {
            if (style.open) app.shell.expanded.erase(id);
            else app.shell.expanded.insert(id);
        }
        if (r.clicked) st.selected = id;
        if (r.doubleClicked) {
            const std::filesystem::path file = assembly::ResolvePartPath(session.path, comp.path);
            app.Later([&app, file] { OpenDocument(app, file); });
        }
        {
            ui::MenuStyle menu(ui);
            if (ImGui::BeginPopupContextItem("##comp-menu")) {
                st.selected = id;
                if (ui::MenuItem(ui, Tr("Открыть деталь"), nullptr, "ui-open")) {
                    const std::filesystem::path file = assembly::ResolvePartPath(session.path, comp.path);
                    app.Later([&app, file] { OpenDocument(app, file); });
                }
                if (ui::MenuItem(ui, comp.grounded ? Tr("Освободить") : Tr("Закрепить"), nullptr, "ui-lock"))
                    app.Later([&app] { ToggleGrounded(app); });
                if (ui::MenuItem(ui, comp.visible ? Tr("Скрыть") : Tr("Показать"), nullptr, comp.visible ? "ui-eye-off" : "ui-eye"))
                    app.Later([&st, id] {
                        if (assembly::Component* cc = st.assembly.FindComponent(id)) {
                            st.Checkpoint(cc->visible ? N_("Скрыть") : N_("Показать"));
                            cc->visible = !cc->visible;
                            st.assembly.Touch();
                        }
                    });
                ImGui::Separator();
                if (ui::MenuItem(ui, Tr("Удалить"), "Del", "ui-trash")) app.Later([&app, &session] { AssemblyDelete(app, session); });
                ImGui::EndPopup();
            }
        }
        ImGui::PopID();
        if (!style.open) continue;
        for (const assembly::Constraint* con : constraints) {
            RowStyle cs;
            cs.depth = 2;
            cs.icon = ConstraintIcon(con->type);
            cs.selected = st.selected == con->id;
            cs.muted = con->suppressed;
            const ObjectId other = con->first.component == id ? con->second.component : con->first.component;
            if (const assembly::Component* o = a.FindComponent(other)) cs.meta = o->name;
            ImGui::PushID(static_cast<int>(con->id.value));
            if (Row(app, "##in-comp", con->name, cs, rowW).clicked) st.selected = con->id;
            ImGui::PopID();
        }
    }

    // Component patterns: one row each, the instances under it.
    for (const assembly::ComponentPattern& pat : a.patterns) {
        const ObjectId id = pat.id;
        RowStyle style;
        style.depth = 1;
        style.icon = pat.kind == assembly::PatternKind::Circular ? "tree-pattern-circular" : "tree-pattern-rect";
        style.selected = st.selected == id;
        style.meta = Trf("{} шт.", pat.instances.size() + 1);
        style.caret = !pat.instances.empty();
        style.open = app.shell.expanded.contains(id);
        ImGui::PushID(static_cast<int>(id.value));
        const RowResult r = Row(app, "##pattern", pat.name, style, rowW);
        if (r.caretClicked) {
            if (style.open) app.shell.expanded.erase(id);
            else app.shell.expanded.insert(id);
        }
        if (r.clicked) st.selected = id;
        {
            ui::MenuStyle menu(ui);
            if (ImGui::BeginPopupContextItem("##pattern-menu")) {
                st.selected = id;
                if (ui::MenuItem(ui, Tr("Удалить массив"), "Del", "ui-trash"))
                    app.Later([&app, &session] { AssemblyDelete(app, session); });
                ImGui::EndPopup();
            }
        }
        if (style.open)
            for (std::size_t i = 0; i < pat.instances.size(); ++i)
                for (ObjectId cid : pat.instances[i]) {
                    const assembly::Component* comp = a.FindComponent(cid);
                    if (!comp) continue;
                    RowStyle cs;
                    cs.depth = 2;
                    cs.icon = "tree-component";
                    cs.selected = st.selected == cid;
                    cs.muted = !comp->visible;
                    cs.meta = Trf("№ {}", i + 2);
                    ImGui::PushID(static_cast<int>(cid.value));
                    const RowResult rr = Row(app, "##instance", comp->name, cs, rowW);
                    if (rr.hovered) app.shell.hoveredTreeItem = cid;
                    if (rr.clicked) st.selected = cid;
                    ImGui::PopID();
                }
        ImGui::PopID();
    }

    // All constraints.
    if (!a.constraints.empty()) {
        ImGui::Dummy({0, ui.S(6)});
        ui::FieldLabel(ui, Tr("Зависимости"));
        for (assembly::Constraint& con : a.constraints) {
            RowStyle style;
            style.depth = 1;
            style.icon = ConstraintIcon(con.type);
            style.selected = st.selected == con.id;
            style.muted = con.suppressed;
            switch (con.state) {
                case assembly::ConstraintState::Failed:
                    style.statusIcon = "status-error";
                    style.statusText = Tr("Не выполняется: противоречит другим зависимостям или геометрии");
                    break;
                case assembly::ConstraintState::Redundant:
                    style.statusIcon = "status-warning";
                    style.statusText = Tr("Лишняя: то же уже задано другими зависимостями");
                    break;
                default: break;
            }
            ImGui::PushID(static_cast<int>(con.id.value));
            const RowResult r = Row(app, "##con", con.name, style, rowW);
            if (r.clicked) st.selected = con.id;
            {
                ui::MenuStyle menu(ui);
                if (ImGui::BeginPopupContextItem("##con-menu")) {
                    st.selected = con.id;
                    const ObjectId cid = con.id;
                    if (ui::MenuItem(ui, con.suppressed ? Tr("Включить") : Tr("Подавить"), nullptr,
                                     con.suppressed ? "ui-eye" : "ui-eye-off"))
                        app.Later([&st, cid] {
                            if (assembly::Constraint* cc = st.assembly.FindConstraint(cid)) {
                                st.Checkpoint(cc->suppressed ? N_("Включить") : N_("Подавить"));
                                cc->suppressed = !cc->suppressed;
                                st.assembly.Touch();
                            }
                        });
                    ImGui::Separator();
                    if (ui::MenuItem(ui, Tr("Удалить"), "Del", "ui-trash")) app.Later([&app, &session] { AssemblyDelete(app, session); });
                    ImGui::EndPopup();
                }
            }
            ImGui::PopID();
        }
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
    if (dynamic_cast<const doc::WorkPlaneFeature*>(&f)) return "tree-plane";
    if (dynamic_cast<const doc::WorkAxisFeature*>(&f)) return "tree-axis";
    if (dynamic_cast<const doc::WorkPointFeature*>(&f)) return "tree-point";
    if (const auto* p = dynamic_cast<const doc::PatternFeature*>(&f))
        return p->kind == doc::PatternKind::Circular ? "tree-pattern-circular" : "tree-pattern-rect";
    if (dynamic_cast<const doc::MirrorFeature*>(&f)) return "tree-mirror";
    if (dynamic_cast<const doc::LoftFeature*>(&f)) return "tree-loft";
    if (dynamic_cast<const doc::SweepFeature*>(&f)) return "tree-sweep";
    if (dynamic_cast<const doc::CoilFeature*>(&f)) return "tree-coil";
    if (dynamic_cast<const doc::ThreadFeature*>(&f)) return "tree-thread";
    if (dynamic_cast<const doc::SectionFeature*>(&f)) return "tree-cut-section";
    if (dynamic_cast<const doc::ShellFeature*>(&f)) return "tree-shell";
    return "tree-unknown";
}

} // namespace bz::app
