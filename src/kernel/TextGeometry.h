#pragma once
// Letters of sketch text as geometry (OCCT's TrueType outlines). The
// interface draws the outlines, the model builds faces from the same shapes.

#include "core/Math.h"

#include <filesystem>
#include <string>
#include <tuple>
#include <vector>

namespace bz::kernel {

/// What a text looks like (sketch::Text without its box).
struct TextFormat {
    std::string text;
    double      height = 5.0;
    std::string font;          ///< family; empty: the program's own font
    bool        bold = false;
    bool        italic = false;
    int         hAlign = 0;    ///< 0 left, 1 centre, 2 right
    int         vAlign = 0;    ///< 0 bottom, 1 middle, 2 top

    auto Key() const { return std::tie(text, height, font, bold, italic, hAlign, vAlign); }
    bool operator<(const TextFormat& o) const { return Key() < o.Key(); }
};

/// The program's own font files: regular and bold (set once at start).
void SetTextFont(const std::filesystem::path& regular, const std::filesystem::path& bold);

/// Font families installed in the system, sorted (scanned once).
const std::vector<std::string>& AvailableFonts();

/// Closed outlines of the letters laid out in a box `width` x `height`, in box
/// coordinates (origin at the bottom-left corner, x along the bottom side).
/// Cached; empty without a font.
std::vector<std::vector<Vec2>> TextOutlines(const TextFormat& format, double boxWidth, double boxHeight);

/// Size of the text's block (width, height): a box that just holds it.
Vec2 TextExtent(const TextFormat& format);

} // namespace bz::kernel
