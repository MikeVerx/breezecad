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

} // namespace bz
