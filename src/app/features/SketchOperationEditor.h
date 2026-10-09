#pragma once
// Operations that turn sketch regions into a solid (extrude, revolve): the
// sketch is chosen when the operation starts, regions are picked in the
// viewport, and the result is a new body or combines with the last one.
// A derived editor only describes its own options.

#include "app/features/FeatureOperationEditor.h"
#include "app/sketch/SketchView.h"
#include "document/SketchSolidFeature.h"

namespace bz::app {

class SketchOperationEditor : public FeatureOperationEditor {
public:
    /// New operation on `sketch`, or on the selected / last unused sketch.
    void StartNew(AppContext& app, Session& session, ObjectId sketch = {});

    void DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) override;

protected:
    /// The sketch a new operation starts on, given the one StartNew proposes
    /// (the selected or last unused sketch). The default keeps it.
    virtual ObjectId ChooseSketch(const Session& session, ObjectId proposed) const;
    /// A new feature of this kind with defaults (sketch, name and id are set by the caller).
    virtual std::unique_ptr<doc::SketchSolidFeature> Create(Session& session, ObjectId sketch) const = 0;
    /// The operation's own options, below "Операция" and "Профиль".
    virtual void DrawOptions(AppContext& app, Session& session, doc::SketchSolidFeature& feature, float width) = 0;
    /// After Применить: settings worth keeping for the next operation.
    virtual void CarryOver(const doc::SketchSolidFeature& previous, doc::SketchSolidFeature& next) const;
    /// Viewport picking before regions (revolve picks its axis). Returns
    /// true when it consumed the click.
    virtual bool PickInViewport(AppContext& app, Session& session, doc::SketchSolidFeature& feature, const SketchView& view);

    doc::SketchSolidFeature* Feature(Session& session) const;

private:
    void DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) final;
    void StartNext(AppContext& app, Session& session, const doc::Feature& previous) final;
    void DrawProfileSection(AppContext& app, Session& session, doc::SketchSolidFeature& f, float width);

    int myHoveredRegion = -1;
};

} // namespace bz::app
