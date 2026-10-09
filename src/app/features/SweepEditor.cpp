#include "app/features/SweepEditor.h"

#include "app/sketch/SketchOverlay.h"
#include "document/SketchFeature.h"
#include "ui/Widgets.h"

#include <algorithm>

namespace bz::app {

bool SweepEditor::CanEdit(const doc::Feature& f) const { return dynamic_cast<const doc::SweepFeature*>(&f) != nullptr; }

std::string SweepEditor::Hint() const {
    return "Выберите в панели эскиз траектории; щелчок по контуру профиля выбирает его. Enter — готово, Esc — отмена";
}

namespace {

bool HasRegions(const Session& session, ObjectId sketch) {
    const auto* regions = session.model.Regions(sketch);
    return regions && !regions->empty();
}

} // namespace

ObjectId SweepEditor::ChooseSketch(const Session& session, ObjectId proposed) const {
    // The last sketch drawn is often the path (an open chain): the profile
    // is then the last sketch that has a closed region.
    if (HasRegions(session, proposed)) return proposed;
    for (std::size_t i = session.doc.EndMarker(); i-- > 0;) {
        const auto* sk = dynamic_cast<const doc::SketchFeature*>(session.doc.Features()[i].get());
        if (sk && HasRegions(session, sk->id)) return sk->id;
    }
    return proposed;
}

std::unique_ptr<doc::SketchSolidFeature> SweepEditor::Create(Session& session, ObjectId sketch) const {
    auto f = std::make_unique<doc::SweepFeature>();
    // The path: the last other sketch without closed regions (a path is
    // usually an open chain), else the last other sketch.
    ObjectId open, any;
    for (std::size_t i = 0; i < session.doc.EndMarker(); ++i) {
        const auto* sk = dynamic_cast<const doc::SketchFeature*>(session.doc.Features()[i].get());
        if (!sk || sk->id == sketch) continue;
        any = sk->id;
        if (!HasRegions(session, sk->id)) open = sk->id;
    }
    f->path = open ? open : any;
    return f;
}

void SweepEditor::DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) {
    // The path first, so the profile and its regions stay on top.
    if (const auto* f = dynamic_cast<doc::SweepFeature*>(Feature(session)))
        if (const auto* path = session.doc.FindAs<doc::SketchFeature>(f->path)) {
            SketchDrawStyle style;
            style.passive = true;
            DrawSketch(app.ui, ImGui::GetWindowDrawList(), *path, {path->placement.frame, vf}, session.doc.units, style);
        }
    SketchOperationEditor::DrawInViewport(app, session, vf);
}

void SweepEditor::DrawOptions(AppContext& app, Session& session, doc::SketchSolidFeature& feature, float width) {
    auto& f = static_cast<doc::SweepFeature&>(feature);
    Section(app, "Траектория");
    // One sketch: a ticked list where a new tick replaces the old one.
    std::vector<ObjectId> picked;
    if (f.path) picked.push_back(f.path);
    const auto isPath = [&f](const doc::Feature& candidate) {
        return dynamic_cast<const doc::SketchFeature*>(&candidate) && candidate.id != f.sketch;
    };
    if (FeatureChecklist(app, session, picked, isPath, width, "Нарисуйте траекторию в другом эскизе.")) {
        const auto added = std::ranges::find_if(picked, [&](ObjectId id) { return id != f.path; });
        f.path = added != picked.end() ? *added : ObjectId{};
    }
}

} // namespace bz::app
