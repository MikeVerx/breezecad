#pragma once
// Packs of standard parts ("пакеты стандартных изделий"): the library's
// tables and how to build a part from a row, as data in a JSON file
// (*.bzlib) instead of code. The library window, library.* commands and
// assemblies show a pack's parts next to the built-in ones
// (StandardParts.h); docs/ADR/0006-standard-part-packs.md describes the
// format.
//
// A pack declares kinds of parts (a folder and a variety of the library
// window, with a recipe: sketches of simple shapes and extrude / revolve)
// and standards (a table of sizes for a kind). Building a part puts the
// table's values into document variables and the recipe's expressions into
// dimensions, so the part is an ordinary parametric tree, the same as the
// built-in ones.

#include "core/Expected.h"

#include <array>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bz::doc::parts {

/// The format this build reads ("format" in the file) and its version.
inline constexpr std::string_view kPackFormat = "breezecad-standard-parts";
inline constexpr int              kPackVersion = 1;
/// File extension of packs.
inline constexpr std::string_view kPackExtension = ".bzlib";

/// A curve or curves of a recipe's sketch. Coordinates and sizes are
/// expressions over the standard's columns and L ("b/2 - R").
struct PackShape {
    enum class Type : unsigned char {
        Rect,      ///< w × h centred on (x, y), corners rounded by r (none when r is empty or 0)
        Circle,    ///< diameter d centred on (x, y)
        Polygon,   ///< closed polyline through points
        Hexagon,   ///< regular, s across flats, centred on the origin, corners on X
        Axis,      ///< осевая from points[0] to points[1]: what the next revolve turns about
    };
    Type        type = Type::Rect;
    std::string w, h, r, x, y, d, s;
    std::vector<std::array<std::string, 2>> points;   ///< Polygon; Axis: its two ends
    /// Polygon: a radius for each corner (empty or "0": sharp), rounding it
    /// tangent to both sides (the root and toe radii of an angle or a channel).
    std::vector<std::string> radii;
    /// Polygon: its closing line (last point → first) is the осевая to revolve about.
    bool axis = false;
};

/// One operation of a recipe.
struct PackStep {
    enum class Type : unsigned char { Sketch, Extrude, Revolve };
    enum class Operation : unsigned char { New, Join, Cut };
    enum class Direction : unsigned char { Normal, Reversed, Symmetric };
    Type                   type = Type::Sketch;
    std::string            plane = "XY";   ///< Sketch: XY, XZ or YZ
    std::vector<PackShape> shapes;         ///< Sketch
    std::string            distance;       ///< Extrude
    Operation              operation = Operation::New;   ///< Extrude, Revolve: on the last sketch
    Direction              direction = Direction::Normal;   ///< Extrude
};

/// A kind of part: where it sits in the library window and how it is built.
struct PackKind {
    std::string id;          ///< "profile.square", unique among the loaded packs
    std::string group;       ///< folder: "Профили" (may be a built-in one: "Болты")
    std::string variety;     ///< row in the folder: "Квадратные"
    std::string title;       ///< "Профиль гнутый замкнутый квадратный"
    std::string noun;        ///< "Профиль"
    std::string sizeTitle;   ///< caption of the sizes: "Сечение a×t, мм"
    std::string appearance = "std.steel";
    std::string material = "Сталь";
    std::vector<PackStep> build;
    /// Outer size across X, Y and along Z (expressions; empty: not checked).
    std::array<std::string, 3> envelope;
    /// Instead of a recipe: a part document (its JSON, as in a .bzc) whose
    /// variables named like the table's columns take a row's values — a part
    /// the user drew saved into the library ("Сохранить в библиотеку").
    std::string document;
};

/// A column of a standard's table.
struct PackColumn {
    std::string name;     ///< variable name in the part and in expressions: "t"
    std::string title;    ///< "Толщина стенки"
    bool        length = true;   ///< mm, becomes a variable; false: kept for reference (mass of 1 m…)
};

/// A size the user types within a range, besides the table's row and the
/// length (the width of a sheet).
struct PackInput {
    std::string name;    ///< variable: "B"
    std::string title;   ///< "Ширина, мм"
    double      min = 0, max = 0, preset = 0;
};

/// Lengths a part of the standard can have.
struct PackLength {
    enum class Mode : unsigned char {
        None,   ///< no length (a nut)
        List,   ///< one of `list`
        Free,   ///< any from min to max (a profile cut to size)
    };
    Mode                mode = Mode::None;
    std::vector<double> list;
    double              min = 0, max = 0, preset = 0;   ///< Free; preset: the length offered first
};

/// A standard: a table of sizes for a kind.
struct PackStandard {
    std::string id;       ///< stable key, "gost30245.square"
    std::string title;    ///< "ГОСТ 30245-2003"
    std::string kind;     ///< PackKind::id
    std::string note;
    std::string family;   ///< "ГОСТ", "ISO" or "DIN"; empty: by the title
    /// How a size is written, from the columns: "{h}×{b}×{t}".
    std::string size;
    /// Designation; {size}, {L}, {title}, {noun} and columns are replaced.
    /// Empty: "{noun} {size} {title}" (with "×{L}" before the title when
    /// there is a length).
    std::string designation;
    std::vector<PackColumn>          columns;
    std::vector<std::vector<double>> rows;
    /// Names of the rows ("10П", "20Б1") written instead of `size` when the
    /// standard names its sizes by number; empty: by `size`.
    std::vector<std::string>         labels;
    PackLength                       length;
    std::vector<PackInput>           inputs;
};

struct Pack {
    std::string id;        ///< "gost30245"
    std::string title;
    std::string source;    ///< where the numbers come from (standard, table)
    std::string author;
    std::string version;
    std::vector<PackKind>     kinds;
    std::vector<PackStandard> standards;
    std::filesystem::path     file;   ///< where it was read from (empty: from text)
};

/// The pack as JSON text (what ParsePack reads back): for packs the program
/// writes itself, the user's library.
std::string PackToJson(const Pack& pack);

/// Reads a pack from JSON text. Checks everything the program relies on:
/// ids, columns, rows, expressions name only the standard's columns and L.
/// Errors are in Russian and name the place ("стандарт «…»: …").
Expected<Pack> ParsePack(std::string_view json);
Expected<Pack> ReadPack(const std::filesystem::path& file);

/// Checks a standard against the kind it belongs to: placeholders name its
/// columns, the recipe's expressions name its columns (and L) and give
/// sizes above zero for every row.
Status CheckStandard(const PackStandard& standard, const PackKind& kind);

/// A number as packs write it in sizes and expressions: "2", "2.5".
std::string PackNumber(double v);
/// `text` with "{column}" replaced by the row's values and "{key}" by `extra`.
std::string FillPackTemplate(std::string_view text, const PackStandard& standard, std::size_t row,
                             std::span<const std::pair<std::string, std::string>> extra);
/// The size of a row as the library shows it ("40×20×2").
std::string PackSize(const PackStandard& standard, std::size_t row);

// --- Loaded packs ------------------------------------------------------------

struct PacksLoaded {
    int                      packs = 0;
    std::vector<std::string> problems;   ///< one per file that was skipped, with its name
};

/// Replaces the loaded packs with the *.bzlib files of `folders` (in order;
/// a pack whose id is loaded already is skipped quietly — the same pack from
/// a later folder —, one with a kind or a standard id already loaded is
/// skipped with a problem).
/// Pointers from the library (StandardInfo, KindInfo) are invalid afterwards.
PacksLoaded LoadPacks(std::span<const std::filesystem::path> folders);
/// Adds a pack (tests, a pack just installed). Fails like LoadPacks would.
Status AddPack(Pack pack);
/// Forgets every pack.
void ClearPacks();
std::span<const std::unique_ptr<Pack>> LoadedPacks();
/// A kind of the loaded packs, or nullptr.
const PackKind* FindPackKind(std::string_view id);
/// Changes with every LoadPacks / AddPack / ClearPacks (the library caches by it).
unsigned PacksGeneration();

/// Checks `file` and copies it into `folder` (named after the pack id),
/// replacing an older copy of the same pack. Returns the copy's path; the
/// caller reloads the packs.
Expected<std::filesystem::path> InstallPack(const std::filesystem::path& file, const std::filesystem::path& folder);

} // namespace bz::doc::parts
