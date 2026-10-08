#include "app/features/ExtrudeEditor.h"

#include "app/Format.h"
#include "app/sketch/SketchEditor.h"
#include "app/sketch/SketchOverlay.h"
#include "document/Commands.h"
#include "document/SketchFeature.h"
#include "ui/Widgets.h"

#include <array>
#include <cmath>

namespace bz::app {
namespace {

using doc::BooleanOp;
using doc::ExtentType;

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

/// The last sketch in the active part that no extrusion uses yet.
ObjectId LastUnusedSketch(const doc::Document& d) {
    for (std::size_t i = d.EndMarker(); i-- > 0;) {
        const auto* sk = dynamic_cast<const doc::SketchFeature*>(d.Features()[i].get());
        if (!sk) continue;
        bool used = false;
        for (const auto& f : d.Features())
            if (const auto* ex = dynamic_cast<const doc::ExtrudeFeature*>(f.get()); ex && ex->sketch == sk->id) used = true;
        if (!used) return sk->id;
    }
    return {};
}

} // namespace

doc::ExtrudeFeature* ExtrudeEditor::Feature(Session& session) const {
    return session.doc.FindAs<doc::ExtrudeFeature>(myFeature);
}

void ExtrudeEditor::Changed(Session& session) const {
    const int index = session.doc.IndexOf(myFeature);
    if (index >= 0) session.doc.MarkChanged(static_cast<std::size_t>(index));
}

void ExtrudeEditor::Reset() {
    myFeature = {};
    myIsNew = false;
    myOriginal.reset();
    myHoveredRegion = -1;
}

void ExtrudeEditor::StartNew(AppContext& app, Session& session, ObjectId sketch) {
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

    auto f = std::make_unique<doc::ExtrudeFeature>();
    f->id = session.doc.Ids().Next();
    f->name = session.doc.UniqueFeatureName("Выдавливание");
    f->sketch = sketch;
    f->distance.name = session.doc.NextParameterName();
    f->distance.expression = "10";
    f->operation = session.model.Bodies().empty() ? BooleanOp::NewBody : BooleanOp::Join;
    myFeature = f->id;
    myIsNew = true;
    myDistance = f->distance.expression;
    myTaper = f->taper.expression;
    myFocusDistance = true;
    session.doc.Insert(std::move(f), session.doc.EndMarker());   // preview, not yet an undo step
    session.selectedFeature = myFeature;
}

void ExtrudeEditor::EditExisting(AppContext& app, Session& session, ObjectId feature) {
    auto* f = session.doc.FindAs<doc::ExtrudeFeature>(feature);
    if (!f) return;
    if (IsOpen()) Cancel(app, session);
    if (session.mode == EditMode::Sketch) app.sketch.Finish(app, session);
    myFeature = feature;
    myIsNew = false;
    myOriginal = f->Clone();
    myDistance = f->distance.expression;
    myTaper = f->taper.expression;
    myFocusDistance = true;
    session.selectedFeature = feature;
}

bool ExtrudeEditor::Commit(AppContext& app, Session& session) {
    doc::ExtrudeFeature* f = Feature(session);
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

void ExtrudeEditor::Apply(AppContext& app, Session& session) {
    const doc::ExtrudeFeature* f = Feature(session);
    if (!f) return;
    const ObjectId sketch = f->sketch;
    const doc::ExtrudeFeature settings = *f;
    if (!Commit(app, session)) return;
    Reset();
    // Ready for the next profile of the same sketch, like "Применить" in Inventor.
    StartNew(app, session, sketch);
    if (doc::ExtrudeFeature* next = Feature(session)) {
        next->profiles = {false, {}};
        next->operation = settings.operation == BooleanOp::NewBody ? BooleanOp::Join : settings.operation;
        next->extent = settings.extent;
        next->reversed = settings.reversed;
        Changed(session);
    }
}

void ExtrudeEditor::Done(AppContext& app, Session& session) {
    if (!Commit(app, session)) return;
    Reset();
    app.shell.keepModelInView = true;
}

void ExtrudeEditor::Cancel(AppContext& app, Session& session) {
    (void)app;
    if (myIsNew) {
        session.doc.Take(myFeature);
    } else if (myOriginal) {
        session.doc.Replace(std::move(myOriginal));
    }
    session.selectedFeature = {};
    Reset();
}

std::string ExtrudeEditor::Hint() const {
    return "Щёлкните по контурам эскиза, введите расстояние. Enter или щелчок колесом — готово, Esc — отмена";
}

void ExtrudeEditor::DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) {
    doc::ExtrudeFeature* f = Feature(session);
    if (!f) return;
    const auto* sk = session.doc.FindAs<doc::SketchFeature>(f->sketch);
    const std::vector<kernel::SketchRegion>* regions = session.model.Regions(f->sketch);
    if (!sk) return;

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
            FillSketchRegion(dl, view, (*regions)[i], fill, hovered ? c.sketchHover : c.sketchFree, app.ui.S(hovered ? 2.0f : 0.0f));
        }
    }
    SketchDrawStyle style;
    style.passive = true;
    DrawSketch(app.ui, dl, *sk, view, session.doc.units, style);

    // Click: pick or drop a region (switches the profile to manual choice).
    if (regions && vf.hovered && myHoveredRegion >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        std::vector<Vec2> picks;
        for (std::size_t i = 0; i < regions->size(); ++i)
            if (chosen[i] != (static_cast<int>(i) == myHoveredRegion)) picks.push_back((*regions)[i].sample);
        f->profiles = {false, std::move(picks)};
        Changed(session);
    }
}

void ExtrudeEditor::DrawPanel(AppContext& app, Session& session) {
    doc::ExtrudeFeature* f = Feature(session);
    if (!f) {
        Reset();
        return;
    }
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float width = ImGui::GetContentRegionAvail().x;

    // Header: icon, title, close.
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(48);
        ui.icons.Draw(dl, "tree-extrude", {p.x + ui.S(14), p.y + (h - ui.S(24)) / 2}, ui.S(24), {1, 1, 1, 1});
        dl->AddText(ui.fonts.title, ui.S(14), {p.x + ui.S(48), p.y + (h - ui.S(14)) / 2 - ui.S(1)}, ui::ToU32(c.text),
                    myIsNew ? "Выдавливание" : f->name.c_str());
        ImGui::SetCursorScreenPos({p.x + width - ui.S(38), p.y + ui.S(9)});
        if (ui::IconButton(ui, "##close-op", "ui-close", 30, 14, "Отмена (Esc)")) {
            app.Later([&app, &session, this] { Cancel(app, session); });
        }
        ImGui::SetCursorScreenPos({p.x, p.y + h});
        ui::Divider(ui, c.dividerSoft);
    }

    const float footer = ui.S(62);
    ImGui::BeginChild("##extrude-body", {width, ImGui::GetContentRegionAvail().y - footer}, ImGuiChildFlags_None);
    ImGui::SetCursorPos({ui.S(14), ui.S(14)});
    ImGui::BeginGroup();
    ImGui::PushItemWidth(width - ui.S(28));
    const float inner = width - ui.S(28);
    const auto section = [&](const char* label) {
        ui::FieldLabel(ui, label);
        ImGui::Dummy({0, ui.S(2)});
    };
    const auto gap = [&] { ImGui::Dummy({0, ui.S(12)}); };

    // Operation.
    {
        section("Операция");
        const std::size_t index = static_cast<std::size_t>(session.doc.IndexOf(f->id));
        bool bodiesBefore = false;
        for (std::size_t i = 0; i < index; ++i)
            if (dynamic_cast<const doc::ExtrudeFeature*>(session.doc.Features()[i].get()) && session.doc.IsActive(i))
                bodiesBefore = true;
        static constexpr std::array<const char*, 4> labels{"Новое тело", "Добавить", "Вырезать", "Пересечь"};
        const int current = static_cast<int>(f->operation);
        const int chosen = ui::Segmented(ui, "op", labels, current, 2, inner, bodiesBefore ? 0 : 0b1110);
        if (chosen != current) {
            f->operation = static_cast<BooleanOp>(chosen);
            if (f->operation != BooleanOp::Cut && f->extent == ExtentType::ThroughAll) f->extent = ExtentType::Distance;
            Changed(session);
        }
        gap();
    }

    // Profile.
    {
        section("Профиль");
        const auto* sk = session.doc.FindAs<doc::SketchFeature>(f->sketch);
        const auto* regions = session.model.Regions(f->sketch);
        int count = 0;
        if (regions) {
            if (f->profiles.automatic) {
                for (const auto& r : *regions) count += r.automatic ? 1 : 0;
            } else {
                count = static_cast<int>(f->profiles.regions.size());
            }
        }
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(34);
        dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, {p.x + inner, p.y + h}, ui::ToU32(c.accentSoft), ui.S(8));
        dl->AddRect(p, {p.x + inner, p.y + h}, ui::ToU32(c.accentSoftBorder), ui.S(8));
        ui.icons.Draw(dl, count > 0 ? "ui-check" : "status-warning", {p.x + ui.S(10), p.y + (h - ui.S(14)) / 2}, ui.S(14), c.accent);
        const std::string chip = (sk ? sk->name : std::string("?")) + " \xC2\xB7 " +
                                 (f->profiles.automatic ? "авто" : "вручную") + ": " + std::to_string(count);
        dl->AddText(ui.fonts.medium, ui.S(13), {p.x + ui.S(32), p.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(c.text), chip.c_str());
        ImGui::Dummy({inner, h});
        {
            ui::FontScope font(ui.fonts.caption);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textMuted));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner);
            ImGui::TextUnformatted(f->profiles.automatic
                                       ? "Все замкнутые контуры, вложенные стали отверстиями. Щёлкните по контуру, чтобы выбрать вручную."
                                       : "Щёлкните по контуру на модели, чтобы добавить или убрать его.");
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        if (!f->profiles.automatic && ui::LinkButton(ui, "##auto-profile", "Выбрать автоматически")) {
            f->profiles = {true, {}};
            Changed(session);
        }
        gap();
    }

    // Direction.
    {
        section("Направление");
        static constexpr std::array<const char*, 3> labels{"Прямое", "Обратное", "Симметрично"};
        const int current = f->extent == ExtentType::Symmetric ? 2 : f->reversed ? 1 : 0;
        const int chosen = ui::Segmented(ui, "dir", labels, current, 3, inner);
        if (chosen != current) {
            f->reversed = chosen == 1;
            if (chosen == 2) f->extent = ExtentType::Symmetric;
            else if (f->extent == ExtentType::Symmetric) f->extent = ExtentType::Distance;
            Changed(session);
        }
        gap();
    }

    // Depth for cuts: a distance or through everything.
    if (f->operation == BooleanOp::Cut) {
        section("Глубина");
        static constexpr std::array<const char*, 2> labels{"На расстояние", "Насквозь"};
        const int current = f->extent == ExtentType::ThroughAll ? 1 : 0;
        const int chosen = ui::Segmented(ui, "depth", labels, current, 2, inner);
        if (chosen != current) {
            f->extent = chosen == 1 ? ExtentType::ThroughAll : ExtentType::Distance;
            Changed(session);
        }
        gap();
    }

    if (f->extent != ExtentType::ThroughAll) {
        section(f->extent == ExtentType::Symmetric ? "Расстояние (всего)" : "Расстояние");
        const ui::FieldResult r = ui::ExpressionField(ui, "distance", myDistance, UnitLabel(Quantity::Length, session.doc.units),
                                                      f->distance.error, inner, myFocusDistance);
        myFocusDistance = false;
        if (r.edited) {
            f->distance.expression = myDistance;
            Changed(session);
        }
        // Enter in the field finishes the operation, as the "Готово Enter" button says.
        if (r.submitted && f->distance.error.empty()) app.Later([&app, &session, this] { Done(app, session); });
        if (r.fxClicked) ImGui::OpenPopup("##fx-distance");
        ui::FontScope font(ui.fonts.body);   // before Begin: must outlive the End call
        if (ImGui::BeginPopup("##fx-distance")) {
            ImGui::TextDisabled("Вставить переменную");
            for (const Parameter* p : session.doc.AllParameters()) {
                if (p->name.empty() || p == &f->distance || p->quantity != Quantity::Length) continue;
                const std::string item = p->name + "  =  " + FormatValue(p->value, Quantity::Length, session.doc.units);
                if (ImGui::Selectable(item.c_str())) {
                    myDistance = p->name;
                    f->distance.expression = myDistance;
                    Changed(session);
                }
            }
            ImGui::EndPopup();
        }
        gap();
    }

    section("Уклон");
    const ui::FieldResult taper = ui::ExpressionField(ui, "taper", myTaper, UnitLabel(Quantity::Angle, session.doc.units),
                                                      f->taper.error, inner, false, false);
    if (taper.edited) {
        f->taper.expression = myTaper;
        Changed(session);
    }

    if (f->status.state == doc::FeatureState::Error && !f->status.message.empty()) {
        gap();
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
    ui::ItemTooltip(ui, "Создать и сразу начать следующее выдавливание");
    ImGui::SameLine(0, ui.S(8));
    if (ui::PrimaryButton(ui, "##done", "Готово", "Enter", {bw, bh})) app.Later([&app, &session, this] { Done(app, session); });
}

} // namespace bz::app
