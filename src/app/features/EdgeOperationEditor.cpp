#include "app/features/EdgeOperationEditor.h"

#include "core/I18n.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <array>

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
        for (std::size_t i = 0; i < to.size() && i < from.size(); ++i) {
            to[i]->expression = from[i]->expression;
            if (to[i]->name.empty() && !from[i]->name.empty()) to[i]->name = session.doc.NextParameterName();
        }
        if (auto* chamfer = dynamic_cast<doc::ChamferFeature*>(next))
            if (const auto* before = dynamic_cast<const doc::ChamferFeature*>(&previous)) {
                chamfer->mode = before->mode;
                chamfer->flipped = before->flipped;
            }
        LoadTexts(*next);
        Changed(session);
    }
}

bool EdgeOperationEditor::ShowStatus(const doc::Feature& feature) const {
    // "Выберите рёбра" before any edge is picked just repeats the caption.
    return !static_cast<const doc::EdgeFeature&>(feature).edges.empty();
}

std::string EdgeOperationEditor::Hint() const {
    return myByFace ? Tr("Щёлкните грань — возьмутся все её рёбра (повторный щелчок убирает их), введите размер. Enter — готово, Esc — отмена")
                    : Tr("Щёлкните рёбра модели (повторный щелчок убирает ребро), введите размер. Enter — готово, Esc — отмена");
}

void EdgeOperationEditor::DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) {
    auto& f = static_cast<doc::EdgeFeature&>(feature);
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();

    Section(app, Tr("Щелчок выбирает"));
    {
        static constexpr std::array<const char*, 2> modes{N_("Ребро"), N_("Грань целиком")};
        const int current = myByFace ? 1 : 0;
        if (const int chosen = ui::Segmented(ui, "pick-mode", modes, current, 2, width); chosen != current) myByFace = chosen == 1;
        Caption(app, myByFace ? Tr("Все рёбра грани разом. Убрать отдельные потом можно в режиме «Ребро».")
                              : Tr("Каждое ребро по отдельности. Касательные к нему рёбра скругляются вместе с ним."),
                width);
    }
    Gap(app);

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
    app.viewport.BeginPickingOriginal(session.model, static_cast<std::size_t>(index),
                                      myByFace ? render::PickTarget::Faces : render::PickTarget::Edges);
    app.viewport.SetMarkedEdges(f->edges);

    if (!vf.hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    if (myByFace) {
        // Every edge of the face: added, or all dropped when all were in already.
        const std::vector<doc::EdgeReference> edges = app.viewport.PickFaceEdges(vf.ToLocal(ImGui::GetMousePos()));
        if (edges.empty()) return;
        const auto chosen = [&](const doc::EdgeReference& e) {
            return std::any_of(f->edges.begin(), f->edges.end(), [&](const auto& x) { return SameEdge(x, e); });
        };
        if (std::all_of(edges.begin(), edges.end(), chosen)) {
            std::erase_if(f->edges, [&](const auto& x) {
                return std::any_of(edges.begin(), edges.end(), [&](const auto& e) { return SameEdge(x, e); });
            });
        } else {
            for (const auto& e : edges)
                if (!chosen(e)) f->edges.push_back(e);
        }
        Changed(session);
        return;
    }
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
    const auto& f = static_cast<const doc::ChamferFeature&>(feature);
    myDistance = f.distance.expression;
    myDistance2 = f.distance2.expression;
    myAngle = f.angle.expression;
}

void ChamferEditor::DrawSize(AppContext& app, Session& session, doc::EdgeFeature& feature, float width) {
    auto& f = static_cast<doc::ChamferFeature&>(feature);
    using doc::ChamferMode;
    Section(app, Tr("Способ"));
    {
        static constexpr std::array<const char*, 3> modes{N_("Равные"), N_("С углом"), N_("Два размера")};
        const int current = static_cast<int>(f.mode);
        if (const int chosen = ui::Segmented(app.ui, "chamfer-mode", modes, current, 3, width); chosen != current) {
            f.mode = static_cast<ChamferMode>(chosen);
            // A size shown for the first time gets its name in the variables.
            if (f.mode == ChamferMode::TwoDistances && f.distance2.name.empty()) f.distance2.name = session.doc.NextParameterName();
            if (f.mode == ChamferMode::DistanceAngle && f.angle.name.empty()) f.angle.name = session.doc.NextParameterName();
            Changed(session);
        }
    }
    Gap(app);
    switch (f.mode) {
        case ChamferMode::EqualDistance:
            Section(app, Tr("Расстояние"));
            ParameterField(app, session, "distance", myDistance, f.distance, width, myFocusFirstField);
            return;
        case ChamferMode::DistanceAngle:
            Section(app, Tr("Расстояние"));
            ParameterField(app, session, "distance", myDistance, f.distance, width, myFocusFirstField);
            ImGui::Dummy({0, app.ui.S(6)});
            Section(app, Tr("Угол"));
            ParameterField(app, session, "angle", myAngle, f.angle, width, false);
            Caption(app, Tr("Расстояние откладывается по опорной грани, угол — между фаской и этой гранью (больше 0° и меньше 90°)."),
                    width);
            break;
        case ChamferMode::TwoDistances:
            Section(app, Tr("Расстояние 1"));
            ParameterField(app, session, "distance", myDistance, f.distance, width, myFocusFirstField);
            ImGui::Dummy({0, app.ui.S(6)});
            Section(app, Tr("Расстояние 2"));
            ParameterField(app, session, "distance2", myDistance2, f.distance2, width, false);
            Caption(app, Tr("Расстояние 1 — по опорной грани, расстояние 2 — по другой грани ребра."), width);
            break;
    }
    Gap(app);
    Section(app, Tr("Опорная грань"));
    {
        static constexpr std::array<const char*, 2> sides{N_("Прямо"), N_("Наоборот")};
        const int current = f.flipped ? 1 : 0;
        if (const int chosen = ui::Segmented(app.ui, "chamfer-side", sides, current, 2, width); chosen != current) {
            f.flipped = chosen == 1;
            Changed(session);
        }
        Caption(app, Tr("У ребра две грани. Опорная — та, что смотрит выше (по оси Z); у вертикальных рёбер — "
                        "по оси Y, затем X. Результат виден сразу на модели; «Наоборот» меняет грани местами."),
                width);
    }
}

} // namespace bz::app
