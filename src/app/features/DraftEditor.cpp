#include "app/features/DraftEditor.h"

#include "document/Origin.h"
#include "ui/Widgets.h"

#include <array>

namespace bz::app {

bool DraftEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::DraftFeature*>(&f) != nullptr; }

std::string DraftEditor::Hint() const {
    return "Щёлкните боковые грани (повторный щелчок убирает), выберите нейтральную плоскость и угол. Enter — готово, Esc — отмена";
}

void DraftEditor::StartNew(AppContext& app, Session& session) {
    if (session.model.Bodies().empty()) {
        app.notes.Info("Сначала создайте тело: эскиз и выдавливание или вращение");
        return;
    }
    auto f = std::make_unique<doc::DraftFeature>();
    f->plane = doc::origin::kPlaneXY;   // parts usually come out of the mould upward
    f->angle.name = session.doc.NextParameterName();
    OpenNew(app, session, std::move(f));
}

void DraftEditor::LoadTexts(const doc::Feature& feature) { myAngle = static_cast<const doc::DraftFeature&>(feature).angle.expression; }

void DraftEditor::DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) {
    auto* f = dynamic_cast<doc::DraftFeature*>(Edited(session));
    if (!f) return;
    // Faces first: a click on the body picks a face, elsewhere a plane.
    if (!PickOriginalFaces(app, session, f->faces, vf)) PickPlane(app, session, f->plane, vf);
}

void DraftEditor::DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) {
    auto& f = static_cast<doc::DraftFeature&>(feature);
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();

    Section(app, "Грани");
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(34);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool any = !f.faces.empty();
        dl->AddRectFilled(p, {p.x + width, p.y + h}, ui::ToU32(c.accentSoft), ui.S(8));
        dl->AddRect(p, {p.x + width, p.y + h}, ui::ToU32(c.accentSoftBorder), ui.S(8));
        ui.icons.Draw(dl, any ? "ui-check" : "status-warning", {p.x + ui.S(10), p.y + (h - ui.S(14)) / 2}, ui.S(14), c.accent);
        const std::string chip = any ? "Выбрано граней: " + std::to_string(f.faces.size()) : std::string("Грани не выбраны");
        dl->AddText(ui.fonts.medium, ui.S(13), {p.x + ui.S(32), p.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(c.text), chip.c_str());
        ImGui::Dummy({width, h});
    }
    Caption(app, "Щёлкните боковую грань тела, чтобы задать ей уклон; повторный щелчок убирает.", width);
    if (!f.faces.empty() && ui::LinkButton(ui, "##clear-faces", "Очистить выбор")) {
        f.faces.clear();
        Changed(session);
    }
    Gap(app);

    Section(app, "Нейтральная плоскость");
    PlaneChoice(app, session, f.plane, width);
    Gap(app);

    Section(app, "Направление извлечения");
    static constexpr std::array<const char*, 2> labels{"По нормали", "Обратно"};
    const int current = f.reversed ? 1 : 0;
    const int chosen = ui::Segmented(ui, "pull", labels, current, 2, width);
    if (chosen != current) {
        f.reversed = chosen == 1;
        Changed(session);
    }
    Gap(app);

    Section(app, "Угол");
    ParameterField(app, session, "angle", myAngle, f.angle, width, false);
}

} // namespace bz::app
