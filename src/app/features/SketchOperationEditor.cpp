#include "app/features/SketchOperationEditor.h"

#include "app/Format.h"
#include "app/sketch/SketchEditor.h"
#include "app/sketch/SketchOverlay.h"
#include "document/Commands.h"
#include "document/SketchFeature.h"
#include "ui/Widgets.h"

#include <array>

namespace bz::app {
namespace {

using doc::BooleanOp;

bool InsideRegion(const kernel::SketchRegion& region, Vec2 p) {
    bool inside = false;
    for (const auto& loop : region.loops)
        for (std::size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++) {
            const Vec2 a = loop[j], b = loop[i];
            if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) inside = !inside;
        }
    return inside;
}

/// Index of the region containing a profile sample point, or -1.
int RegionOf(const std::vector<kernel::SketchRegion>& regions, Vec2 p) {
    for (std::size_t i = 0; i < regions.size(); ++i)
        if (InsideRegion(regions[i], p)) return static_cast<int>(i);
    return -1;
}

/// True when some feature of the document reads `sketch`.
bool IsUsed(const doc::Document& d, ObjectId sketch) {
    for (const auto& f : d.Features())
        for (ObjectId dep : f->Dependencies())
            if (dep == sketch) return true;
    return false;
}

/// The last sketch in the active part that no operation uses yet.
ObjectId LastUnusedSketch(const doc::Document& d) {
    for (std::size_t i = d.EndMarker(); i-- > 0;) {
        const auto* sk = dynamic_cast<const doc::SketchFeature*>(d.Features()[i].get());
        if (sk && !IsUsed(d, sk->id)) return sk->id;
    }
    return {};
}

/// True when an active solid operation comes before `index`: only then can
/// a new one join, cut or intersect.
bool HasBodyBefore(const doc::Document& d, std::size_t index) {
    for (std::size_t i = 0; i < index && i < d.FeatureCount(); ++i)
        if (dynamic_cast<const doc::SketchSolidFeature*>(d.Features()[i].get()) && d.IsActive(i)) return true;
    return false;
}

} // namespace

// --- lifecycle --------------------------------------------------------------

doc::SketchSolidFeature* SketchOperationEditor::Feature(Session& session) const {
    return session.doc.FindAs<doc::SketchSolidFeature>(myFeature);
}

void SketchOperationEditor::Changed(Session& session) const {
    const int index = session.doc.IndexOf(myFeature);
    if (index >= 0) session.doc.MarkChanged(static_cast<std::size_t>(index));
}

void SketchOperationEditor::Reset() {
    myFeature = {};
    myIsNew = false;
    myOriginal.reset();
    myHoveredRegion = -1;
}

void SketchOperationEditor::StartNew(AppContext& app, Session& session, ObjectId sketch) {
    if (session.mode == EditMode::Sketch) {
        if (!sketch) sketch = session.activeSketch;
        app.sketch.Finish(app, session);
    }
    if (!sketch && session.doc.FindAs<doc::SketchFeature>(session.selectedFeature)) sketch = session.selectedFeature;
    if (!sketch) sketch = LastUnusedSketch(session.doc);
    if (!sketch) {
        app.notes.Info("Сначала нарисуйте эскиз с замкнутым контуром");
        return;
    }
    if (IsOpen()) Cancel(app, session);

    std::unique_ptr<doc::SketchSolidFeature> f = Create(session, sketch);
    f->id = session.doc.Ids().Next();
    f->name = session.doc.UniqueFeatureName(Title());
    f->sketch = sketch;
    f->operation = HasBodyBefore(session.doc, session.doc.EndMarker()) ? BooleanOp::Join : BooleanOp::NewBody;
    myFeature = f->id;
    myIsNew = true;
    LoadTexts(*f);
    myFocusFirstField = true;
    session.doc.Insert(std::move(f), session.doc.EndMarker());   // preview, not yet an undo step
    session.selectedFeature = myFeature;
}

void SketchOperationEditor::EditExisting(AppContext& app, Session& session, ObjectId feature) {
    auto* f = session.doc.FindAs<doc::SketchSolidFeature>(feature);
    if (!f || !CanEdit(*f)) return;
    if (IsOpen()) Cancel(app, session);
    if (session.mode == EditMode::Sketch) app.sketch.Finish(app, session);
    myFeature = feature;
    myIsNew = false;
    myOriginal = f->Clone();
    LoadTexts(*f);
    myFocusFirstField = true;
    session.selectedFeature = feature;
}

bool SketchOperationEditor::Commit(AppContext& app, Session& session) {
    doc::SketchSolidFeature* f = Feature(session);
    if (!f) return false;
    session.RebuildIfNeeded();
    if (f->status.state == doc::FeatureState::Error) {
        app.notes.Error(f->status.message);
        return false;
    }
    if (myIsNew) {
        const auto index = static_cast<std::size_t>(session.doc.IndexOf(myFeature));
        std::unique_ptr<doc::Feature> taken = session.doc.Take(myFeature);
        session.Execute(doc::AddFeature(std::move(taken), index));
    } else {
        session.CommitInPlaceEdit(std::move(myOriginal), f->name);
    }
    return true;
}

void SketchOperationEditor::Apply(AppContext& app, Session& session) {
    const doc::SketchSolidFeature* f = Feature(session);
    if (!f) return;
    const ObjectId sketch = f->sketch;
    const std::unique_ptr<doc::Feature> previous = f->Clone();
    if (!Commit(app, session)) return;
    Reset();
    // Ready for the next profile of the same sketch, like "Применить" in Inventor.
    StartNew(app, session, sketch);
    if (doc::SketchSolidFeature* next = Feature(session)) {
        const auto& prev = static_cast<const doc::SketchSolidFeature&>(*previous);
        next->profiles = {false, {}};
        next->operation = prev.operation == BooleanOp::NewBody ? BooleanOp::Join : prev.operation;
        CarryOver(prev, *next);
        LoadTexts(*next);
        Changed(session);
    }
}

void SketchOperationEditor::Done(AppContext& app, Session& session) {
    if (!Commit(app, session)) return;
    Reset();
    app.shell.keepModelInView = true;
}

void SketchOperationEditor::Cancel(AppContext& app, Session& session) {
    (void)app;
    if (myIsNew) {
        session.doc.Take(myFeature);
    } else if (myOriginal) {
        session.doc.Replace(std::move(myOriginal));
    }
    session.selectedFeature = {};
    Reset();
}

void SketchOperationEditor::CarryOver(const doc::SketchSolidFeature& previous, doc::SketchSolidFeature& next) const {
    (void)previous;
    (void)next;
}

bool SketchOperationEditor::PickInViewport(AppContext& app, Session& session, doc::SketchSolidFeature& feature,
                                           const SketchView& view) {
    (void)app;
    (void)session;
    (void)feature;
    (void)view;
    return false;
}

// --- viewport -----------------------------------------------------------------

void SketchOperationEditor::DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) {
    doc::SketchSolidFeature* f = Feature(session);
    if (!f) return;
    const auto* sk = session.doc.FindAs<doc::SketchFeature>(f->sketch);
    if (!sk) return;
    const std::vector<kernel::SketchRegion>* regions = session.model.Regions(f->sketch);

    const SketchView view{sk->placement.frame, vf};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ui::ThemeColors& c = app.ui.C();

    // Which regions are chosen now.
    std::vector<bool> chosen(regions ? regions->size() : 0, false);
    if (regions) {
        for (std::size_t i = 0; i < regions->size(); ++i) chosen[i] = f->profiles.automatic && (*regions)[i].automatic;
        if (!f->profiles.automatic)
            for (const Vec2 p : f->profiles.regions)
                if (const int r = RegionOf(*regions, p); r >= 0) chosen[static_cast<std::size_t>(r)] = true;
    }

    myHoveredRegion = -1;
    if (regions && vf.hovered)
        if (const auto cursor = view.FromScreen(ImGui::GetMousePos())) myHoveredRegion = RegionOf(*regions, *cursor);

    if (regions) {
        for (std::size_t i = 0; i < regions->size(); ++i) {
            const bool hovered = static_cast<int>(i) == myHoveredRegion;
            if (!chosen[i] && !hovered) continue;
            const Color fill = hovered ? c.regionFill.WithAlpha(c.regionFill.a * 1.5f) : c.regionFill;
            FillSketchRegion(dl, view, (*regions)[i], fill, hovered ? c.sketchHover : c.sketchFree,
                             app.ui.S(hovered ? 2.0f : 0.0f));
        }
    }
    SketchDrawStyle style;
    style.passive = true;
    DrawSketch(app.ui, dl, *sk, view, session.doc.units, style);

    // The operation may want the click first (an axis line wins over the region behind it).
    if (PickInViewport(app, session, *f, view)) {
        myHoveredRegion = -1;
        return;
    }

    // Click: pick or drop a region (switches the profile to manual choice).
    if (regions && vf.hovered && myHoveredRegion >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        std::vector<Vec2> picks;
        for (std::size_t i = 0; i < regions->size(); ++i)
            if (chosen[i] != (static_cast<int>(i) == myHoveredRegion)) picks.push_back((*regions)[i].sample);
        f->profiles = {false, std::move(picks)};
        Changed(session);
    }
}

// --- panel --------------------------------------------------------------------

void SketchOperationEditor::Section(AppContext& app, const char* label) const {
    ui::FieldLabel(app.ui, label);
    ImGui::Dummy({0, app.ui.S(2)});
}

void SketchOperationEditor::Gap(AppContext& app) const { ImGui::Dummy({0, app.ui.S(12)}); }

bool SketchOperationEditor::ParameterField(AppContext& app, Session& session, const char* id, std::string& text,
                                           Parameter& parameter, float width, bool focus, bool showFx) {
    ui::Ui& ui = app.ui;
    ImGui::PushID(id);
    const ui::FieldResult r = ui::ExpressionField(ui, "value", text, UnitLabel(parameter.quantity, session.doc.units),
                                                  parameter.error, width, focus, showFx);
    bool changed = false;
    if (r.edited) {
        parameter.expression = text;
        Changed(session);
        changed = true;
    }
    // Enter finishes the operation, as the "Готово Enter" button says.
    if (r.submitted && parameter.error.empty()) app.Later([&app, &session, this] { Done(app, session); });
    if (r.fxClicked) ImGui::OpenPopup("##fx");
    ui::FontScope font(ui.fonts.body);   // before Begin: must outlive the End call
    if (ImGui::BeginPopup("##fx")) {
        ImGui::TextDisabled("Вставить переменную");
        for (const Parameter* p : session.doc.AllParameters()) {
            if (p->name.empty() || p == &parameter || p->quantity != parameter.quantity) continue;
            const std::string item = p->name + "  =  " + FormatValue(p->value, p->quantity, session.doc.units);
            if (ImGui::Selectable(item.c_str())) {
                text = p->name;
                parameter.expression = text;
                Changed(session);
                changed = true;
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

void SketchOperationEditor::DrawProfileSection(AppContext& app, Session& session, doc::SketchSolidFeature& f, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    Section(app, "Профиль");
    const auto* sk = session.doc.FindAs<doc::SketchFeature>(f.sketch);
    const auto* regions = session.model.Regions(f.sketch);
    int count = 0;
    if (regions) {
        if (f.profiles.automatic) {
            for (const auto& r : *regions) count += r.automatic ? 1 : 0;
        } else {
            count = static_cast<int>(f.profiles.regions.size());
        }
    }
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ui.S(34);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, {p.x + width, p.y + h}, ui::ToU32(c.accentSoft), ui.S(8));
    dl->AddRect(p, {p.x + width, p.y + h}, ui::ToU32(c.accentSoftBorder), ui.S(8));
    ui.icons.Draw(dl, count > 0 ? "ui-check" : "status-warning", {p.x + ui.S(10), p.y + (h - ui.S(14)) / 2}, ui.S(14), c.accent);
    const std::string chip = (sk ? sk->name : std::string("?")) + " \xC2\xB7 " + (f.profiles.automatic ? "авто" : "вручную") +
                             ": " + std::to_string(count);
    dl->AddText(ui.fonts.medium, ui.S(13), {p.x + ui.S(32), p.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(c.text), chip.c_str());
    ImGui::Dummy({width, h});
    {
        ui::FontScope font(ui.fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textMuted));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextUnformatted(f.profiles.automatic
                                   ? "Все замкнутые контуры, вложенные стали отверстиями. Щёлкните по контуру, чтобы выбрать вручную."
                                   : "Щёлкните по контуру на модели, чтобы добавить или убрать его.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    if (!f.profiles.automatic && ui::LinkButton(ui, "##auto-profile", "Выбрать автоматически")) {
        f.profiles = {true, {}};
        Changed(session);
    }
}

void SketchOperationEditor::DrawPanel(AppContext& app, Session& session) {
    doc::SketchSolidFeature* f = Feature(session);
    if (!f) {
        Reset();
        return;
    }
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const float width = ImGui::GetContentRegionAvail().x;

    // Header: icon, title, close.
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(48);
        ui.icons.Draw(dl, Icon(), {p.x + ui.S(14), p.y + (h - ui.S(24)) / 2}, ui.S(24), {1, 1, 1, 1});
        dl->AddText(ui.fonts.title, ui.S(14), {p.x + ui.S(48), p.y + (h - ui.S(14)) / 2 - ui.S(1)}, ui::ToU32(c.text),
                    myIsNew ? Title() : f->name.c_str());
        ImGui::SetCursorScreenPos({p.x + width - ui.S(38), p.y + ui.S(9)});
        if (ui::IconButton(ui, "##close-op", "ui-close", 30, 14, "Отмена (Esc)"))
            app.Later([&app, &session, this] { Cancel(app, session); });
        ImGui::SetCursorScreenPos({p.x, p.y + h});
        ui::Divider(ui, c.dividerSoft);
    }

    const float footer = ui.S(62);
    ImGui::BeginChild("##operation-body", {width, ImGui::GetContentRegionAvail().y - footer}, ImGuiChildFlags_None);
    ImGui::SetCursorPos({ui.S(14), ui.S(14)});
    ImGui::BeginGroup();
    const float inner = width - ui.S(28);
    ImGui::PushItemWidth(inner);

    // Operation: a new body, or combined with the body before it.
    {
        Section(app, "Операция");
        const bool bodyBefore = HasBodyBefore(session.doc, static_cast<std::size_t>(session.doc.IndexOf(f->id)));
        static constexpr std::array<const char*, 4> labels{"Новое тело", "Добавить", "Вырезать", "Пересечь"};
        const int current = static_cast<int>(f->operation);
        const int chosen = ui::Segmented(ui, "op", labels, current, 2, inner, bodyBefore ? 0 : 0b1110);
        if (chosen != current) {
            f->operation = static_cast<BooleanOp>(chosen);
            Changed(session);
        }
        Gap(app);
    }
    DrawProfileSection(app, session, *f, inner);
    Gap(app);
    DrawOptions(app, session, *f, inner);
    myFocusFirstField = false;

    if (f->status.state == doc::FeatureState::Error && !f->status.message.empty()) {
        Gap(app);
        ui::FontScope font(ui.fonts.bodySmall);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.danger));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner);
        ImGui::TextUnformatted(f->status.message.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ImGui::PopItemWidth();
    ImGui::EndGroup();
    ImGui::EndChild();

    // Footer: Отмена / Применить / Готово.
    ui::Divider(ui, c.dividerSoft);
    const ImVec2 fp = ImGui::GetCursorScreenPos();
    const float bw = (width - ui.S(28) - ui.S(16)) / 3.0f;
    const float bh = ui.S(36);
    ImGui::SetCursorScreenPos({fp.x + ui.S(14), fp.y + ui.S(12)});
    if (ui::SecondaryButton(ui, "##cancel", "Отмена", nullptr, {bw, bh})) app.Later([&app, &session, this] { Cancel(app, session); });
    ui::ItemTooltip(ui, "Esc");
    ImGui::SameLine(0, ui.S(8));
    if (ui::SecondaryButton(ui, "##apply", "Применить", nullptr, {bw, bh})) app.Later([&app, &session, this] { Apply(app, session); });
    ui::ItemTooltip(ui, "Создать и сразу начать следующую такую же операцию");
    ImGui::SameLine(0, ui.S(8));
    if (ui::PrimaryButton(ui, "##done", "Готово", "Enter", {bw, bh})) app.Later([&app, &session, this] { Done(app, session); });
}

} // namespace bz::app
