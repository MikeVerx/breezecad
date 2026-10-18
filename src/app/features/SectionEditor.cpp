#include "app/features/SectionEditor.h"
#include "app/features/ToolChoice.h"

#include "core/I18n.h"
#include "document/Origin.h"
#include "ui/Widgets.h"

#include <array>

namespace bz::app {

bool SectionEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::SectionFeature*>(&f) != nullptr; }

std::string SectionEditor::Hint() const {
    return Tr("Выберите плоскость и сторону, которая останется. Enter — готово, Esc — отмена");
}

void SectionEditor::StartNew(AppContext& app, Session& session) {
    if (session.model.Bodies().empty()) {
        app.notes.Info(Tr("Сначала создайте тело: эскиз и выдавливание или вращение"));
        return;
    }
    auto f = std::make_unique<doc::SectionFeature>();
    f->plane = doc::origin::kPlaneXZ;   // the front plane: the usual cut to look inside
    OpenNew(app, session, std::move(f));
}

void SectionEditor::DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) {
    auto* f = dynamic_cast<doc::SectionFeature*>(Edited(session));
    if (!f) return;
    // A flat face of the body (as it was before the section) wins over the planes behind it.
    if (vf.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        if (const std::optional<doc::FaceReference> face = app.viewport.PickFace(vf.ToLocal(ImGui::GetMousePos()))) {
            f->face = *face;
            f->plane = {};
            Changed(session);
            return;
        }
    const ObjectId before = f->plane;
    PickPlane(app, session, f->plane, vf);
    if (f->plane != before) f->face = {};
}

void SectionEditor::DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) {
    auto& f = static_cast<doc::SectionFeature&>(feature);
    Section(app, Tr("Плоскость"));
    const ObjectId before = f.plane;
    PlaneChoice(app, session, f.plane, width);
    if (f.plane != before) {
        f.face = {};
        Changed(session);
    }
    if (f.face.IsSet()) Caption(app, Tr("Сечение по грани детали."), width);
    Caption(app, Tr("Или щёлкните в окне модели рабочую плоскость или плоскую грань детали."), width);
    Gap(app);

    static constexpr std::array<ChoiceItem, 2> sides{{{nullptr, N_("За плоскостью")}, {nullptr, N_("Перед плоскостью")}}};
    const int current = f.flipped ? 1 : 0;
    const int chosen = ToolChoice(app, "side", N_("Остаётся"), sides, current, width);
    if (chosen != current) {
        f.flipped = chosen == 1;
        Changed(session);
    }
    Gap(app);
    Caption(app,
            Tr("Сечение — обычная операция дерева: погасите её, чтобы увидеть деталь целиком, или удалите. "
            "Рёбра и грани среза можно выбирать для следующих операций."),
            width);
}

} // namespace bz::app
