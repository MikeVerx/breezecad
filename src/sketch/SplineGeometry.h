#pragma once
// The shape of a sketch spline as one B-spline, so the sketch draws exactly
// what the model builds.
//   Through points: cubic pieces meeting at every point with a common
//   tangent (along the neighbours, Catmull-Rom); C1.
//   Control vertices: a clamped uniform B-spline of degree 3 (lower with
//   fewer points) on the points, starting and ending at the end points.

#include "sketch/Sketch.h"

#include <vector>

namespace bz::sketch {

struct BSpline {
    int                 degree = 3;
    std::vector<Vec2>   poles;
    std::vector<double> knots;   ///< distinct values
    std::vector<int>    mults;   ///< multiplicity of each knot
};

/// nullopt-free: an empty spline (no poles) for fewer than two points.
BSpline SplineShape(const Sketch& sk, const Spline& spline);

/// The same from positions (the tool's preview before the spline exists).
BSpline SplineShape(const std::vector<Vec2>& points, bool interpolate, bool closed);

/// Points along the curve, `perSpan` per knot span.
std::vector<Vec2> SampleSpline(const BSpline& b, int perSpan = 16);

} // namespace bz::sketch
