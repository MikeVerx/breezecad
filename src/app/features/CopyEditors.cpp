#include "app/features/CopyEditors.h"

#include "core/I18n.h"
#include "document/HoleFeature.h"
#include "document/Origin.h"
#include "document/SketchSolidFeature.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <array>

namespace bz::app {
namespace {

using doc::ModelAxis;
using doc::PatternKind;

/// Operations whose result can be copied (the kernel keeps their tools).
bool IsCopyable(const doc::Feature& f) {
    if (const auto* solid = dynamic_cast<const doc::SketchSolidFeature*>(&f)) return solid->operation != doc::BooleanOp::Intersect;
    return dynamic_cast<const doc::HoleFeature*>(&f) != nullptr;
}

constexpr std::array<const char*, 3> kAxisLabels{"X", "Y", "Z"};

/// Segmented X / Y / Z. Returns true when changed.
bool AxisChoice(ui::Ui& ui, const char* id, ModelAxis& axis, float width) {
    const int current = static_cast<int>(axis);
    const int chosen = ui::Segmented(ui, id, kAxisLabels, current, 3, width);
    if (chosen == current) return false;
    axis = static_cast<ModelAxis>(chosen);
    return true;
}

/// Segmented "Прямо" / "Обратно". Returns true when changed.
bool DirectionChoice(ui::Ui& ui, const char* id, bool& reversed, float width) {
    static constexpr std::array<const char*, 2> labels{N_("Прямо"), N_("Обратно")};
    const int current = reversed ? 1 : 0;
    const int chosen = ui::Segmented(ui, id, labels, current, 2, width);
    if (chosen == current) return false;
    reversed = chosen == 1;
    return true;
}

} // namespace

// --- CopyEditor ---------------------------------------------------------------

void CopyEditor::DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) {
    (void)app;
    (void)session;
    (void)vf;
}

void CopyEditor::ChooseDefaultSource(const Session& session, doc::CopyFeature& f) {
    if (const doc::Feature* selected = session.doc.Find(session.selectedFeature); selected && IsCopyable(*selected)) {
        f.sources = {selected->id};
        return;
    }
    for (std::size_t i = session.doc.EndMarker(); i-- > 0;)
        if (IsCopyable(*session.doc.Features()[i]) && session.doc.IsActive(i)) {
            f.sources = {session.doc.Features()[i]->id};
            return;
        }
}

bool CopyEditor::DrawSources(AppContext& app, Session& session, doc::CopyFeature& f, float width) {
    Section(app, Tr("Что копировать"));
    return FeatureChecklist(app, session, f.sources, IsCopyable, width,
                            Tr("Нет операций, которые можно скопировать: выдавливание, вращение или отверстие."));
}

// --- PatternEditor --------------------------------------------------------------

bool PatternEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::PatternFeature*>(&f) != nullptr; }

std::string PatternEditor::Hint() const {
    return Tr("Отметьте операции, которые нужно повторить, задайте направление, количество и шаг. Enter — готово, Esc — отмена");
}

const char* PatternEditor::Icon() const {
    return myKind == PatternKind::Circular ? "tree-pattern-circular" : "tree-pattern-rect";
}

void PatternEditor::StartNew(AppContext& app, Session& session, PatternKind kind) {
    if (session.model.Bodies().empty()) {
        app.notes.Info(Tr("Сначала создайте тело и операцию, которую нужно повторить"));
        return;
    }
    auto f = std::make_unique<doc::PatternFeature>();
    f->kind = kind;
    ChooseDefaultSource(session, *f);
    OpenNew(app, session, std::move(f));
}

void PatternEditor::LoadTexts(const doc::Feature& feature) {
    const auto& f = static_cast<const doc::PatternFeature&>(feature);
    myKind = f.kind;
    myCount1 = f.count1.expression;
    mySpacing1 = f.spacing1.expression;
    myCount2 = f.count2.expression;
    mySpacing2 = f.spacing2.expression;
    myCircularCount = f.circularCount.expression;
    myAngle = f.angle.expression;
}

void PatternEditor::DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) {
    auto& f = static_cast<doc::PatternFeature&>(feature);
    DrawSources(app, session, f, width);
    Gap(app);

    Section(app, Tr("Вид массива"));
    static constexpr std::array<const char*, 2> kinds{N_("Прямоугольный"), N_("Круговой")};
    const int current = static_cast<int>(f.kind);
    const int chosen = ui::Segmented(app.ui, "kind", kinds, current, 2, width);
    if (chosen != current) {
        f.kind = static_cast<PatternKind>(chosen);
        myKind = f.kind;
        Changed(session);
    }
    Gap(app);
    if (f.kind == PatternKind::Rectangular) DrawRectangular(app, session, f, width);
    else DrawCircular(app, session, f, width);
}

void PatternEditor::DrawRectangular(AppContext& app, Session& session, doc::PatternFeature& f, float width) {
    ui::Ui& ui = app.ui;
    const float half = (width - ui.S(8)) / 2;
    Section(app, Tr("Направление 1"));
    if (AxisChoice(ui, "dir1", f.direction1, half)) Changed(session);
    ImGui::SameLine(0, ui.S(8));
    if (DirectionChoice(ui, "rev1", f.reversed1, half)) Changed(session);
    ImGui::Dummy({0, ui.S(4)});
    Section(app, Tr("Количество и шаг"));
    // Fields are not focused on open: they are below the list of operations,
    // and focusing would scroll it away.
    ParameterField(app, session, "count1", myCount1, f.count1, half, false, false);
    ImGui::SameLine(0, ui.S(8));
    ParameterField(app, session, "spacing1", mySpacing1, f.spacing1, half, false);
    Gap(app);

    Section(app, Tr("Направление 2"));
    static constexpr std::array<const char*, 4> second{N_("Нет"), "X", "Y", "Z"};
    const int current = f.useDirection2 ? static_cast<int>(f.direction2) + 1 : 0;
    const int chosen = ui::Segmented(ui, "dir2", second, current, 4, width);
    if (chosen != current) {
        f.useDirection2 = chosen > 0;
        if (chosen > 0) f.direction2 = static_cast<ModelAxis>(chosen - 1);
        Changed(session);
    }
    if (!f.useDirection2) return;
    ImGui::Dummy({0, ui.S(4)});
    if (DirectionChoice(ui, "rev2", f.reversed2, width)) Changed(session);
    ImGui::Dummy({0, ui.S(4)});
    ParameterField(app, session, "count2", myCount2, f.count2, half, false, false);
    ImGui::SameLine(0, ui.S(8));
    ParameterField(app, session, "spacing2", mySpacing2, f.spacing2, half, false);
}

void PatternEditor::DrawCircular(AppContext& app, Session& session, doc::PatternFeature& f, float width) {
    ui::Ui& ui = app.ui;
    const float half = (width - ui.S(8)) / 2;
    Section(app, Tr("Ось (через начало координат)"));
    if (AxisChoice(ui, "axis", f.axis, width)) Changed(session);
    Gap(app);
    Section(app, Tr("Количество и угол"));
    ParameterField(app, session, "count", myCircularCount, f.circularCount, half, false, false);
    ImGui::SameLine(0, ui.S(8));
    ParameterField(app, session, "angle", myAngle, f.angle, half, false);
    Caption(app, Tr("360° — копии по кругу; меньший угол — первая и последняя на его концах."), width);
}

// --- MirrorEditor ---------------------------------------------------------------

bool MirrorEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::MirrorFeature*>(&f) != nullptr; }

std::string MirrorEditor::Hint() const {
    return Tr("Отметьте операции, которые нужно отразить, и щёлкните плоскость симметрии. Enter — готово, Esc — отмена");
}

void MirrorEditor::StartNew(AppContext& app, Session& session) {
    if (session.model.Bodies().empty()) {
        app.notes.Info(Tr("Сначала создайте тело и операцию, которую нужно отразить"));
        return;
    }
    auto f = std::make_unique<doc::MirrorFeature>();
    f->plane = doc::origin::kPlaneYZ;
    ChooseDefaultSource(session, *f);
    OpenNew(app, session, std::move(f));
}

void MirrorEditor::DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) {
    if (auto* f = dynamic_cast<doc::MirrorFeature*>(Edited(session))) PickPlane(app, session, f->plane, vf);
}

void MirrorEditor::DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) {
    auto& f = static_cast<doc::MirrorFeature&>(feature);
    DrawSources(app, session, f, width);
    Gap(app);
    Section(app, Tr("Плоскость симметрии"));
    PlaneChoice(app, session, f.plane, width);
}

} // namespace bz::app
