#pragma once
// Panels of the operations that repeat other operations: "Массив"
// (rectangular or circular) and "Зеркало". Both start with the list of
// operations to copy; the preview shows the copies at once.

#include "app/features/FeatureOperationEditor.h"
#include "document/PatternFeature.h"

namespace bz::app {

class CopyEditor : public FeatureOperationEditor {
public:
    /// Nothing to pick by default: the live preview shows the copies.
    void DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) override;

protected:
    /// The operations to copy: every one before this feature that can be
    /// copied, ticked or not. Returns true when the choice changed.
    bool DrawSources(AppContext& app, Session& session, doc::CopyFeature& f, float width);
    /// A new copy feature's default source: the selected operation if it can
    /// be copied, else the last one that can.
    static void ChooseDefaultSource(const Session& session, doc::CopyFeature& f);
};

class PatternEditor final : public CopyEditor {
public:
    void        StartNew(AppContext& app, Session& session, doc::PatternKind kind);
    bool        CanEdit(const doc::Feature& f) const override;
    const char* Title() const override { return "Массив"; }
    std::string Hint() const override;

protected:
    const char* Icon() const override;
    void LoadTexts(const doc::Feature& feature) override;
    void DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) override;

private:
    void DrawRectangular(AppContext& app, Session& session, doc::PatternFeature& f, float width);
    void DrawCircular(AppContext& app, Session& session, doc::PatternFeature& f, float width);

    doc::PatternKind myKind = doc::PatternKind::Rectangular;   ///< for the header icon
    std::string myCount1, mySpacing1, myCount2, mySpacing2, myCircularCount, myAngle;
};

class MirrorEditor final : public CopyEditor {
public:
    void        StartNew(AppContext& app, Session& session);
    bool        CanEdit(const doc::Feature& f) const override;
    const char* Title() const override { return "Зеркало"; }
    std::string Hint() const override;
    bool        DrawsPlanes() const override { return true; }
    void        DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) override;

protected:
    const char* Icon() const override { return "tree-mirror"; }
    void LoadTexts(const doc::Feature& feature) override { (void)feature; }
    void DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) override;
};

} // namespace bz::app
