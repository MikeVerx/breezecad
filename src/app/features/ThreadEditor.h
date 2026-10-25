#pragma once
// The "Резьба" operation panel: a metric, pipe (G) or trapezoidal (Tr)
// thread on a shaft or in a hole, with a fit and a lead-in chamfer
// (doc::ThreadFeature). The face is clicked on the body; the pitch
// follows the detected size until it is typed.

#include "app/features/FeatureOperationEditor.h"
#include "core/I18n.h"
#include "document/ThreadFeature.h"

namespace bz {
struct UnitSystem;
}

namespace bz::app {

/// "M8×1.25", "G1/2", "Tr16×4": how a thread is named (panels, the tree).
std::string ThreadDesignation(doc::ThreadStandard standard, double nominal, double pitch, const UnitSystem& units);

class ThreadEditor final : public FeatureOperationEditor {
public:
    void        StartNew(AppContext& app, Session& session);
    bool        CanEdit(const doc::Feature& f) const override;
    const char* Title() const override { return Tr("Резьба"); }
    std::string Hint() const override;
    bool        PicksOriginal() const override { return true; }

    void DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) override;

protected:
    const char* Icon() const override { return "tree-thread"; }
    void LoadTexts(const doc::Feature& feature) override;
    void DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) override;

private:
    std::string myPitch, myLength, myClearance;
    bool        myAutoPitch = false;   ///< a face was just picked: take the standard pitch
};

} // namespace bz::app
