#pragma once
// A 2D drawing as DXF holds it: layers and lines, arcs, circles, polylines
// and texts. What the flat pattern is saved as and what the DXF editor
// («Редактор DXF», preparing a blank for cutting) edits. No geometry kernel.
//
// Reading takes ASCII DXF of any version (R12 … 2018): LINE, ARC, CIRCLE,
// LWPOLYLINE, POLYLINE, TEXT, MTEXT; SPLINE and ELLIPSE become polylines,
// block references (INSERT) are exploded. Writing makes DXF R12 (AC1009) laid
// out as AutoCAD writes it — complete tables, handles, model and paper space
// blocks — with non-ASCII text as \U+XXXX (docs/ADR/0007-sheet-metal.md).

#include "core/Expected.h"
#include "core/Math.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace bz::dxf {

/// Layers of a blank for the shop. ASCII names: CAM programs of laser and
/// punch machines do not all read other letters in layer names.
inline constexpr const char* kCut  = "CUT";    ///< «Рез»: outline and holes
inline constexpr const char* kBend = "BEND";   ///< «Гиб»: bend lines and their notes
inline constexpr const char* kMark = "MARK";   ///< «Маркировка»: engraved text, marks

enum class Kind : unsigned char { Line, Arc, Circle, Polyline, Text };

struct Entity {
    Kind        kind = Kind::Line;
    std::string layer = "0";
    int         color = 256;   ///< ACI; 256: by layer

    Vec2   a, b;                ///< Line
    Vec2   center;              ///< Arc, Circle
    double radius = 0.0;
    double startDeg = 0.0, endDeg = 0.0;   ///< Arc, counter-clockwise from start to end
    std::vector<Vec2>   points;            ///< Polyline vertices
    std::vector<double> bulges;            ///< Polyline: bulge after each vertex (tan of a quarter of the arc's angle)
    bool   closed = false;                 ///< Polyline
    Vec2   at;                  ///< Text: insertion point (left of the baseline)
    double height = 2.5, rotationDeg = 0.0;
    std::string text;           ///< UTF-8

    static Entity Line(Vec2 a, Vec2 b, std::string layer);
    static Entity Arc(Vec2 center, double radius, double startDeg, double endDeg, std::string layer);
    static Entity Circle(Vec2 center, double radius, std::string layer);
    static Entity Text(Vec2 at, double height, double rotationDeg, std::string text, std::string layer);
};

struct Layer {
    std::string name;
    int         color = 7;      ///< ACI
    bool        visible = true;
};

struct Drawing {
    std::vector<Layer>  layers;
    std::vector<Entity> entities;

    const Layer* FindLayer(std::string_view name) const;
    Layer*       FindLayer(std::string_view name);
    /// The layer, made when missing.
    Layer& EnsureLayer(std::string_view name, int color = 7);
};

/// What reading skipped, for the user ("пропущено: 3 HATCH").
struct ReadReport {
    std::vector<std::pair<std::string, int>> skipped;   ///< entity type, count
    double unitsToMm = 1.0;                             ///< from $INSUNITS
    std::string version;                                ///< $ACADVER
};

Expected<Drawing> Read(std::string_view text, ReadReport* report = nullptr);
/// DXF R12 that AutoCAD, Inventor, CAM programs and ezdxf open.
std::string WriteR12(const Drawing& drawing);

// --- geometry --------------------------------------------------------------------

/// Points along the entity: exact for lines, short chords for curves
/// (`maxAngleDeg` per chord). Texts give their insertion point.
std::vector<Vec2> Outline(const Entity& e, double maxAngleDeg = 5.0);
/// Where an open entity starts and ends (none for circles, closed polylines, texts).
bool Ends(const Entity& e, Vec2& start, Vec2& end);
Box2   Bounds(const Entity& e);
Box2   Bounds(const Drawing& d, bool visibleOnly = false);
/// Distance from `p` to the entity, mm (texts: to their box).
double Distance(const Entity& e, Vec2 p);
/// Points worth snapping to: ends, middles, centres.
std::vector<Vec2> SnapPoints(const Entity& e);

void Move(Entity& e, Vec2 by);
void Rotate(Entity& e, Vec2 around, double angleDeg);
/// Mirror across the vertical line x = `x` (a blank turned over).
void MirrorX(Entity& e, double x);

// --- checks before cutting ------------------------------------------------------

struct Check {
    std::vector<Vec2>        openEnds;     ///< ends of curves that meet no other end
    std::vector<std::size_t> duplicates;   ///< entities lying exactly over an earlier one
    int closedContours = 0;                ///< closed loops (circles, closed polylines, chains)
};
/// Looks at the entities on `layer` (all when empty). `tolerance`: ends this
/// close count as joined, mm.
Check CheckForCutting(const Drawing& d, std::string_view layer, double tolerance = 0.01);
/// Joins open ends on `layer` (all when empty) that are at most `maxGap`
/// apart: the ends of lines and polylines move to meet (to the middle, or
/// onto an arc's end — arcs keep their shape). Returns how many gaps it closed.
int CloseGaps(Drawing& d, std::string_view layer, double maxGap, double tolerance = 0.01);

// --- text ------------------------------------------------------------------------

/// "\U+041B" escapes for everything outside ASCII; "°" as AutoCAD's %%d.
std::string EscapeText(std::string_view utf8);
/// The other way: \U+XXXX and %%d/%%c/%%p back to UTF-8.
std::string UnescapeText(std::string_view text);

} // namespace bz::dxf
