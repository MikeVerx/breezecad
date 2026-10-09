#pragma once
// Compound shapes the drawing tools make: rectangles three ways, circles and
// arcs through three points, regular polygons. Each adds its curves and the
// constraints that keep the shape what it is (sides stay square, a polygon
// stays regular), and nothing that fixes its size: dimensions are the
// caller's business.

#include "sketch/Sketch.h"

#include <array>
#include <optional>
#include <utility>
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

/// At any angle around an existing centre point: `side` is the middle of a
/// side (direction and half length), `third` sets the other half size.
/// Nullopt when the points are in line.
std::optional<RectangleIds> AddThreePointCentredRectangle(Sketch& sk, IdGenerator& ids, ObjectId centre, Vec2 side, Vec2 third);

/// A slot: two ends rounded by half circles, the sides tangent to them.
struct SlotIds {
    std::vector<ObjectId> curves;   ///< sides and end caps
    ObjectId              axis;     ///< construction centre line or arc
    ObjectId              cap;      ///< one end cap: its diameter is the slot width
};

/// Straight slot between two existing centre points, `width` across.
SlotIds AddStraightSlot(Sketch& sk, IdGenerator& ids, ObjectId centre1, ObjectId centre2, double width);

/// Slot along the counter-clockwise arc around `centre` from point `start`
/// to point `end` (existing points; `end` is moved onto the radius of `start`).
SlotIds AddArcSlot(Sketch& sk, IdGenerator& ids, ObjectId centre, ObjectId start, ObjectId end, double width);

/// Outlines for previews (closed, in sketch coordinates).
std::vector<Vec2> StraightSlotOutline(Vec2 centre1, Vec2 centre2, double width);
std::vector<Vec2> ArcSlotOutline(Vec2 centre, Vec2 start, Vec2 end, double width);

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

/// The circle tangent to three lines (each given by two points). Of the
/// circles that touch all three (up to four), the one whose touching points
/// are nearest the `picks` (where the lines were clicked). nullopt when the
/// lines allow none (parallel ones, all through one point).
struct TangentCircle {
    Vec2   centre;
    double radius = 0;
};
std::optional<TangentCircle> CircleTangentToLines(const std::array<std::pair<Vec2, Vec2>, 3>& lines,
                                                  const std::array<Vec2, 3>& picks);

} // namespace bz::sketch
