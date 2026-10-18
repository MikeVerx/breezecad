#include "app/features/FeatureOperationEditor.h"
#include "app/features/ToolChoice.h"

#include "app/Format.h"
#include "app/shell/Shell.h"
#include "app/sketch/SketchEditor.h"
#include "app/viewport/Planes.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "document/Commands.h"
#include "document/LoftFeature.h"
#include "document/Origin.h"
#include "document/SketchSolidFeature.h"
#include "document/WorkPlaneFeature.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace bz::app {

bool HasSolidBefore(const doc::Document& d, std::size_t index) {
    for (std::size_t i = 0; i < index && i < d.FeatureCount(); ++i) {
        const doc::Feature* f = d.Features()[i].get();
        const bool solid = dynamic_cast<const doc::SketchSolidFeature*>(f) || dynamic_cast<const doc::LoftFeature*>(f);   // incl. sweep
        if (solid && d.IsActive(i) && !f->suppressed) return true;
    }
    return false;
}

// --- lifecycle --------------------------------------------------------------

doc::Feature* FeatureOperationEditor::Edited(Session& session) const { return session.doc.Find(myFeature); }

void FeatureOperationEditor::Changed(Session& session) const {
    const int index = session.doc.IndexOf(myFeature);
    if (index >= 0) session.doc.MarkChanged(static_cast<std::size_t>(index));
}

void FeatureOperationEditor::Reset() {
    myFeature = {};
    myIsNew = false;
    myOriginal.reset();
}

void FeatureOperationEditor::OpenNew(AppContext& app, Session& session, std::unique_ptr<doc::Feature> feature) {
    if (IsOpen()) Cancel(app, session);
    if (session.mode == EditMode::Sketch) app.sketch.Finish(app, session);
    feature->id = session.doc.Ids().Next();
    if (feature->name.empty()) feature->name = session.doc.UniqueFeatureName(Title());
    myFeature = feature->id;
    myIsNew = true;
    LoadTexts(*feature);
    myFocusFirstField = true;
    session.doc.Insert(std::move(feature), session.doc.EndMarker());   // preview, not yet an undo step
    session.selectedFeature = myFeature;
}

void FeatureOperationEditor::EditExisting(AppContext& app, Session& session, ObjectId feature) {
    doc::Feature* f = session.doc.Find(feature);
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

bool FeatureOperationEditor::Commit(AppContext& app, Session& session) {
    doc::Feature* f = Edited(session);
    if (!f) return false;
    session.RebuildNow();
    if (f->status.state == doc::FeatureState::Error) {
        app.notes.Error(Tr(f->status.message));
        return false;
    }
    // Turning the preview into an undo step puts the same feature back in
    // the same place: what is already built stays valid, no second rebuild.
    const std::size_t built = session.doc.DirtyFrom();
    if (myIsNew) {
        const auto index = static_cast<std::size_t>(session.doc.IndexOf(myFeature));
        std::unique_ptr<doc::Feature> taken = session.doc.Take(myFeature);
        session.Execute(doc::AddFeature(std::move(taken), index));
    } else {
        session.CommitInPlaceEdit(std::move(myOriginal), f->name);
    }
    session.doc.KeepDirtyFrom(built);
    return true;
}

void FeatureOperationEditor::Apply(AppContext& app, Session& session) {
    const doc::Feature* f = Edited(session);
    if (!f) return;
    const std::unique_ptr<doc::Feature> previous = f->Clone();
    if (!Commit(app, session)) return;
    Reset();
    StartNext(app, session, *previous);
}

void FeatureOperationEditor::StartNext(AppContext& app, Session& session, const doc::Feature& previous) {
    (void)app;
    (void)session;
    (void)previous;
}

bool FeatureOperationEditor::ShowStatus(const doc::Feature& feature) const {
    (void)feature;
    return true;
}

void FeatureOperationEditor::Done(AppContext& app, Session& session) {
    if (!Commit(app, session)) return;
    Reset();
    app.shell.keepModelInView = true;
}

void FeatureOperationEditor::Cancel(AppContext& app, Session& session) {
    (void)app;
    if (myIsNew) {
        session.doc.Take(myFeature);
    } else if (myOriginal) {
        session.doc.Replace(std::move(myOriginal));
    }
    session.selectedFeature = {};
    Reset();
}

// --- panel pieces -------------------------------------------------------------

void FeatureOperationEditor::Section(AppContext& app, const char* label) const {
    ui::FieldLabel(app.ui, label);
    ImGui::Dummy({0, app.ui.S(2)});
}

void FeatureOperationEditor::Gap(AppContext& app) const { ImGui::Dummy({0, app.ui.S(12)}); }

void FeatureOperationEditor::Caption(AppContext& app, const char* text, float width) const {
    ui::FontScope font(app.ui.fonts.caption);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(app.ui.C().textMuted));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

bool FeatureOperationEditor::FeatureChecklist(AppContext& app, Session& session, std::vector<ObjectId>& chosen,
                                              const std::function<bool(const doc::Feature&)>& eligible, float width,
                                              const char* emptyText) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const int self = session.doc.IndexOf(myFeature);
    bool changed = false;
    bool any = false;
    for (int i = 0; i < self; ++i) {
        const doc::Feature& candidate = *session.doc.Features()[static_cast<std::size_t>(i)];
        if (!eligible(candidate) || candidate.suppressed) continue;
        any = true;
        const auto it = std::ranges::find(chosen, candidate.id);
        const bool on = it != chosen.end();

        ImGui::PushID(static_cast<int>(candidate.id.value));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(30);
        if (ImGui::InvisibleButton("##check", {width, h})) {
            if (on) {
                chosen.erase(it);
            } else {
                chosen.push_back(candidate.id);
                std::ranges::sort(chosen, {}, [&](ObjectId id) { return session.doc.IndexOf(id); });
            }
            changed = true;
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (ImGui::IsItemHovered()) dl->AddRectFilled(p, {p.x + width, p.y + h}, ui::ToU32(c.hoverRow), ui.S(6));
        const float box = ui.S(16);
        const ImVec2 b{p.x + ui.S(8), p.y + (h - box) / 2};
        if (on) {
            dl->AddRectFilled(b, {b.x + box, b.y + box}, ui::ToU32(c.accent), ui.S(4));
            ui.icons.Draw(dl, "ui-check", {b.x + ui.S(2), b.y + ui.S(2)}, ui.S(12), c.textOnAccent);
        } else {
            dl->AddRect(b, {b.x + box, b.y + box}, ui::ToU32(c.fieldBorder), ui.S(4), 0, ui.S(1.2f));
        }
        ui.icons.Draw(dl, FeatureIcon(candidate), {p.x + ui.S(34), p.y + (h - ui.S(18)) / 2}, ui.S(18), {1, 1, 1, 1});
        dl->AddText(ui.fonts.body, ui.S(13), {p.x + ui.S(60), p.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(c.text),
                    candidate.name.c_str());
        ImGui::PopID();
    }
    if (!any) Caption(app, emptyText, width);
    if (changed) Changed(session);
    return changed;
}

bool FeatureOperationEditor::PickOriginalFaces(AppContext& app, Session& session, std::vector<doc::FaceReference>& faces,
                                               const ViewportFrame& vf) {
    const int index = session.doc.IndexOf(myFeature);
    if (index < 0) return false;
    app.viewport.BeginPickingOriginal(session.model, static_cast<std::size_t>(index), render::PickTarget::Faces);
    app.viewport.SetMarkedFaces(faces);
    if (!vf.hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return false;
    const auto picked = app.viewport.PickFace(vf.ToLocal(ImGui::GetMousePos()));
    if (!picked) return false;
    const auto existing =
        std::ranges::find_if(faces, [&](const doc::FaceReference& r) { return app.viewport.SameOriginalFace(r, *picked); });
    if (existing != faces.end()) faces.erase(existing);
    else faces.push_back(*picked);
    Changed(session);
    return true;
}

void FeatureOperationEditor::PlaneChoice(AppContext& app, Session& session, ObjectId& plane, float width) {
    static constexpr std::array<ChoiceItem, 3> labels{{{nullptr, "XY"}, {nullptr, "XZ"}, {nullptr, "YZ"}}};
    static constexpr std::array<ObjectId, 3> ids{doc::origin::kPlaneXY, doc::origin::kPlaneXZ, doc::origin::kPlaneYZ};
    const auto at = std::ranges::find(ids, plane);
    const int current = at != ids.end() ? static_cast<int>(at - ids.begin()) : -1;
    const int chosen = ToolChoice(app, "plane", N_("Плоскость"), labels, current, width);
    if (chosen != current && chosen >= 0) {
        plane = ids[static_cast<std::size_t>(chosen)];
        Changed(session);
    }
    if (const auto* wp = session.doc.FindAs<doc::WorkPlaneFeature>(plane)) {
        const std::string text = Trf("Рабочая плоскость: {}", wp->name);
        Caption(app, text.c_str(), width);
    } else {
        Caption(app, Tr("Или щёлкните рабочую плоскость в окне модели."), width);
    }
}

void FeatureOperationEditor::PickPlane(AppContext& app, Session& session, ObjectId& plane, const ViewportFrame& vf) {
    const int index = session.doc.IndexOf(myFeature);
    if (index < 0) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    std::vector<ShownPlane> planes = OriginPlanes(session);
    const std::vector<ShownPlane> work = WorkPlanes(session, static_cast<std::size_t>(index));
    planes.insert(planes.end(), work.begin(), work.end());
    const ShownPlane* hot = vf.hovered ? PlaneUnderCursor(vf, ImGui::GetMousePos(), planes) : nullptr;
    // Only the chosen plane and the one under the cursor: the rest would hide the preview.
    for (const ShownPlane& p : planes)
        if (&p == hot || p.id == plane) DrawPlane(app, dl, vf, p, true);
    if (hot && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        plane = hot->id;
        Changed(session);
    }
}

bool FeatureOperationEditor::Ready(Session& session) const {
    const doc::Feature* f = Edited(session);
    return f && (f->status.state == doc::FeatureState::Ok || f->status.state == doc::FeatureState::Warning);
}

bool FeatureOperationEditor::StepBack(AppContext& app, Session& session) {
    doc::Feature* f = Edited(session);
    if (!f || !PopPick(*f)) return false;
    Changed(session);
    return true;
}

bool FeatureOperationEditor::ParameterField(AppContext& app, Session& session, const char* id, std::string& text,
                                            Parameter& parameter, float width, bool focus, bool showFx) {
    ui::Ui& ui = app.ui;
    ImGui::PushID(id);
    if (app.shell.mobile.phone) {
        // Where the field is, whenever that changes: the emulator's check taps it (smoke-apk.sh).
        static std::vector<std::pair<ImGuiID, ImVec2>> logged;
        const ImGuiID gid = ImGui::GetID("value");
        const ImVec2 at = ImGui::GetCursorScreenPos();
        auto known = std::find_if(logged.begin(), logged.end(), [gid](const auto& e) { return e.first == gid; });
        if (known == logged.end() || std::abs(known->second.x - at.x) > 1 || std::abs(known->second.y - at.y) > 1) {
            if (known == logged.end()) logged.emplace_back(gid, at);
            else known->second = at;
            log::Info("Touch: field {} at {:.0f},{:.0f} size {:.0f}x{:.0f}", id, at.x, at.y, width,
                      ui.S(ui.theme.metrics.fieldHeight));
        }
    }
    const ui::FieldResult r = ui::ExpressionField(ui, "value", text, UnitLabel(parameter.quantity, session.doc.units),
                                                  parameter.error, width, focus, showFx);
    bool changed = false;
    if (r.edited) {
        parameter.expression = text;
        Changed(session);
        changed = true;
    }
    // Enter finishes the operation, as the "Готово Enter" button says.
    // Esc too: the field has the focus as soon as the panel opens, and Esc
    // should cancel the operation as the panel says, not just leave the field.
    // Not on the phone: there ✓ at a field only enters the number and ✕ only
    // leaves the field (the author, 18.10.2026); the row's ✓ / ✕ do the rest.
    const bool phone = app.shell.mobile.phone;
    if (r.submitted && parameter.error.empty() && !phone) app.Later([&app, &session, this] { Done(app, session); });
    if (r.cancelled && !phone) app.Later([&app, &session, this] { Cancel(app, session); });
    if (r.fxClicked) ImGui::OpenPopup("##fx");
    ui::FontScope font(ui.fonts.body);   // before Begin: must outlive the End call
    if (ImGui::BeginPopup("##fx")) {
        ImGui::TextDisabled("%s", Tr("Вставить переменную"));
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

void FeatureOperationEditor::DrawPanel(AppContext& app, Session& session) {
    doc::Feature* f = Edited(session);
    if (!f) {
        Reset();
        return;
    }
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    const float width = ImGui::GetContentRegionAvail().x;

    // Header: icon, title, close (not on the phone: block 2's status line and ✕ under block 1).
    if (!app.shell.mobile.phone) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(48);
        ui.icons.Draw(dl, Icon(), {p.x + ui.S(14), p.y + (h - ui.S(24)) / 2}, ui.S(24), {1, 1, 1, 1});
        ui::FittedText(dl, ui.fonts.title, ui.S(14), {p.x + ui.S(48), p.y + (h - ui.S(14)) / 2 - ui.S(1)}, p.x + width - ui.S(40),
                       ui::ToU32(c.text), myIsNew ? Title() : f->name.c_str());
        ImGui::SetCursorScreenPos({p.x + width - ui.S(38), p.y + ui.S(9)});
        if (ui::IconButton(ui, "##close-op", "ui-close", 30, 14, Tr("Отмена (Esc)")))
            app.Later([&app, &session, this] { Cancel(app, session); });
        ImGui::SetCursorScreenPos({p.x, p.y + h});
        ui::Divider(ui, c.dividerSoft);
    }

    // The phone has no footer: Отмена/ОК are the row under block 1, "Применить" its bottom line.
    const bool phone = app.shell.mobile.phone;
    const float footer = phone ? 0.0f : ui.S(62);
    ImGui::BeginChild("##operation-body", {width, ImGui::GetContentRegionAvail().y - footer}, ImGuiChildFlags_None);
    ImGui::SetCursorPos({ui.S(14), ui.S(14)});
    ImGui::BeginGroup();
    const float inner = width - ui.S(28);
    ImGui::PushItemWidth(inner);
    DrawBody(app, session, *f, inner);
    myFocusFirstField = false;

    if ((f->status.state == doc::FeatureState::Error || f->status.state == doc::FeatureState::Warning) && ShowStatus(*f)) {
        if (!f->status.message.empty()) {
            Gap(app);
            ui::FontScope font(ui.fonts.bodySmall);
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ui::ToVec4(f->status.state == doc::FeatureState::Error ? c.danger : c.warning));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner);
            ImGui::TextUnformatted(Tr(f->status.message).c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
    }
    ImGui::PopItemWidth();
    ImGui::EndGroup();
    ImGui::EndChild();

    if (phone) return;
    // Footer: Отмена / Применить / Готово.
    ui::Divider(ui, c.dividerSoft);
    const ImVec2 fp = ImGui::GetCursorScreenPos();
    const float bw = (width - ui.S(28) - ui.S(16)) / 3.0f;
    const float bh = ui.S(36);
    ImGui::SetCursorScreenPos({fp.x + ui.S(14), fp.y + ui.S(12)});
    if (ui::SecondaryButton(ui, "##cancel", Tr("Отмена"), nullptr, {bw, bh})) app.Later([&app, &session, this] { Cancel(app, session); });
    ui::ItemTooltip(ui, "Esc");
    ImGui::SameLine(0, ui.S(8));
    if (ui::SecondaryButton(ui, "##apply", Tr("Применить"), nullptr, {bw, bh})) app.Later([&app, &session, this] { Apply(app, session); });
    ui::ItemTooltip(ui, Tr("Создать и сразу начать следующую такую же операцию"));
    ImGui::SameLine(0, ui.S(8));
    if (ui::PrimaryButton(ui, "##done", Tr("Готово"), "Enter", {bw, bh})) app.Later([&app, &session, this] { Done(app, session); });
}

bool FeatureOperationEditor::PickChip(ui::Ui& ui, const char* id, const std::string& caption, const std::string& value, bool active, float width) {
    const ui::ThemeColors& c = ui.C();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ui.S(34);
    const bool clicked = ImGui::InvisibleButton(id, {width, h});
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, {p.x + width, p.y + h}, ui::ToU32(active ? c.accentSoft : hovered ? c.hover : c.panel), ui.S(8));
    dl->AddRect(p, {p.x + width, p.y + h}, ui::ToU32(active ? c.accent : c.accentSoftBorder), ui.S(8), 0, ui.S(active ? 1.6f : 1.0f));
    ui.icons.Draw(dl, value.empty() ? "status-warning" : "ui-check", {p.x + ui.S(10), p.y + (h - ui.S(14)) / 2}, ui.S(14),
                  value.empty() ? c.warning : c.accent);
    const std::string text = caption + ": " + (value.empty() ? std::string(Tr("не выбрано")) : value);
    dl->AddText(ui.fonts.medium, ui.S(13), {p.x + ui.S(32), p.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(c.text), text.c_str());
    return clicked;
}


} // namespace bz::app
