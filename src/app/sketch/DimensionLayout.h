#pragma once
// Where the pieces of a dimension go, in sketch coordinates. Used both for
// drawing and for hit-testing the label.

#include "core/Units.h"
#include "sketch/Sketch.h"

#include <optional>
#include <string>
#include <vector>

namespace bz::app {

struct DimensionLayout {
    struct Segment {
        Vec2 a, b;
    };
    std::vector<Segment> lines;      ///< extension lines and the dimension line
    struct Arrow {
        Vec2 tip, direction;         ///< direction points from the tail to the tip
    };
    std::vector<Arrow> arrows;
    std::vector<Vec2>  arc;          ///< polyline for angle dimensions
    Vec2               label;        ///< text centre
    Vec2               anchor;       ///< what labelOffset is relative to
    std::string        text;         ///< "60", "Ø20", "R10", "30°", "(45)"
    std::string        name;         ///< the parameter's name ("d3"), shown small under the value
};

/// Layout of a dimensional constraint, or nullopt when its geometry is
/// degenerate (e.g. an angle between parallel lines).
std::optional<DimensionLayout> LayoutDimension(const sketch::Sketch& sk, const sketch::Constraint& c,
                                               const UnitSystem& units);

/// The point the label offset of a dimension is measured from.
std::optional<Vec2> DimensionAnchor(const sketch::Sketch& sk, const sketch::Constraint& c);

/// Current measured value of a dimension type between entities (mm / rad),
/// used to prefill a new dimension. nullopt when not applicable.
std::optional<double> MeasureDimension(const sketch::Sketch& sk, const sketch::Constraint& c);

} // namespace bz::app
