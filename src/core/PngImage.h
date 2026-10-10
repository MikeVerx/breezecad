#pragma once
// Reading PNG pictures shipped with the program (the splash picture): 8-bit
// RGB or RGBA, not interlaced. Built on miniz's inflate, so no image library
// is needed.

#include "core/Expected.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace bz {

struct Image {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;   ///< width * height * 4, rows top to bottom
};

/// Decodes PNG bytes. Other kinds of PNG (palette, 16-bit, interlaced) are
/// refused with an error rather than shown wrong.
Expected<Image> DecodePng(std::span<const std::uint8_t> bytes);
Expected<Image> LoadPng(const std::filesystem::path& file);

} // namespace bz
