#include "app/features/LoftEditor.h"

#include "app/sketch/SketchOverlay.h"
#include "document/SketchFeature.h"
#include "ui/Widgets.h"

#include <array>

namespace bz::app {
namespace {

bool IsSketch(const doc::Feature& f) { return dynamic_cast<const doc::SketchFeature*>(&f) != nullptr; }

} // namespace

bool LoftEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::LoftFeature*>(&f) != nullptr; }

std::string LoftEditor::Hint() const {
    return "Отметьте эскизы-сечения (по одному замкнутому контуру в каждом). Enter — готово, Esc — отмена";
}

void LoftEditor::StartNew(AppContext& app, Session& session) {
    // Start with the last two sketches: the usual case is "this one to that one".
    std::vector<ObjectId> sketches;
    for (std::size_t i = 0; i < session.doc.EndMarker(); ++i)
        if (IsSketch(*session.doc.Features()[i])) sketches.push_back(session.doc.Features()[i]->id);
    if (sketches.size() < 2) {
        app.notes.Info("Для лофта нужны хотя бы два эскиза — например, на рабочих плоскостях одна над другой");
        return;
    }
    auto f = std::make_unique<doc::LoftFeature>();
    f->sections = {sketches[sketches.size() - 2], sketches.back()};
    f->operation = session.model.Bodies().empty() ? doc::BooleanOp::NewBody : doc::BooleanOp::Join;
    OpenNew(app, session, std::move(f));
}

void LoftEditor::DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) {
    // The chosen sections, so it is clear what the loft goes through.
    auto* f = dynamic_cast<doc::LoftFeature*>(Edited(session));
    if (!f) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    SketchDrawStyle style;
    style.passive = true;
    for (ObjectId id : f->sections)
        if (const auto* sk = session.doc.FindAs<doc::SketchFeature>(id))
            DrawSketch(app.ui, dl, *sk, {sk->placement.frame, vf}, session.doc.units, style);
}

void LoftEditor::DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) {
    auto& f = static_cast<doc::LoftFeature&>(feature);
    Section(app, "Операция");
    static constexpr std::array<const char*, 3> ops{"Новое тело", "Добавить", "Вырезать"};
    const bool bodyBefore = HasSolidBefore(session.doc, static_cast<std::size_t>(session.doc.IndexOf(f.id)));
    const int current = std::min(static_cast<int>(f.operation), 2);
    const int chosen = ui::Segmented(app.ui, "op", ops, current, 3, width, bodyBefore ? 0 : 0b110);
    if (chosen != current) {
        f.operation = static_cast<doc::BooleanOp>(chosen);
        Changed(session);
    }
    Gap(app);

    Section(app, "Сечения (по порядку дерева)");
    FeatureChecklist(app, session, f.sections, IsSketch, width, "Нет эскизов. Нарисуйте сечения на разных плоскостях.");
    Gap(app);

    Section(app, "Переходы");
    static constexpr std::array<const char*, 2> kinds{"Плавные", "Прямые"};
    const int ruled = f.ruled ? 1 : 0;
    const int pick = ui::Segmented(app.ui, "ruled", kinds, ruled, 2, width);
    if (pick != ruled) {
        f.ruled = pick == 1;
        Changed(session);
    }
}

} // namespace bz::app
