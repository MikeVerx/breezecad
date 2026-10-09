#include "app/features/ExtrudeEditor.h"

#include "app/Format.h"
#include "app/sketch/SketchView.h"
#include "document/SketchFeature.h"
#include "ui/Widgets.h"

#include <array>
#include <cmath>

namespace bz::app {

using doc::BooleanOp;
using doc::ExtentType;

bool ExtrudeEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::ExtrudeFeature*>(&f) != nullptr; }

std::string ExtrudeEditor::Hint() const {
    return "Щёлкните по контурам эскиза, введите расстояние. Enter или щелчок колесом — готово, Esc — отмена";
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
}

void ExtrudeEditor::CarryOver(const doc::SketchSolidFeature& previous, doc::SketchSolidFeature& next) const {
    const auto& prev = static_cast<const doc::ExtrudeFeature&>(previous);
    auto& f = static_cast<doc::ExtrudeFeature&>(next);
    f.extent = prev.extent;
    f.reversed = prev.reversed;
}

namespace {

/// Where the cursor ray passes closest to the line base + dir * t: that t.
std::optional<double> RayToLine(const ViewportFrame& vf, ImVec2 mouse, Vec3 base, Vec3 dir) {
    Vec3 origin, ray;
    vf.projection.Unproject(vf.ToLocal(mouse), origin, ray);
    const Vec3 w0 = base - origin;
    const double b = dir.Dot(ray), c = ray.Dot(ray), d = dir.Dot(w0), e = ray.Dot(w0);
    const double denom = c - b * b;   // dir is a unit vector
    if (std::abs(denom) < 1e-9) return std::nullopt;   // looking straight along the arrow
    return (b * e - c * d) / denom;
}

/// A round step for dragged values: about 4 pixels at this zoom (1, 2, 5 × 10^n mm).
double DragStep(double mmPerPixel) {
    const double target = std::max(mmPerPixel * 4.0, 1e-3);
    const double power = std::pow(10.0, std::floor(std::log10(target)));
    for (double m : {1.0, 2.0, 5.0})
        if (power * m >= target) return power * m;
    return power * 10.0;
}

} // namespace

bool ExtrudeEditor::PickInViewport(AppContext& app, Session& session, doc::SketchSolidFeature& feature, const SketchView& view) {
    auto& f = static_cast<doc::ExtrudeFeature&>(feature);
    if (f.extent == ExtentType::ThroughAll || f.distance.HasError()) {
        myDragging = false;
        return false;
    }
    // The arrow starts in the middle of the chosen profile.
    const auto* regions = session.model.Regions(f.sketch);
    Vec2 middle{};
    int count = 0;
    if (regions)
        for (const kernel::SketchRegion& r : *regions) {
            const bool chosen = f.profiles.automatic ? r.automatic : std::any_of(f.profiles.regions.begin(), f.profiles.regions.end(),
                                                                                  [&](Vec2 p) { return (p - r.sample).Length() < 1e-9; });
            if (!chosen || r.loops.empty()) continue;
            for (Vec2 p : r.loops[0]) middle = middle + p;
            count += static_cast<int>(r.loops[0].size());
        }
    if (count == 0) return false;
    middle = middle * (1.0 / count);
    const Frame& frame = view.frame;
    const Vec3 base = frame.ToWorld(middle);
    const bool symmetric = f.extent == ExtentType::Symmetric;
    const Vec3 dir = frame.normal * (f.reversed ? -1.0 : 1.0);
    const double reach = symmetric ? f.distance.value / 2 : f.distance.value;
    const Vec3 tip = base + dir * reach;

    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = view.vf.Project(base), b = view.vf.Project(tip);
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool overHandle = std::hypot(mouse.x - b.x, mouse.y - b.y) < ui.S(12);
    const bool hot = overHandle || myDragging;
    app.shell.handleHot = hot;

    // Drawn over everything, so it is there to grab even inside the body.
    const ImU32 ink = ui::ToU32(hot ? c.sketchHover : c.accent);
    dl->AddLine(a, b, ink, ui.S(hot ? 3.0f : 2.2f));
    const float len = std::max(std::hypot(b.x - a.x, b.y - a.y), 1.0f);
    const ImVec2 u{(b.x - a.x) / len, (b.y - a.y) / len}, n{-u.y, u.x};
    const float head = ui.S(14);
    if (len > head * 1.2f)
        dl->AddTriangleFilled(b, {b.x - u.x * head + n.x * head * 0.45f, b.y - u.y * head + n.y * head * 0.45f},
                              {b.x - u.x * head - n.x * head * 0.45f, b.y - u.y * head - n.y * head * 0.45f}, ink);
    dl->AddCircleFilled(b, ui.S(hot ? 7.5f : 6.0f), ui::ToU32(c.panel));
    dl->AddCircle(b, ui.S(hot ? 7.5f : 6.0f), ink, 24, ui.S(2.2f));
    if (myDragging) {
        const std::string text = FormatValue(f.distance.value, Quantity::Length, session.doc.units, 2) + " " +
                                 UnitLabel(Quantity::Length, session.doc.units);
        dl->AddText(ui.fonts.mono, ui.S(13), {b.x + ui.S(12), b.y - ui.S(20)}, ink, text.c_str());
    }

    if (!myDragging && overHandle && view.vf.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (const auto t = RayToLine(view.vf, mouse, base, dir)) {
            myDragging = true;
            myDragOffset = reach - *t;
        }
    }
    if (myDragging) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            myDragging = false;
            return true;
        }
        if (const auto t = RayToLine(view.vf, mouse, base, dir)) {
            const double step = DragStep(view.MmPerPixel());
            double along = std::round((*t + myDragOffset) / step) * step;
            // Dragged past the sketch plane: the other direction.
            if (along < 0 && !symmetric) {
                f.reversed = !f.reversed;
                along = -along;
                myDragOffset = -myDragOffset;
            }
            const double total = std::max(symmetric ? 2 * along : along, step);
            if (std::abs(total - f.distance.value) > step * 0.5) {
                myDistance = FormatValue(total, Quantity::Length, session.doc.units, 3);
                f.distance.expression = myDistance;
                Changed(session);
            }
        }
        return true;
    }
    return overHandle;
}

void ExtrudeEditor::DrawOptions(AppContext& app, Session& session, doc::SketchSolidFeature& feature, float width) {
    auto& f = static_cast<doc::ExtrudeFeature&>(feature);
    ui::Ui& ui = app.ui;

    // "Through all" only makes sense for cuts.
    if (f.operation != BooleanOp::Cut && f.extent == ExtentType::ThroughAll) {
        f.extent = ExtentType::Distance;
        Changed(session);
    }

    Section(app, "Направление");
    {
        static constexpr std::array<const char*, 3> labels{"Прямое", "Обратное", "Симметрично"};
        const int current = f.extent == ExtentType::Symmetric ? 2 : f.reversed ? 1 : 0;
        const int chosen = ui::Segmented(ui, "dir", labels, current, 3, width);
        if (chosen != current) {
            f.reversed = chosen == 1;
            if (chosen == 2) f.extent = ExtentType::Symmetric;
            else if (f.extent == ExtentType::Symmetric) f.extent = ExtentType::Distance;
            Changed(session);
        }
    }
    Gap(app);

    // Depth for cuts: a distance or through everything.
    if (f.operation == BooleanOp::Cut) {
        Section(app, "Глубина");
        static constexpr std::array<const char*, 2> labels{"На расстояние", "Насквозь"};
        const int current = f.extent == ExtentType::ThroughAll ? 1 : 0;
        const int chosen = ui::Segmented(ui, "depth", labels, current, 2, width);
        if (chosen != current) {
            f.extent = chosen == 1 ? ExtentType::ThroughAll : ExtentType::Distance;
            Changed(session);
        }
        Gap(app);
    }

    if (f.extent != ExtentType::ThroughAll) {
        Section(app, f.extent == ExtentType::Symmetric ? "Расстояние (всего)" : "Расстояние");
        ParameterField(app, session, "distance", myDistance, f.distance, width, myFocusFirstField);
        Gap(app);
    }

    Section(app, "Уклон");
    ParameterField(app, session, "taper", myTaper, f.taper, width, false, false);
}

} // namespace bz::app
