#pragma once
// The "Резьба" operation panel: an ISO metric thread on a shaft or in a
// hole (doc::ThreadFeature). The face is clicked on the body; the pitch
// follows the detected size until it is typed.

#include "app/features/FeatureOperationEditor.h"
#include "core/I18n.h"
#include "document/ThreadFeature.h"

namespace bz::app {

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
    std::string myPitch, myLength;
    bool        myAutoPitch = false;   ///< a face was just picked: take the standard pitch
};

} // namespace bz::app
