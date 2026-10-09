#pragma once
// Operations on edges of the existing body (Сопряжение, Фаска): edges are
// clicked in the viewport (again to drop one), the size is typed in the panel.
// While the panel is open the viewport shows the result live and draws the
// edges of the body as it was before the operation over it; those are the
// ones picked, so an edge already rounded off can be clicked again to drop it.

#include "app/features/FeatureOperationEditor.h"
#include "document/EdgeFeature.h"

namespace bz::app {

class EdgeOperationEditor : public FeatureOperationEditor {
public:
    void StartNew(AppContext& app, Session& session);
    void DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) override;
    bool PicksOriginal() const override { return true; }
    std::string Hint() const override;

protected:
    /// A new feature of this kind with defaults.
    virtual std::unique_ptr<doc::EdgeFeature> Create(Session& session) const = 0;
    /// The size field (radius, distance).
    virtual void DrawSize(AppContext& app, Session& session, doc::EdgeFeature& feature, float width) = 0;

private:
    void DrawBody(AppContext& app, Session& session, doc::Feature& feature, float width) final;
    void StartNext(AppContext& app, Session& session, const doc::Feature& previous) final;
    bool ShowStatus(const doc::Feature& feature) const final;
};

class FilletEditor final : public EdgeOperationEditor {
public:
    bool        CanEdit(const doc::Feature& f) const override;
    const char* Title() const override { return "Сопряжение"; }

protected:
    const char* Icon() const override { return "tree-fillet"; }
    std::unique_ptr<doc::EdgeFeature> Create(Session& session) const override;
    void LoadTexts(const doc::Feature& feature) override;
    void DrawSize(AppContext& app, Session& session, doc::EdgeFeature& feature, float width) override;

private:
    std::string myRadius;
};

class ChamferEditor final : public EdgeOperationEditor {
public:
    bool        CanEdit(const doc::Feature& f) const override;
    const char* Title() const override { return "Фаска"; }

protected:
    const char* Icon() const override { return "tree-chamfer"; }
    std::unique_ptr<doc::EdgeFeature> Create(Session& session) const override;
    void LoadTexts(const doc::Feature& feature) override;
    void DrawSize(AppContext& app, Session& session, doc::EdgeFeature& feature, float width) override;

private:
    std::string myDistance;
};

} // namespace bz::app
