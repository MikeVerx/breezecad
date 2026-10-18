#include "app/features/ExtrudeEditor.h"
#include "app/features/ToolChoice.h"

#include "app/Format.h"
#include "app/sketch/SketchView.h"
#include "core/I18n.h"
#include "document/Origin.h"
#include "document/SketchFeature.h"
#include "ui/Widgets.h"

#include <array>
#include <cmath>
#include <span>

namespace bz::app {

using doc::BooleanOp;
using doc::ExtentType;

bool ExtrudeEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::ExtrudeFeature*>(&f) != nullptr; }

std::string ExtrudeEditor::Hint() const {
    return Tr("Щёлкните по контурам эскиза, введите расстояние. Enter или щелчок колесом — готово, Esc — отмена");
}

std::unique_ptr<doc::SketchSolidFeature> ExtrudeEditor::Create(Session& session, ObjectId sketch) const {
    (void)sketch;
    auto f = std::make_unique<doc::ExtrudeFeature>();
    f->distance.name = session.doc.NextParameterName();
    f->distance.expression = "10";
    return f;
}

void ExtrudeEditor::LoadTexts(const doc::Feature& feature) {
    const auto& f = static_cast<const doc::ExtrudeFeature&>(feature);
    myDistance = f.distance.expression;
    myTaper = f.taper.expression;
    myDistance2 = f.distance2.expression;
}

void ExtrudeEditor::CarryOver(const doc::SketchSolidFeature& previous, doc::SketchSolidFeature& next) const {
    const auto& prev = static_cast<const doc::ExtrudeFeature&>(previous);
    auto& f = static_cast<doc::ExtrudeFeature&>(next);
    f.extent = prev.extent;
    f.reversed = prev.reversed;
    f.distance2.expression = prev.distance2.expression;
}

bool ExtrudeEditor::PickInViewport(AppContext& app, Session& session, doc::SketchSolidFeature& feature, const SketchView& view) {
    auto& f = static_cast<doc::ExtrudeFeature&>(feature);
    if (f.extent == ExtentType::ToPlane) {
        myArrow.Release();
        if (!f.toPlane && !f.toFace.IsSet()) myPickTarget = true;
        if (!myPickTarget) return false;
        // A flat face of the body under the cursor; elsewhere an origin or
        // work plane. Planes lie over the body, so they are offered only
        // off it: otherwise a big plane would hide the face aimed at.
        const int index = session.doc.IndexOf(f.id);
        if (index < 0) return false;
        app.viewport.BeginPickingOriginal(session.model, static_cast<std::size_t>(index), render::PickTarget::Faces);
        const Vec2 at = view.vf.ToLocal(ImGui::GetMousePos());
        if (view.vf.hovered && app.viewport.OriginalUnder(at)) {
            const std::optional<doc::FaceReference> face = app.viewport.PickFace(at, true);
            if (face && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                f.toFace = *face;
                f.toPlane = {};
                myPickTarget = false;
                Changed(session);
            }
            return true;
        }
        const ObjectId before = f.toPlane;
        PickPlane(app, session, f.toPlane, view.vf);
        if (f.toPlane != before) {
            f.toFace = {};
            myPickTarget = false;
        }
        return true;
    }
    if (f.extent != ExtentType::Asymmetric || f.distance2.HasError()) myArrowBack.Release();
    if (f.extent == ExtentType::ThroughAll || f.distance.HasError()) {
        myArrow.Release();
        return false;
    }
    const std::optional<Vec2> middle = ProfileMiddle(session, f);
    if (!middle) {
        myArrow.Release();
        return false;
    }
    const bool symmetric = f.extent == ExtentType::Symmetric;
    const bool asymmetric = f.extent == ExtentType::Asymmetric;
    const Vec3 dir = view.frame.normal * (f.reversed ? -1.0 : 1.0);
    const double step = DragStep(view.MmPerPixel());
    if (asymmetric && !f.distance2.HasError()) {
        // The other side has its own arrow; neither turns around.
        const std::string back = FormatValue(f.distance2.value, Quantity::Length, session.doc.units, 2) + " " +
                                 UnitLabel(Quantity::Length, session.doc.units);
        if (auto along = myArrowBack.Update(app, view.vf, view.frame.ToWorld(*middle), dir * -1.0, f.distance2.value, back)) {
            myDistance2 = FormatValue(std::max(*along, step), Quantity::Length, session.doc.units, 3);
            f.distance2.expression = myDistance2;
            Changed(session);
        }
        if (myArrowBack.Hot()) return true;
    }
    const double reach = symmetric ? f.distance.value / 2 : f.distance.value;
    const std::string label = FormatValue(f.distance.value, Quantity::Length, session.doc.units, 2) + " " +
                              UnitLabel(Quantity::Length, session.doc.units);
    if (auto along = myArrow.Update(app, view.vf, view.frame.ToWorld(*middle), dir, reach, label)) {
        // Dragged past the sketch plane: the other direction.
        if (*along < 0 && !symmetric && !asymmetric) {
            f.reversed = !f.reversed;
            *along = -*along;
            myArrow.Flipped();
        }
        const double total = std::max(symmetric ? 2 * *along : *along, step);
        myDistance = FormatValue(total, Quantity::Length, session.doc.units, 3);
        f.distance.expression = myDistance;
        Changed(session);
    }
    return myArrow.Hot();
}

void ExtrudeEditor::DrawOptions(AppContext& app, Session& session, doc::SketchSolidFeature& feature, float width) {
    auto& f = static_cast<doc::ExtrudeFeature&>(feature);
    ui::Ui& ui = app.ui;

    // "Through all" only makes sense for cuts.
    if (f.operation != BooleanOp::Cut && f.extent == ExtentType::ThroughAll) {
        f.extent = ExtentType::Distance;
        Changed(session);
    }

    // Direction, four pictures (the author's icons, 18.10): up the sketch's normal, down,
    // both ways equally, both ways by different distances.
    {
        static constexpr std::array<ChoiceItem, 4> dirs{{{"dir-up", N_("Вверх (по нормали эскиза)")},
                                                         {"dir-down", N_("Вниз (против нормали)")},
                                                         {"dir-both", N_("В обе стороны поровну")},
                                                         {"dir-asym", N_("В обе стороны, разные расстояния")}}};
        const int current = f.extent == ExtentType::Asymmetric ? 3 : f.extent == ExtentType::Symmetric ? 2 : f.reversed ? 1 : 0;
        const int chosen = ToolChoice(app, "dir", N_("Направление"), dirs, current, width);
        if (chosen != current) {
            const bool both = f.extent == ExtentType::Symmetric || f.extent == ExtentType::Asymmetric;
            if (chosen <= 1) {
                f.reversed = chosen == 1;
                if (both) f.extent = ExtentType::Distance;
            } else {
                f.extent = chosen == 3 ? ExtentType::Asymmetric : ExtentType::Symmetric;
                if (f.extent == ExtentType::Asymmetric && f.distance2.name.empty()) f.distance2.name = session.doc.NextParameterName();
            }
            Changed(session);
        }
        if (f.extent == ExtentType::Asymmetric && ui::LinkButton(ui, "##flip", Tr("Поменять стороны"))) {
            f.reversed = !f.reversed;
            Changed(session);
        }
    }

    // Depth: a distance, up to a plane or face, or (cuts) through everything.
    {
        static constexpr std::array<ChoiceItem, 3> depths{{{"ext-distance", N_("На расстояние")},
                                                           {"ext-to", N_("До плоскости или грани")},
                                                           {"ext-through", N_("Насквозь (только вырез)")}}};
        const bool cut = f.operation == BooleanOp::Cut;
        const int current = f.extent == ExtentType::ToPlane ? 1 : f.extent == ExtentType::ThroughAll ? 2 : 0;
        const int chosen = ToolChoice(app, "depth", N_("Глубина"), depths, current, width, cut ? 0 : 0b100);
        if (chosen != current) {
            f.extent = chosen == 1 ? ExtentType::ToPlane : chosen == 2 ? ExtentType::ThroughAll : ExtentType::Distance;
            myPickTarget = chosen == 1;
            Changed(session);
        }
    }
    if (f.extent == ExtentType::ToPlane) {
        ImGui::Dummy({0, ui.S(6)});
        std::string target;
        if (f.toPlane == doc::origin::kPlaneXY) {
            target = Tr("Плоскость XY");
        } else if (f.toPlane == doc::origin::kPlaneXZ) {
            target = Tr("Плоскость XZ");
        } else if (f.toPlane == doc::origin::kPlaneYZ) {
            target = Tr("Плоскость YZ");
        } else if (const doc::Feature* p = f.toPlane ? session.doc.Find(f.toPlane) : nullptr) {
            target = p->name;
        } else if (f.toFace.IsSet()) {
            target = f.toFace.curved ? Tr("кривая грань") : Tr("плоская грань");
        }
        if (PickChip(ui, "##to", Tr("До"), target, myPickTarget, width)) myPickTarget = true;
        Caption(app, Tr("Щёлкните плоскость или грань тела, плоскую или кривую: конец выдавливания примет её форму. "
                        "Если грань меньше выдавливания, её поверхность продолжится за края."),
                width);
    }
    Gap(app);

    if (f.extent == ExtentType::Distance || f.extent == ExtentType::Symmetric) {
        Section(app, f.extent == ExtentType::Symmetric ? Tr("Расстояние (всего)") : Tr("Расстояние"));
        ParameterField(app, session, "distance", myDistance, f.distance, width, myFocusFirstField);
        Gap(app);
    }
    if (f.extent == ExtentType::Asymmetric) {
        Section(app, Tr("Расстояние 1"));
        ParameterField(app, session, "distance", myDistance, f.distance, width, myFocusFirstField);
        ImGui::Dummy({0, ui.S(6)});
        Section(app, Tr("Расстояние 2 (в другую сторону)"));
        ParameterField(app, session, "distance2", myDistance2, f.distance2, width, false);
        Gap(app);
    }

    Section(app, Tr("Уклон"));
    ParameterField(app, session, "taper", myTaper, f.taper, width, false, false);
}

} // namespace bz::app
