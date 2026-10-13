#include "app/features/HoleEditor.h"

#include "app/Format.h"
#include "app/sketch/SketchView.h"
#include "core/I18n.h"
#include "ui/Widgets.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <numbers>

namespace bz::app {
namespace {

using doc::HoleType;

constexpr float kCentrePickRadius = 9.0f;   ///< design px around a centre that hit it

bool HasFace(const doc::HoleFeature& f) { return f.face.IsSet(); }

/// True when a picked planar face lies in the plane of the hole's face.
bool OnSamePlane(const doc::FaceReference& face, const Frame& plane, Vec3 point) {
    return plane.normal.Dot(face.normal) > 0.999 && std::abs((point - face.point).Dot(face.normal)) < 1e-3;
}

/// A clicked coordinate rounded to a step that matches the zoom, so a centre
/// placed by eye reads 25 rather than 24.9731.
double RoundForZoom(double value, double mmPerPixel) {
    const double step = std::pow(10.0, std::floor(std::log10(std::max(mmPerPixel * 10.0, 1e-6))));
    return std::round(value / step) * step;
}

/// Number typed in the document units ("12,5" works too); nullopt if not a number.
std::optional<double> ParseLength(std::string text, const UnitSystem& units) {
    for (char& ch : text)
        if (ch == ',') ch = '.';
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str()) return std::nullopt;
    while (*end == ' ') ++end;
    if (*end != '\0' || !std::isfinite(value)) return std::nullopt;
    return units.ToInternal(value, Quantity::Length);
}

/// Circle of `diameter` around `centre` in the hole plane, as a screen polyline.
void DrawPlaneCircle(ImDrawList* dl, const SketchView& view, Vec2 centre, double diameter, ImU32 colour, float thickness) {
    constexpr int kSegments = 48;
    std::array<ImVec2, kSegments> points;
    for (int i = 0; i < kSegments; ++i) {
        const double a = 2.0 * std::numbers::pi * i / kSegments;
        points[static_cast<std::size_t>(i)] =
            view.ToScreen({centre.x + diameter / 2 * std::cos(a), centre.y + diameter / 2 * std::sin(a)});
    }
    dl->AddPolyline(points.data(), kSegments, colour, ImDrawFlags_Closed, thickness);
}

/// Widest diameter of the hole at the surface (what the user sees).
double OuterDiameter(const doc::HoleFeature& f) {
    switch (f.type) {
        case HoleType::Counterbore: return f.headDiameter.value;
        case HoleType::Countersink: return f.sinkDiameter.value;
        case HoleType::Simple: break;
    }
    return f.diameter.value;
}

} // namespace

// --- lifecycle --------------------------------------------------------------

doc::HoleFeature* HoleEditor::Feature(Session& session) const { return dynamic_cast<doc::HoleFeature*>(Edited(session)); }

bool HoleEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::HoleFeature*>(&f) != nullptr; }

std::string HoleEditor::Hint() const {
    if (myCoaxial)
        return Tr("Щёлкните круглую кромку или цилиндрическую грань — отверстие встанет соосно с ней. Enter — готово, Esc — отмена");
    return Tr("Щёлкните плоскую грань — там встанет центр; ещё щелчки добавляют центры, щелчок по центру убирает. "
           "Enter — готово, Esc — отмена");
}

void HoleEditor::StartNew(AppContext& app, Session& session) {
    if (session.model.Bodies().empty()) {
        app.notes.Info(Tr("Сначала создайте тело: эскиз и выдавливание или вращение"));
        return;
    }
    auto f = std::make_unique<doc::HoleFeature>();
    f->diameter.name = session.doc.NextParameterName();
    myHoveredCentre.reset();
    OpenNew(app, session, std::move(f));
}

void HoleEditor::StartNext(AppContext& app, Session& session, const doc::Feature& previous) {
    // The next hole keeps the type and sizes, like a second click of the tool.
    const auto& prev = static_cast<const doc::HoleFeature&>(previous);
    StartNew(app, session);
    doc::HoleFeature* next = Feature(session);
    if (!next) return;
    next->type = prev.type;
    next->throughAll = prev.throughAll;
    for (auto [to, from] : {std::pair{&next->diameter, &prev.diameter}, {&next->depth, &prev.depth},
                            {&next->headDiameter, &prev.headDiameter}, {&next->headDepth, &prev.headDepth},
                            {&next->sinkDiameter, &prev.sinkDiameter}})
        to->expression = from->expression;
    LoadTexts(*next);
    Changed(session);
}

bool HoleEditor::ShowStatus(const doc::Feature& feature) const {
    // "Укажите центр" while nothing is placed yet just repeats the caption.
    return !static_cast<const doc::HoleFeature&>(feature).centres.empty();
}

void HoleEditor::LoadTexts(const doc::Feature& feature) {
    const auto& f = static_cast<const doc::HoleFeature&>(feature);
    myDiameter = f.diameter.expression;
    myDepth = f.depth.expression;
    myHeadDiameter = f.headDiameter.expression;
    myHeadDepth = f.headDepth.expression;
    mySinkDiameter = f.sinkDiameter.expression;
}

// --- viewport -----------------------------------------------------------------

void HoleEditor::DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) {
    doc::HoleFeature* f = Feature(session);
    if (!f) return;
    myHoveredCentre.reset();
    if (myCoaxial)
        if (const int index = session.doc.IndexOf(f->id); index >= 0)
            app.viewport.BeginPickingOriginal(session.model, static_cast<std::size_t>(index), render::PickTarget::EdgesAndFaces);

    if (HasFace(*f)) {
        const SketchView view{doc::PlaneFrame(f->face), vf};
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ui::ThemeColors& c = app.ui.C();
        const ImVec2 mouse = ImGui::GetMousePos();
        const float pickRadius = app.ui.S(kCentrePickRadius);

        for (std::size_t i = 0; i < f->centres.size(); ++i) {
            const ImVec2 p = view.ToScreen(f->centres[i]);
            const float dx = p.x - mouse.x, dy = p.y - mouse.y;
            if (vf.hovered && dx * dx + dy * dy < pickRadius * pickRadius) myHoveredCentre = static_cast<int>(i);
        }
        for (std::size_t i = 0; i < f->centres.size(); ++i) {
            const bool hovered = myHoveredCentre == static_cast<int>(i);
            // Half transparent: the preview's own hole edge stays visible under it.
            const ImU32 ink = ui::ToU32(hovered ? c.danger : c.accent.WithAlpha(0.55f));
            const Vec2 centre = f->centres[i];
            DrawPlaneCircle(dl, view, centre, f->diameter.value, ink, app.ui.S(1.5f));
            if (f->type != HoleType::Simple) DrawPlaneCircle(dl, view, centre, OuterDiameter(*f), ink, app.ui.S(1.0f));
            // Centre mark.
            const ImVec2 p = view.ToScreen(centre);
            const float arm = app.ui.S(5);
            dl->AddLine({p.x - arm, p.y}, {p.x + arm, p.y}, ink, app.ui.S(1.5f));
            dl->AddLine({p.x, p.y - arm}, {p.x, p.y + arm}, ink, app.ui.S(1.5f));
        }
        if (myHoveredCentre) ImGui::SetTooltip("%s", Tr("Убрать центр"));

        // Depth arrow from the first centre into the body.
        if (!f->centres.empty() && !f->throughAll && !f->depth.HasError()) {
            const Vec3 base = view.frame.ToWorld(f->centres.front());
            const std::string label = FormatValue(f->depth.value, Quantity::Length, session.doc.units, 2) + " " +
                                      UnitLabel(Quantity::Length, session.doc.units);
            if (const auto depth = myArrow.Update(app, vf, base, view.frame.normal * -1.0, f->depth.value, label)) {
                myDepth = FormatValue(std::max(*depth, DragStep(vf.projection.mmPerPixel)), Quantity::Length,
                                      session.doc.units, 3);
                f->depth.expression = myDepth;
                Changed(session);
            }
            if (myArrow.Hot()) {
                myHoveredCentre.reset();
                return;
            }
        } else {
            myArrow.Release();
        }
    }

    if (!vf.hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    if (myHoveredCentre) {
        f->RemoveCentre(static_cast<std::size_t>(*myHoveredCentre));
        myHoveredCentre.reset();
        Changed(session);
        return;
    }
    if (myCoaxial) ClickCoaxial(app, session, *f, vf);
    else ClickModel(app, session, *f, vf);
}

void HoleEditor::ClickCoaxial(AppContext& app, Session& session, doc::HoleFeature& f, const ViewportFrame& vf) {
    const Vec2 at = vf.ToLocal(ImGui::GetMousePos());
    doc::HoleAxis axis;
    Vec3 axisDir;
    std::optional<Vec3> onAxis;   // a point of the axis, when known now (a cylinder)
    if (const auto edge = app.viewport.PickEdge(at); edge && edge->curve == doc::CurveKind::Circle) {
        axis.edge = *edge;
        axisDir = edge->direction;
        // No face yet: the flat face the rim lies on (the top of a boss).
        if (!HasFace(f))
            for (const doc::FaceSignature& s : edge->faces)
                if (s.kind == doc::SurfaceKind::Plane && std::abs(s.direction.Dot(edge->direction)) > 0.999) {
                    f.face = {s.direction, edge->middle};
                    break;
                }
    } else if (const auto cylinder = app.viewport.PickCylinder(at)) {
        axis.cylinder = doc::CylinderTouch{cylinder->axisPoint, cylinder->axisDir, cylinder->radius, {}};
        axisDir = cylinder->axisDir;
        onAxis = cylinder->axisPoint;
    } else if (const auto face = app.viewport.PickFace(at); face && !HasFace(f)) {
        f.face = *face;   // the face first; the round edge or cylinder next
        Changed(session);
        return;
    } else {
        app.notes.Info(Tr("Щёлкните круглую кромку или цилиндрическую грань"));
        return;
    }
    if (!HasFace(f)) {
        app.notes.Info(Tr("Сначала щёлкните плоскую грань, на которой будет отверстие"));
        return;
    }
    if (std::abs(axisDir.Normalized().Dot(f.face.normal)) < 0.999) {
        app.notes.Info(Tr("Ось кромки или цилиндра не перпендикулярна грани отверстия"));
        return;
    }
    const Frame plane = doc::PlaneFrame(f.face);
    // Where the axis crosses the face; for an edge the rebuild puts it there
    // (its centre is not known here), meanwhile a point of the rim.
    Vec3 at3 = axis.edge ? axis.edge->middle : *onAxis;
    if (onAxis) {
        const Vec3 d = axisDir.Normalized();
        at3 = *onAxis + d * ((plane.origin - *onAxis).Dot(plane.normal) / d.Dot(plane.normal));
    }
    axis.centre = f.centres.size();
    f.centres.push_back(plane.ToLocal(at3));
    f.concentric.push_back(axis);
    Changed(session);
}

void HoleEditor::ClickModel(AppContext& app, Session& session, doc::HoleFeature& f, const ViewportFrame& vf) {
    const auto hit = app.viewport.Pick(vf.ToLocal(ImGui::GetMousePos()));
    if (!hit) return;
    if (!hit->plane) {
        app.notes.Info(Tr("Отверстие ставится на плоскую грань"));
        return;
    }
    if (!HasFace(f) || (f.centres.empty() && !OnSamePlane(f.face, *hit->plane, hit->point))) {
        f.face = {hit->plane->normal, hit->point};
    } else if (!OnSamePlane(f.face, *hit->plane, hit->point)) {
        app.notes.Info(Tr("Все центры отверстия лежат на одной грани. Чтобы выбрать другую, нажмите «Другая грань»"));
        return;
    }
    const Vec2 local = doc::PlaneFrame(f.face).ToLocal(hit->point);
    const double mmPerPixel = vf.projection.mmPerPixel;
    f.centres.push_back({RoundForZoom(local.x, mmPerPixel), RoundForZoom(local.y, mmPerPixel)});
    Changed(session);
}

// --- panel --------------------------------------------------------------------

void HoleEditor::DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) {
    auto& f = static_cast<doc::HoleFeature&>(feature);
    DrawFaceSection(app, session, f, width);
    Gap(app);
    DrawSizes(app, session, f, width);
}

void HoleEditor::DrawFaceSection(AppContext& app, Session& session, doc::HoleFeature& f, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    Section(app, Tr("Положение"));
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ui.S(34);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool ready = HasFace(f) && !f.centres.empty();
        dl->AddRectFilled(p, {p.x + width, p.y + h}, ui::ToU32(c.accentSoft), ui.S(8));
        dl->AddRect(p, {p.x + width, p.y + h}, ui::ToU32(c.accentSoftBorder), ui.S(8));
        ui.icons.Draw(dl, ready ? "ui-check" : "status-warning", {p.x + ui.S(10), p.y + (h - ui.S(14)) / 2}, ui.S(14), c.accent);
        const std::string chip = !HasFace(f) ? std::string(Tr("Грань не выбрана"))
                                             : Trf("Грань выбрана \xC2\xB7 центров: {}", f.centres.size());
        dl->AddText(ui.fonts.medium, ui.S(13), {p.x + ui.S(32), p.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(c.text), chip.c_str());
        ImGui::Dummy({width, h});
    }
    {
        static constexpr std::array<const char*, 2> modes{N_("Центр по щелчку"), N_("Соосно")};
        const int current = myCoaxial ? 1 : 0;
        if (const int chosen = ui::Segmented(ui, "centre-mode", modes, current, 2, width); chosen != current) myCoaxial = chosen == 1;
        ImGui::Dummy({0, ui.S(4)});
    }
    if (myCoaxial)
        Caption(app,
                Tr("Щёлкните круглую кромку или цилиндрическую грань: центр встанет на её ось и будет следовать за ней. "
                   "Сначала можно указать плоскую грань для отверстия; кромка на плоской грани выберет её сама."),
                width);
    else
        Caption(app,
                HasFace(f) ? Tr("Щёлкните по грани, чтобы добавить центр; щелчок по центру убирает его. Координаты можно ввести точно.")
                           : Tr("Щёлкните плоскую грань тела там, где нужно отверстие."),
                width);
    if (HasFace(f)) {
        DrawCentres(app, session, f, width);
        if (ui::LinkButton(ui, "##other-face", Tr("Другая грань"))) {
            f.face = {};
            f.centres.clear();
            f.concentric.clear();
            Changed(session);
        }
    }
}

void HoleEditor::DrawCentres(AppContext& app, Session& session, doc::HoleFeature& f, float width) {
    ui::Ui& ui = app.ui;
    const UnitSystem& units = session.doc.units;
    const float gap = ui.S(6);
    const float remove = ui.S(28);
    const float label = ui.S(18);
    const float field = (width - remove - 2 * label - 3 * gap) / 2;
    std::optional<std::size_t> erase;

    for (std::size_t i = 0; i < f.centres.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        Vec2& centre = f.centres[i];
        const auto coordinate = [&](const char* name, const char* id, double& value) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(name);
            ImGui::SameLine(0, 0);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + label - ImGui::CalcTextSize(name).x);
            // A visible field frame: the bare table input would read as plain text here.
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, {p.x + field, p.y + ui.S(28)}, ui::ToU32(ui.C().field), ui.S(5));
            dl->AddRect(p, {p.x + field, p.y + ui.S(28)}, ui::ToU32(ui.C().fieldBorder), ui.S(5));
            if (ui::CommitInput(ui, id, FormatValue(value, Quantity::Length, units), app.shell.edit, field, true))
                if (const auto parsed = ParseLength(app.shell.edit.text, units)) {
                    value = *parsed;
                    Changed(session);
                }
        };
        if (const doc::HoleAxis* axis = f.AxisOf(i)) {
            // Held on an axis: the rebuild puts it there, typing would not stick.
            ImGui::AlignTextToFramePadding();
            const float w = width - remove - gap;
            ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(ui.C().textSecondary));
            ImGui::TextUnformatted(axis->edge ? Tr("Соосно с кромкой") : Tr("Соосно с цилиндром"));
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 0);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, w - ImGui::GetItemRectSize().x));
        } else {
            coordinate("X", "##x", centre.x);
            ImGui::SameLine(0, gap);
            coordinate("Y", "##y", centre.y);
            ImGui::SameLine(0, gap);
        }
        if (ui::IconButton(ui, "##remove", "ui-close", 28, 12, Tr("Убрать центр"))) erase = i;
        ImGui::PopID();
    }
    if (erase) {
        f.RemoveCentre(*erase);
        Changed(session);
    }
}

void HoleEditor::DrawSizes(AppContext& app, Session& session, doc::HoleFeature& f, float width) {
    ui::Ui& ui = app.ui;

    Section(app, Tr("Тип"));
    {
        static constexpr std::array<const char*, 3> labels{N_("Простое"), N_("Цековка"), N_("Зенковка")};
        const int current = static_cast<int>(f.type);
        const int chosen = ui::Segmented(ui, "type", labels, current, 3, width);
        if (chosen != current) {
            f.type = static_cast<HoleType>(chosen);
            Changed(session);
        }
    }
    Gap(app);

    Section(app, Tr("Диаметр"));
    // Not focused on open: it is below the centres, and focusing would scroll them away.
    ParameterField(app, session, "diameter", myDiameter, f.diameter, width, false);
    Gap(app);

    if (f.type == HoleType::Counterbore) {
        Section(app, Tr("Диаметр цековки"));
        ParameterField(app, session, "head-diameter", myHeadDiameter, f.headDiameter, width, false);
        Gap(app);
        Section(app, Tr("Глубина цековки"));
        ParameterField(app, session, "head-depth", myHeadDepth, f.headDepth, width, false);
        Gap(app);
    } else if (f.type == HoleType::Countersink) {
        Section(app, Tr("Диаметр зенковки (90\xC2\xB0)"));
        ParameterField(app, session, "sink-diameter", mySinkDiameter, f.sinkDiameter, width, false);
        Gap(app);
    }

    Section(app, Tr("Глубина"));
    {
        static constexpr std::array<const char*, 2> labels{N_("На расстояние"), N_("Насквозь")};
        const int current = f.throughAll ? 1 : 0;
        const int chosen = ui::Segmented(ui, "depth-kind", labels, current, 2, width);
        if (chosen != current) {
            f.throughAll = chosen == 1;
            Changed(session);
        }
    }
    if (!f.throughAll) {
        ImGui::Dummy({0, ui.S(6)});
        ParameterField(app, session, "depth", myDepth, f.depth, width, false);
    }
}

} // namespace bz::app
