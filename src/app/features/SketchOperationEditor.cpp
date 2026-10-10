#include "app/features/SketchOperationEditor.h"

#include "app/sketch/SketchEditor.h"
#include "app/sketch/SketchOverlay.h"
#include "core/I18n.h"
#include "document/SketchFeature.h"
#include "ui/Widgets.h"

#include <array>

namespace bz::app {
namespace {

using doc::BooleanOp;

bool InsideRegion(const kernel::SketchRegion& region, Vec2 p) {
    bool inside = false;
    for (const auto& loop : region.loops)
        for (std::size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++) {
            const Vec2 a = loop[j], b = loop[i];
            if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) inside = !inside;
        }
    return inside;
}

/// Index of the region containing a profile sample point, or -1.
int RegionOf(const std::vector<kernel::SketchRegion>& regions, Vec2 p) {
    for (std::size_t i = 0; i < regions.size(); ++i)
        if (InsideRegion(regions[i], p)) return static_cast<int>(i);
    return -1;
}

/// True when some feature of the document reads `sketch`.
bool IsUsed(const doc::Document& d, ObjectId sketch) {
    for (const auto& f : d.Features())
        for (ObjectId dep : f->Dependencies())
            if (dep == sketch) return true;
    return false;
}

/// The last sketch in the active part that no operation uses yet.
ObjectId LastUnusedSketch(const doc::Document& d) {
    for (std::size_t i = d.EndMarker(); i-- > 0;) {
        const auto* sk = dynamic_cast<const doc::SketchFeature*>(d.Features()[i].get());
        if (sk && !IsUsed(d, sk->id)) return sk->id;
    }
    return {};
}

} // namespace

// --- lifecycle --------------------------------------------------------------

doc::SketchSolidFeature* SketchOperationEditor::Feature(Session& session) const {
    return dynamic_cast<doc::SketchSolidFeature*>(Edited(session));
}

std::optional<Vec2> SketchOperationEditor::ProfileMiddle(const Session& session, const doc::SketchSolidFeature& f) {
    const auto* regions = session.model.Regions(f.sketch);
    if (!regions) return std::nullopt;
    std::vector<bool> chosen(regions->size(), false);
    for (std::size_t i = 0; i < regions->size(); ++i) chosen[i] = f.profiles.automatic && (*regions)[i].automatic;
    if (!f.profiles.automatic)
        for (const Vec2 p : f.profiles.regions)
            if (const int r = RegionOf(*regions, p); r >= 0) chosen[static_cast<std::size_t>(r)] = true;
    // Area centroid of the outer loops (shoelace), so dense arcs do not pull it.
    double area = 0.0;
    Vec2 moment{};
    for (std::size_t i = 0; i < regions->size(); ++i) {
        if (!chosen[i] || (*regions)[i].loops.empty()) continue;
        const auto& loop = (*regions)[i].loops[0];
        double a = 0.0;
        Vec2 m{};
        for (std::size_t k = 0, j = loop.size() - 1; k < loop.size(); j = k++) {
            const double cross = loop[j].x * loop[k].y - loop[k].x * loop[j].y;
            a += cross;
            m = m + (loop[j] + loop[k]) * cross;
        }
        const double sign = a < 0 ? -1.0 : 1.0;   // loops may wind either way
        area += a * sign;
        moment = moment + m * sign;
    }
    if (std::abs(area) < 1e-12) return std::nullopt;
    return moment * (1.0 / (3.0 * area));
}

void SketchOperationEditor::StartNew(AppContext& app, Session& session, ObjectId sketch) {
    if (session.mode == EditMode::Sketch && !sketch) sketch = session.activeSketch;
    if (!sketch && session.doc.FindAs<doc::SketchFeature>(session.selectedFeature)) sketch = session.selectedFeature;
    if (!sketch) sketch = LastUnusedSketch(session.doc);
    if (sketch) sketch = ChooseSketch(session, sketch);
    if (!sketch) {
        app.notes.Info(Tr("Сначала нарисуйте эскиз с замкнутым контуром"));
        return;
    }
    std::unique_ptr<doc::SketchSolidFeature> f = Create(session, sketch);
    f->sketch = sketch;
    f->operation = HasSolidBefore(session.doc, session.doc.EndMarker()) ? BooleanOp::Join : BooleanOp::NewBody;
    // As in Inventor: a lone closed contour is taken at once; with several,
    // nothing is chosen until the user clicks the ones to use.
    f->profiles = {false, {}};
    if (const auto* regions = session.model.Regions(sketch); regions && regions->size() == 1)
        f->profiles.regions.push_back(regions->front().sample);
    myHoveredRegion = -1;
    OpenNew(app, session, std::move(f));
}

void SketchOperationEditor::StartNext(AppContext& app, Session& session, const doc::Feature& previous) {
    // Ready for the next profile of the same sketch, like "Применить" in Inventor.
    const auto& prev = static_cast<const doc::SketchSolidFeature&>(previous);
    StartNew(app, session, prev.sketch);
    if (doc::SketchSolidFeature* next = Feature(session)) {
        next->profiles = {false, {}};
        next->operation = prev.operation == BooleanOp::NewBody ? BooleanOp::Join : prev.operation;
        CarryOver(prev, *next);
        LoadTexts(*next);
        Changed(session);
    }
}

ObjectId SketchOperationEditor::ChooseSketch(const Session& session, ObjectId proposed) const {
    (void)session;
    return proposed;
}

void SketchOperationEditor::CarryOver(const doc::SketchSolidFeature& previous, doc::SketchSolidFeature& next) const {
    (void)previous;
    (void)next;
}

bool SketchOperationEditor::PickInViewport(AppContext& app, Session& session, doc::SketchSolidFeature& feature,
                                           const SketchView& view) {
    (void)app;
    (void)session;
    (void)feature;
    (void)view;
    return false;
}

// --- viewport -----------------------------------------------------------------

void SketchOperationEditor::DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) {
    doc::SketchSolidFeature* f = Feature(session);
    if (!f) return;
    const auto* sk = session.doc.FindAs<doc::SketchFeature>(f->sketch);
    if (!sk) return;
    const std::vector<kernel::SketchRegion>* regions = session.model.Regions(f->sketch);

    const SketchView view{sk->placement.frame, vf};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ui::ThemeColors& c = app.ui.C();

    // Which regions are chosen now.
    std::vector<bool> chosen(regions ? regions->size() : 0, false);
    if (regions) {
        for (std::size_t i = 0; i < regions->size(); ++i) chosen[i] = f->profiles.automatic && (*regions)[i].automatic;
        if (!f->profiles.automatic)
            for (const Vec2 p : f->profiles.regions)
                if (const int r = RegionOf(*regions, p); r >= 0) chosen[static_cast<std::size_t>(r)] = true;
    }

    myHoveredRegion = -1;
    if (regions && vf.hovered)
        if (const auto cursor = view.FromScreen(ImGui::GetMousePos())) myHoveredRegion = RegionOf(*regions, *cursor);

    if (regions) {
        for (std::size_t i = 0; i < regions->size(); ++i) {
            const bool hovered = static_cast<int>(i) == myHoveredRegion;
            if (!chosen[i] && !hovered) continue;
            const Color fill = hovered ? c.regionFill.WithAlpha(c.regionFill.a * 1.5f) : c.regionFill;
            FillSketchRegion(dl, view, (*regions)[i], fill, hovered ? c.sketchHover : c.sketchFree,
                             app.ui.S(hovered ? 2.0f : 0.0f));
        }
    }
    SketchDrawStyle style;
    style.passive = true;
    DrawSketch(app.ui, dl, *sk, view, session.doc.units, style);

    // The operation may want the click first (an axis line wins over the region behind it).
    if (PickInViewport(app, session, *f, view)) {
        myHoveredRegion = -1;
        return;
    }

    // Click: pick or drop a region (switches the profile to manual choice).
    if (regions && vf.hovered && myHoveredRegion >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        std::vector<Vec2> picks;
        for (std::size_t i = 0; i < regions->size(); ++i)
            if (chosen[i] != (static_cast<int>(i) == myHoveredRegion)) picks.push_back((*regions)[i].sample);
        f->profiles = {false, std::move(picks)};
        Changed(session);
    }
}

// --- panel --------------------------------------------------------------------

void SketchOperationEditor::DrawProfileSection(AppContext& app, Session& session, doc::SketchSolidFeature& f, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    Section(app, Tr("Профиль"));
    const auto* sk = session.doc.FindAs<doc::SketchFeature>(f.sketch);
    const auto* regions = session.model.Regions(f.sketch);
    int count = 0;
    if (regions) {
        if (f.profiles.automatic) {
            for (const auto& r : *regions) count += r.automatic ? 1 : 0;
        } else {
            count = static_cast<int>(f.profiles.regions.size());
        }
    }
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ui.S(34);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, {p.x + width, p.y + h}, ui::ToU32(c.accentSoft), ui.S(8));
    dl->AddRect(p, {p.x + width, p.y + h}, ui::ToU32(c.accentSoftBorder), ui.S(8));
    ui.icons.Draw(dl, count > 0 ? "ui-check" : "status-warning", {p.x + ui.S(10), p.y + (h - ui.S(14)) / 2}, ui.S(14), c.accent);
    const std::string chip = (sk ? sk->name : std::string("?")) + " \xC2\xB7 " + (f.profiles.automatic ? Tr("авто") : Tr("вручную")) +
                             ": " + std::to_string(count);
    dl->AddText(ui.fonts.medium, ui.S(13), {p.x + ui.S(32), p.y + (h - ui.S(13)) / 2 - ui.S(1)}, ui::ToU32(c.text), chip.c_str());
    ImGui::Dummy({width, h});
    Caption(app,
            f.profiles.automatic ? Tr("Все замкнутые контуры, вложенные стали отверстиями. Щёлкните по контуру, чтобы выбрать вручную.")
                                 : Tr("Щёлкните по контуру на модели, чтобы добавить или убрать его."),
            width);
    if (!f.profiles.automatic && ui::LinkButton(ui, "##auto-profile", Tr("Выбрать все контуры"))) {
        f.profiles = {true, {}};
        Changed(session);
    }
}

void SketchOperationEditor::DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) {
    auto& f = static_cast<doc::SketchSolidFeature&>(feature);
    // Operation: a new body, or combined with the body before it.
    Section(app, Tr("Операция"));
    const bool bodyBefore = HasSolidBefore(session.doc, static_cast<std::size_t>(session.doc.IndexOf(f.id)));
    static constexpr std::array<const char*, 4> labels{N_("Новое тело"), N_("Добавить"), N_("Вырезать"), N_("Пересечь")};
    const int current = static_cast<int>(f.operation);
    const int chosen = ui::Segmented(app.ui, "op", labels, current, 2, width, bodyBefore ? 0 : 0b1110);
    if (chosen != current) {
        f.operation = static_cast<BooleanOp>(chosen);
        Changed(session);
    }
    Gap(app);
    DrawProfileSection(app, session, f, width);
    Gap(app);
    DrawOptions(app, session, f, width);
}

} // namespace bz::app
