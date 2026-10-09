#include "app/features/WorkPlaneEditor.h"

#include "app/Format.h"
#include "app/viewport/Planes.h"
#include "document/Origin.h"
#include "ui/Widgets.h"

#include <array>

namespace bz::app {
namespace {

/// What the plane starts from, for the panel.
std::string BaseName(const Session& session, const doc::WorkPlaneFeature& f) {
    if (f.face.IsSet()) return "Грань тела";
    if (f.base == doc::origin::kPlaneXY) return "Плоскость XY";
    if (f.base == doc::origin::kPlaneXZ) return "Плоскость XZ";
    if (f.base == doc::origin::kPlaneYZ) return "Плоскость YZ";
    if (const doc::Feature* base = session.doc.Find(f.base)) return base->name;
    return {};
}

} // namespace

bool WorkPlaneEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::WorkPlaneFeature*>(&f) != nullptr; }

std::string WorkPlaneEditor::Hint() const {
    return "Щёлкните плоскость или плоскую грань, от которой строить, введите смещение. Enter — готово, Esc — отмена";
}

void WorkPlaneEditor::StartNew(AppContext& app, Session& session) {
    auto f = std::make_unique<doc::WorkPlaneFeature>();
    f->offset.name = session.doc.NextParameterName();
    // A plane selected in the tree is the natural start (a plane above a
    // plane); otherwise XY, so the new plane is on screen at once and a
    // click on another plane or a face moves it there.
    const bool planeSelected =
        session.doc.FindAs<doc::WorkPlaneFeature>(session.selectedFeature) || doc::origin::PlaneFrame(session.selectedFeature);
    f->base = planeSelected ? session.selectedFeature : doc::origin::kPlaneXY;
    OpenNew(app, session, std::move(f));
}

void WorkPlaneEditor::LoadTexts(const doc::Feature& feature) {
    myOffset = static_cast<const doc::WorkPlaneFeature&>(feature).offset.expression;
}

void WorkPlaneEditor::DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) {
    auto* f = dynamic_cast<doc::WorkPlaneFeature*>(Edited(session));
    if (!f) return;
    const int index = session.doc.IndexOf(f->id);
    if (index < 0) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mouse = ImGui::GetMousePos();

    // Candidates: origin planes and the work planes before this one.
    std::vector<ShownPlane> planes = OriginPlanes(session);
    const std::vector<ShownPlane> work = WorkPlanes(session, static_cast<std::size_t>(index));
    planes.insert(planes.end(), work.begin(), work.end());
    const ShownPlane* hot = vf.hovered ? PlaneUnderCursor(vf, mouse, planes) : nullptr;
    std::optional<render::PickResult> face;
    if (vf.hovered) face = app.viewport.Pick(vf.ToLocal(mouse));
    if (face && face->plane) hot = nullptr;   // the body wins, as when starting a sketch

    const bool faint = !session.model.Bodies().empty();
    for (const ShownPlane& p : planes) {
        const bool isBase = !f->face.IsSet() && p.id == f->base;
        DrawPlane(app, dl, vf, p, &p == hot, faint && &p != hot && !isBase);   // the base: not faded
    }
    // The plane being made, in the accent colour.
    bool placed = false;
    for (const ShownPlane& p : WorkPlanes(session, static_cast<std::size_t>(index) + 1)) {
        if (p.id != f->id) continue;
        DrawPlane(app, dl, vf, p, true);
        placed = true;
        // Offset arrow from the start to the middle of the new plane.
        const Vec3 dir = p.frame.normal * (f->reversed ? -1.0 : 1.0);
        const Vec3 tip = p.frame.ToWorld(p.centre);
        const std::string label = FormatValue(f->offset.value, Quantity::Length, session.doc.units, 2) + " " +
                                  UnitLabel(Quantity::Length, session.doc.units);
        if (auto offset = myArrow.Update(app, vf, tip - dir * f->offset.value, dir, f->offset.value, label)) {
            if (*offset < 0) {   // dragged past the start: the other side
                f->reversed = !f->reversed;
                *offset = -*offset;
                myArrow.Flipped();
            }
            myOffset = FormatValue(*offset, Quantity::Length, session.doc.units, 3);
            f->offset.expression = myOffset;
            Changed(session);
        }
    }
    if (!placed || f->offset.HasError()) myArrow.Release();
    if (myArrow.Hot()) return;

    if (!vf.hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    if (hot) {
        f->base = hot->id;
        f->face = {};
        Changed(session);
    } else if (face && face->plane) {
        f->face = {face->plane->normal, face->point};
        f->base = {};
        Changed(session);
    }
}

void WorkPlaneEditor::DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) {
    auto& f = static_cast<doc::WorkPlaneFeature&>(feature);
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();

    Section(app, "От чего строить");
    {
        const std::string base = BaseName(session, f);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(34);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, {p.x + width, p.y + h}, ui::ToU32(c.accentSoft), ui.S(8));
        dl->AddRect(p, {p.x + width, p.y + h}, ui::ToU32(c.accentSoftBorder), ui.S(8));
        ui.icons.Draw(dl, base.empty() ? "status-warning" : "ui-check", {p.x + ui.S(10), p.y + (h - ui.S(14)) / 2}, ui.S(14), c.accent);
        const std::string chip = base.empty() ? std::string("Ничего не выбрано") : base;
        dl->AddText(ui.fonts.medium, ui.S(13), {p.x + ui.S(32), p.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(c.text), chip.c_str());
        ImGui::Dummy({width, h});
    }
    Caption(app, "Щёлкните плоскость начала координат, рабочую плоскость или плоскую грань тела.", width);
    Gap(app);

    Section(app, "Смещение");
    ParameterField(app, session, "offset", myOffset, f.offset, width, myFocusFirstField);
    Gap(app);

    Section(app, "Направление");
    static constexpr std::array<const char*, 2> labels{"По нормали", "Обратно"};
    const int current = f.reversed ? 1 : 0;
    const int chosen = ui::Segmented(ui, "direction", labels, current, 2, width);
    if (chosen != current) {
        f.reversed = chosen == 1;
        Changed(session);
    }
}

} // namespace bz::app
