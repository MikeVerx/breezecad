#include "app/features/CopyEditors.h"

#include "core/I18n.h"
#include "document/HoleFeature.h"
#include "document/ThreadFeature.h"
#include "document/Origin.h"
#include "document/SketchSolidFeature.h"
#include "document/WorkAxisFeature.h"
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
    // Holes and threads; patterns and mirrors too, so a mirror takes the whole pattern.
    return dynamic_cast<const doc::HoleFeature*>(&f) != nullptr || dynamic_cast<const doc::ThreadFeature*>(&f) != nullptr ||
           dynamic_cast<const doc::CopyFeature*>(&f) != nullptr;
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
    PickSourceInView(app, session, vf);
}

bool CopyEditor::PickSourceInView(AppContext& app, Session& session, const ViewportFrame& vf) {
    // As in KOMPAS: a click on the model takes (or drops) the operation that
    // made that surface.
    auto* f = dynamic_cast<doc::CopyFeature*>(Edited(session));
    if (!f || !vf.hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return false;
    const int index = session.doc.IndexOf(f->id);
    const auto hit = app.viewport.Pick(vf.ToLocal(ImGui::GetMousePos()));
    if (index < 0 || !hit) return false;
    const ObjectId id = session.model.FeatureAt(hit->point, session.doc, static_cast<std::size_t>(index));
    const doc::Feature* source = session.doc.Find(id);
    if (!source || !IsCopyable(*source)) {
        app.notes.Info(Tr("Здесь нет операции, которую можно скопировать"));
        return true;
    }
    if (const auto it = std::find(f->sources.begin(), f->sources.end(), id); it != f->sources.end()) {
        f->sources.erase(it);
    } else {
        f->sources.push_back(id);
        // In document order, as the list shows them.
        std::sort(f->sources.begin(), f->sources.end(), [&](ObjectId a, ObjectId b) { return session.doc.IndexOf(a) < session.doc.IndexOf(b); });
    }
    app.notes.Info(Trf("{}: {}", source->name, std::find(f->sources.begin(), f->sources.end(), id) != f->sources.end()
                                                       ? Tr("выбрано") : Tr("снято")));
    Changed(session);
    return true;
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
    const bool changed = FeatureChecklist(app, session, f.sources, IsCopyable, width,
                                          Tr("Нет операций, которые можно скопировать: выдавливание, вращение или отверстие."));
    Caption(app, Tr("Отметьте в списке или щёлкните операцию прямо на модели."), width);
    return changed;
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
    // X, Y, Z through the origin, or a work axis made before the pattern.
    Section(app, Tr("Ось"));
    std::vector<const doc::WorkAxisFeature*> axes;
    const int index = session.doc.IndexOf(f.id);
    for (int i = 0; i < index; ++i)
        if (const auto* wa = dynamic_cast<const doc::WorkAxisFeature*>(session.doc.Features()[static_cast<std::size_t>(i)].get());
            wa && !wa->suppressed)
            axes.push_back(wa);
    std::vector<const char*> labels(kAxisLabels.begin(), kAxisLabels.end());
    int current = static_cast<int>(f.axis);
    for (std::size_t i = 0; i < axes.size(); ++i) {
        labels.push_back(axes[i]->name.c_str());
        if (axes[i]->id == f.workAxis) current = 3 + static_cast<int>(i);
    }
    const int chosen = ui::Segmented(ui, "axis", labels, current, std::min<int>(static_cast<int>(labels.size()), 3), width);
    if (chosen != current) {
        if (chosen < 3) {
            f.axis = static_cast<ModelAxis>(chosen);
            f.workAxis = {};
        } else {
            f.workAxis = axes[static_cast<std::size_t>(chosen - 3)]->id;
        }
        Changed(session);
    }
    if (axes.empty()) Caption(app, Tr("X, Y, Z проходят через начало координат. Другую ось задайте командой «Ось»."), width);
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
    auto* f = dynamic_cast<doc::MirrorFeature*>(Edited(session));
    if (!f) return;
    const ObjectId plane = f->plane;
    PickPlane(app, session, f->plane, vf);
    if (f->plane == plane && !f->wholeBody) PickSourceInView(app, session, vf);   // a click not on a plane
}

void MirrorEditor::DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) {
    auto& f = static_cast<doc::MirrorFeature&>(feature);
    Section(app, Tr("Что отражать"));
    static constexpr std::array<const char*, 2> what{N_("Операции"), N_("Тело целиком")};
    const int current = f.wholeBody ? 1 : 0;
    if (const int chosen = ui::Segmented(app.ui, "mirror-what", what, current, 2, width); chosen != current) {
        f.wholeBody = chosen == 1;
        Changed(session);
    }
    Gap(app);
    if (!f.wholeBody) DrawSources(app, session, f, width);
    Gap(app);
    Section(app, Tr("Плоскость симметрии"));
    PlaneChoice(app, session, f.plane, width);
}

} // namespace bz::app
