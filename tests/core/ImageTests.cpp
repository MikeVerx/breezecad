#include "core/Image.h"

#include <doctest.h>
#include <miniz.h>

#include <cstdint>
#include <filesystem>
#include <vector>

using namespace bz;

TEST_CASE("pictures are read back pixel for pixel") {
    // A 5x3 RGBA picture with every value different, written by miniz.
    std::vector<std::uint8_t> pixels(5 * 3 * 4);
    for (std::size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<std::uint8_t>(i * 7 + 3);
    std::size_t size = 0;
    void* png = tdefl_write_image_to_png_file_in_memory(pixels.data(), 5, 3, 4, &size);
    REQUIRE(png);
    const auto image = DecodeImage({static_cast<const std::uint8_t*>(png), size});
    mz_free(png);
    REQUIRE(image);
    CHECK(image->width == 5);
    CHECK(image->height == 3);
    CHECK(image->rgba == pixels);

    CHECK_FALSE(DecodeImage(std::vector<std::uint8_t>{1, 2, 3}));
}

TEST_CASE("the splash pictures load") {
    for (const char* name : {"part-light.png", "part-dark.png"}) {
        const auto image = ReadImageFile(std::filesystem::path(BZ_SOURCE_DIR) / "assets" / "splash" / name);
        REQUIRE(image);
        CHECK(image->width > 100);
    }
}
