#pragma once
// Drawing sketches over the viewport: grid, curves, points, dimensions,
// constraint glyphs, snap feedback and profile regions.

#include "app/sketch/DimensionLayout.h"
#include "app/sketch/SketchPick.h"
#include "app/sketch/SketchView.h"
#include "document/SketchFeature.h"
#include "kernel/Model.h"
#include "ui/Ui.h"

#include <unordered_set>
#include <vector>

namespace bz::app {

struct SketchDrawStyle {
    const std::unordered_set<ObjectId>*        selection = nullptr;
    ObjectId                                   hover;
    std::vector<ObjectId>                      picked;
    const doc::SketchFeature::Analysis*        analysis = nullptr;
    bool                                       passive = false;   ///< not being edited: thin, muted, no dimensions
    /// Geometric constraints whose glyphs are drawn; nullptr draws all.
    const std::unordered_set<ObjectId>*        glyphs = nullptr;
};

void DrawSketchGrid(ui::Ui& ui, ImDrawList* dl, const SketchView& view, double step);
void DrawSketch(ui::Ui& ui, ImDrawList* dl, const doc::SketchFeature& feature, const SketchView& view,
                const UnitSystem& units, const SketchDrawStyle& style);
void DrawDimension(ui::Ui& ui, ImDrawList* dl, const SketchView& view, const DimensionLayout& layout, Color color,
                   bool highlight);
void DrawSnapMarker(ui::Ui& ui, ImDrawList* dl, ImVec2 at, const Snap& snap);
/// Dashed lines to the points the snapped position is lined up with.
void DrawSnapGuides(ui::Ui& ui, ImDrawList* dl, const SketchView& view, const Snap& snap);

enum class LinePattern : unsigned char { Solid, Dashed, DashDot };

void DrawSketchSegment(ui::Ui& ui, ImDrawList* dl, const SketchView& view, Vec2 a, Vec2 b, Color color, float width,
                       LinePattern pattern);
void DrawSketchSegment(ui::Ui& ui, ImDrawList* dl, const SketchView& view, Vec2 a, Vec2 b, Color color, float width,
                       bool dashed = false);
void DrawSketchArc(ui::Ui& ui, ImDrawList* dl, const SketchView& view, Vec2 center, double radius, double a0, double a1,
                   Color color, float width, bool dashed = false);

/// Fills a closed region (outer loop with holes) of a sketch.
void FillSketchRegion(ImDrawList* dl, const SketchView& view, const kernel::SketchRegion& region, Color fill, Color outline,
                      float outlineWidth);

/// A constraint glyph next to its geometry.
struct GlyphPlacement {
    ObjectId    constraint;
    ImVec2      center;
    const char* icon;
};
/// `only`: the constraints to place (nullptr: all of them).
std::vector<GlyphPlacement> LayoutGlyphs(const sketch::Sketch& sk, const SketchView& view, float scale,
                                         const std::unordered_set<ObjectId>* only = nullptr);

} // namespace bz::app
