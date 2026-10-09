#include "app/features/EdgeOperationEditor.h"

#include "core/I18n.h"
#include "ui/Widgets.h"

#include <algorithm>

namespace bz::app {
namespace {

/// Two references point at the same edge (picked twice from the same body).
bool SameEdge(const doc::EdgeReference& a, const doc::EdgeReference& b) {
    return (a.middle - b.middle).Length() < 1e-6 && a.curve == b.curve;
}

} // namespace

// --- EdgeOperationEditor ------------------------------------------------------

void EdgeOperationEditor::StartNew(AppContext& app, Session& session) {
    if (session.model.Bodies().empty()) {
        app.notes.Info(Tr("Сначала создайте тело: эскиз и выдавливание или вращение"));
        return;
    }
    OpenNew(app, session, Create(session));
}

void EdgeOperationEditor::StartNext(AppContext& app, Session& session, const doc::Feature& previous) {
    StartNew(app, session);
    // Keep the size for the next set of edges.
    if (doc::Feature* next = Edited(session)) {
        std::vector<Parameter*> to;
        next->CollectParameters(to);
        const auto from = previous.Parameters();
        for (std::size_t i = 0; i < to.size() && i < from.size(); ++i) to[i]->expression = from[i]->expression;
        LoadTexts(*next);
        Changed(session);
    }
}

bool EdgeOperationEditor::ShowStatus(const doc::Feature& feature) const {
    // "Выберите рёбра" before any edge is picked just repeats the caption.
    return !static_cast<const doc::EdgeFeature&>(feature).edges.empty();
}

std::string EdgeOperationEditor::Hint() const {
    return Tr("Щёлкните рёбра модели (повторный щелчок убирает ребро), введите размер. Enter — готово, Esc — отмена");
}

void EdgeOperationEditor::DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) {
    auto& f = static_cast<doc::EdgeFeature&>(feature);
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();

    Section(app, Tr("Рёбра"));
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(34);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool any = !f.edges.empty();
        dl->AddRectFilled(p, {p.x + width, p.y + h}, ui::ToU32(c.accentSoft), ui.S(8));
        dl->AddRect(p, {p.x + width, p.y + h}, ui::ToU32(c.accentSoftBorder), ui.S(8));
        ui.icons.Draw(dl, any ? "ui-check" : "status-warning", {p.x + ui.S(10), p.y + (h - ui.S(14)) / 2}, ui.S(14), c.accent);
        const std::string chip = any ? Trf("Выбрано рёбер: {}", f.edges.size()) : std::string(Tr("Рёбра не выбраны"));
        dl->AddText(ui.fonts.medium, ui.S(13), {p.x + ui.S(32), p.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(c.text), chip.c_str());
        ImGui::Dummy({width, h});
    }
    Caption(app, Tr("Щёлкните ребро на модели, чтобы добавить его; повторный щелчок убирает."), width);
    if (!f.edges.empty() && ui::LinkButton(ui, "##clear-edges", Tr("Очистить выбор"))) {
        f.edges.clear();
        Changed(session);
    }
    Gap(app);
    DrawSize(app, session, f, width);
}

void EdgeOperationEditor::DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) {
    auto* f = dynamic_cast<doc::EdgeFeature*>(Edited(session));
    if (!f) return;
    const int index = session.doc.IndexOf(f->id);
    if (index < 0) return;
    app.viewport.BeginPickingOriginal(session.model, static_cast<std::size_t>(index), render::PickTarget::Edges);
    app.viewport.SetMarkedEdges(f->edges);

    if (!vf.hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    const auto picked = app.viewport.PickEdge(vf.ToLocal(ImGui::GetMousePos()));
    if (!picked) return;
    const auto existing = std::find_if(f->edges.begin(), f->edges.end(), [&](const auto& e) { return SameEdge(e, *picked); });
    if (existing != f->edges.end()) f->edges.erase(existing);
    else f->edges.push_back(*picked);
    Changed(session);
}

// --- FilletEditor -------------------------------------------------------------

bool FilletEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::FilletFeature*>(&f) != nullptr; }

std::unique_ptr<doc::EdgeFeature> FilletEditor::Create(Session& session) const {
    auto f = std::make_unique<doc::FilletFeature>();
    f->radius.name = session.doc.NextParameterName();
    return f;
}

void FilletEditor::LoadTexts(const doc::Feature& feature) {
    myRadius = static_cast<const doc::FilletFeature&>(feature).radius.expression;
}

void FilletEditor::DrawSize(AppContext& app, Session& session, doc::EdgeFeature& feature, float width) {
    Section(app, Tr("Радиус"));
    ParameterField(app, session, "radius", myRadius, static_cast<doc::FilletFeature&>(feature).radius, width, myFocusFirstField);
}

// --- ChamferEditor ------------------------------------------------------------

bool ChamferEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::ChamferFeature*>(&f) != nullptr; }

std::unique_ptr<doc::EdgeFeature> ChamferEditor::Create(Session& session) const {
    auto f = std::make_unique<doc::ChamferFeature>();
    f->distance.name = session.doc.NextParameterName();
    return f;
}

void ChamferEditor::LoadTexts(const doc::Feature& feature) {
    myDistance = static_cast<const doc::ChamferFeature&>(feature).distance.expression;
}

void ChamferEditor::DrawSize(AppContext& app, Session& session, doc::EdgeFeature& feature, float width) {
    Section(app, Tr("Расстояние"));
    ParameterField(app, session, "distance", myDistance, static_cast<doc::ChamferFeature&>(feature).distance, width,
                   myFocusFirstField);
}

} // namespace bz::app
