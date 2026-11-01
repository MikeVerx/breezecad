#pragma once
// A sheet metal part as the sheet metal module sees it: a flat base face
// (2D, in the plane of its sketch), flanges bent up or down on straight
// edges of its outline, and the rules they bend with. The folded solid is
// built from this by the kernel; the flat pattern is computed here, without
// any geometry kernel (docs/ADR/0007-sheet-metal.md).
//
// Coordinates are in the sketch plane, mm. The base occupies the thickness
// above the plane (or below it when the part is reversed); "up" is the
// sketch normal. A flange can also stand on the far edge of another flange
// (a flange on a flange); then "up" is towards that flange's inner side.
// 3D values of the layout are "part coordinates": the sketch plane is z = 0.

#include "core/Expected.h"
#include "core/Math.h"
#include "sheetmetal/SheetRules.h"

#include <cstddef>
#include <string>
#include <vector>

namespace bz::sheet {

/// A piece of an outline: a straight line from `a` to `b`, or an arc around
/// `center` (counter-clockwise when `ccw`), or a full circle (`full`, a == b).
struct Segment {
    Vec2 a, b;
    bool arc  = false;
    Vec2 center;
    bool ccw  = true;
    bool full = false;

    static Segment Line(Vec2 a, Vec2 b) { return {a, b, false, {}, true, false}; }
    static Segment Arc(Vec2 a, Vec2 b, Vec2 center, bool ccw) { return {a, b, true, center, ccw, false}; }
    static Segment Circle(Vec2 center, double radius) {
        const Vec2 p = center + Vec2{radius, 0};
        return {p, p, true, center, true, true};
    }

    double Radius() const { return (a - center).Length(); }
    /// Signed sweep of an arc, radians (positive counter-clockwise).
    double Sweep() const;
    double Length() const;
    Vec2   Middle() const;
    /// The same piece run the other way.
    Segment Reversed() const;
};

/// A closed outline, segments end to end.
using Loop = std::vector<Segment>;

/// Area inside a loop, positive when it runs counter-clockwise.
double SignedArea(const Loop& loop);
/// The loop run the other way.
Loop Reversed(const Loop& loop);

/// A face: one outer loop (counter-clockwise) and holes (clockwise).
struct Region {
    Loop              outer;
    std::vector<Loop> holes;

    /// Puts the loops in the orientation above.
    void Orient();
    double Area() const;
    Box2   Bounds() const;
};

/// Which edge of the flange under it a flange on a flange stands on.
enum class FlangeEdge : unsigned char {
    Far,     ///< the edge away from the bend: the new flange goes on (a hem, a C profile)
    Start,   ///< the side edge at the start of the flange under it
    End,     ///< the side edge at its end
};

/// A flange as the user asked for it.
struct Flange {
    /// Which edge of the base: the straight edge of its outline nearest to
    /// this point (sketch coordinates). Solve() moves it to the middle of the
    /// edge found, so it follows the edge when sizes change. Not used when
    /// the flange stands on another flange (`parent`).
    Vec2   edgePoint;
    double height = 20.0;   ///< outer height from the virtual sharp, mm
    double angle  = 1.5707963267948966;   ///< bend angle, rad (pi/2: right angle)
    bool   down   = false;  ///< bent below the sketch plane instead of above
    /// >= 0: stands on an edge (`onEdge`) of flange number `parent`
    /// (earlier in Part::flanges), over that edge's whole length.
    int        parent = -1;
    FlangeEdge onEdge = FlangeEdge::Far;
    /// On a plate edge: how much of the edge it takes. Both offsets 0: the
    /// whole edge; otherwise it starts `offsetStart` from the edge's start
    /// and ends `offsetEnd` before its end (the outline runs counter-clockwise).
    /// `centered`: `width` wide in the middle of the edge instead.
    double offsetStart = 0.0, offsetEnd = 0.0;
    bool   centered = false;
    double width = 0.0;
};

/// A cut through the sheet: a closed outline on one flat face. The face's
/// coordinates: the base — the sketch plane of the part; a flange's strip —
/// x along its edge from where it starts, y out of the bend.
struct Cut {
    int  face = -1;   ///< -1: the base; else the flange number whose strip it is in
    Loop loop;
};

struct Part {
    Rules               rules;
    Region              base;     ///< as sketched
    bool                reversed = false;   ///< base below the sketch plane
    std::vector<Flange> flanges;
    std::vector<Cut>    cuts;
};

/// A flange placed on the part.
struct BentFlange {
    int    parent = -1;     ///< the flange it stands on; -1: on the base
    FlangeEdge onEdge = FlangeEdge::Far;
    int    child = -1;      ///< the flange standing on its far edge; -1: none
    int    sideStart = -1, sideEnd = -1;   ///< flanges on its side edges; -1: none
    std::size_t edge = 0;   ///< on the base: index into Part::base.outer
    std::size_t bendSegment = 0;   ///< on the base: index into Layout::base.outer of where the bend starts
    Vec2   a, b;            ///< on the base: where the bend starts (the trimmed edge), counter-clockwise
    Vec2   out;             ///< on the base: outward normal of the edge, in the plane
    double u1 = 0.0, u2 = 0.0;   ///< on the base: the part of the edge it takes, from the edge's start, mm
    double angle = 0.0;     ///< rad
    double setback = 0.0;   ///< how far the base edge moved in, mm
    double straight = 0.0;  ///< flat length of the flange after the bend, mm
    double allowance = 0.0; ///< neutral length of the bend, mm
    bool   down = false;
    Vec2   edgeMiddle;      ///< on the base: middle of the edge as sketched (the reference, updated)

    // Where the bend starts, in part coordinates (any flange): a corner of
    // the face it bends from, on that face's bottom side (the side away from
    // its normal); the edge runs along `along` for `width`; `outward` points
    // away from the face, in its plane; `normal` is the face's normal.
    Vec3   start, along, outward, normal;
    double width = 0.0;
    // The flat strip after the bend (same meaning, for the strip itself):
    // a flange on this one starts at stripStart + stripOut * straight.
    Vec3   stripStart, stripOut, stripNormal;
    /// The bend axis: a point on it and its direction (a positive turn by
    /// `angle` around it takes the face's end to the strip's).
    Vec3   axisPoint, axisDir;
};

/// The part with every flange placed.
struct Layout {
    Region                  base;   ///< trimmed where flanges start, with reliefs; cuts in the base are its holes
    std::vector<BentFlange> flanges;
    std::vector<Cut>        stripCuts;   ///< cuts in flanges' strips (face >= 0)
};

/// Relief cut at the end of a flange narrower than its edge: `width` wide,
/// `depth` past the bend zone (both from the thickness, not checked against a
/// standard: Inventor-like defaults).
struct Relief {
    double width = 0.0, depth = 0.0;
    static Relief For(const Rules& r) { return {r.thickness, r.thickness / 2.0}; }
};

/// Places the flanges and the cuts. Errors say, in Russian, which flange or
/// cut cannot be made and why. `failed` (when given) gets the index of the
/// flange at fault, or flanges.size() + the index of the cut.
Expected<Layout> Solve(const Part& part, std::size_t* failed = nullptr);

/// Where a flange's strip lies in the flat blank: point = origin + x * u + y * v
/// for a point (u, v) of the strip (Cut coordinates).
struct FlatFrame {
    Vec2 origin, x, y;
    Vec2 Map(Vec2 p) const { return origin + x * p.x + y * p.y; }
};

/// The flat blank.
struct FlatPattern {
    Region outline;
    struct Bend {
        Vec2        a, b;   ///< the bend's centre line
        double      angle = 0.0;
        bool        down  = false;
        double      radius = 0.0;
    };
    std::vector<Bend> bends;
    std::vector<FlatFrame> strips;   ///< for each flange of the layout
    double thickness = 0.0;

    Box2   Bounds() const { return outline.Bounds(); }
    double Area() const { return outline.Area(); }
};

FlatPattern Flatten(const Part& part, const Layout& layout);

/// "ВВЕРХ 90° R2", the note on a bend line.
std::string BendNote(const FlatPattern::Bend& bend);

} // namespace bz::sheet
