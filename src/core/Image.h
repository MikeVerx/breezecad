#pragma once
// Reading pictures: PNG, JPEG, BMP, TGA (through stb_image). Always RGBA.

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

Expected<Image> DecodeImage(std::span<const std::uint8_t> bytes);
Expected<Image> ReadImageFile(const std::filesystem::path& file);

/// The picture made smaller so that neither side exceeds `maxSide` (the
/// proportions kept; each new pixel is the average of the ones it covers, so
/// fine detail blurs instead of shimmering). A picture that fits comes back
/// as it is; `maxSide` < 1 means no limit.
Image FitWithin(const Image& image, int maxSide);

} // namespace bz
