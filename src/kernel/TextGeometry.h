#pragma once
// Letters of sketch text as geometry (OCCT's TrueType outlines of the
// program's own font). The interface draws the outlines, the model builds
// faces from the same shapes.

#include "core/Math.h"

#include <filesystem>
#include <string>
#include <vector>

namespace bz::kernel {

/// The font of sketch text (set once at start; before that text has no shape).
void SetTextFont(const std::filesystem::path& file);

/// Closed outlines of the letters, in text coordinates: the left end of the
/// baseline at the origin, `height` the font size. Cached; empty without a font.
const std::vector<std::vector<Vec2>>& TextOutlines(const std::string& text, double height);

} // namespace bz::kernel
