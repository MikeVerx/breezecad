#pragma once
// Compound shapes the drawing tools make: rectangles three ways, circles and
// arcs through three points, regular polygons. Each adds its curves and the
// constraints that keep the shape what it is (sides stay square, a polygon
// stays regular), and nothing that fixes its size: dimensions are the
// caller's business.

#include "sketch/Sketch.h"

#include <array>
#include <optional>
#include <vector>

namespace bz::sketch {

struct RectangleIds {
    std::array<ObjectId, 4> corners;   ///< counter-clockwise or clockwise, as drawn
    std::array<ObjectId, 4> sides;     ///< sides[i] runs corners[i] → corners[i + 1]
};

/// Axis-aligned, from two existing opposite corner points.
RectangleIds AddCornerRectangle(Sketch& sk, IdGenerator& ids, ObjectId corner, ObjectId opposite);

/// Axis-aligned around an existing centre point, one corner at `corner`. A
/// construction diagonal keeps the centre in the middle.
RectangleIds AddCentredRectangle(Sketch& sk, IdGenerator& ids, ObjectId centre, Vec2 corner);

/// At any angle: side `a`–`b` (existing points), the opposite side through
/// the projection of `third`. Nullopt when the three points are in line.
std::optional<RectangleIds> AddThreePointRectangle(Sketch& sk, IdGenerator& ids, ObjectId a, ObjectId b, Vec2 third);

struct CircleThrough {
    Vec2   centre;
    double radius = 0;
};

/// The circle through three points; nullopt when they are (nearly) in line.
std::optional<CircleThrough> CircleThroughPoints(Vec2 a, Vec2 b, Vec2 c);

/// Regular polygon around an existing centre point. `towards` is a vertex
/// (inscribed) or the middle of a side (circumscribed). A construction
/// circle carries the size; the sides are equal and touch it at the
/// vertices (inscribed) or in their middles (circumscribed).
struct PolygonIds {
    ObjectId              circle;
    std::vector<ObjectId> corners;
    std::vector<ObjectId> sides;
};
PolygonIds AddPolygon(Sketch& sk, IdGenerator& ids, ObjectId centre, Vec2 towards, int sideCount, bool inscribed);

} // namespace bz::sketch
