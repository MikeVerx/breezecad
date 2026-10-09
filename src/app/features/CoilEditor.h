#pragma once
// The "Пружина" operation panel: the axis it winds around, the operation
// (new body, join — a ridge, cut — a groove), the hand and the sizes.

#include "app/features/FeatureOperationEditor.h"
#include "document/CoilFeature.h"

namespace bz::app {

class CoilEditor final : public FeatureOperationEditor {
public:
    void        StartNew(AppContext& app, Session& session);
    bool        CanEdit(const doc::Feature& f) const override;
    const char* Title() const override { return "Пружина"; }
    std::string Hint() const override;
    void        DrawInViewport(AppContext&, Session&, const ViewportFrame&) override {}

protected:
    const char* Icon() const override { return "tree-coil"; }
    void LoadTexts(const doc::Feature& feature) override;
    void DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) override;

private:
    std::string myRadius, myPitch, myTurns, myWire, myStart;
};

} // namespace bz::app
