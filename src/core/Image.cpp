#include "core/Image.h"

#include <stb_image.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <memory>

namespace bz {

Expected<Image> DecodeImage(std::span<const std::uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > 0x7FFFFFFF) return Error{"Картинка пустая или слишком большая"};
    int w = 0, h = 0, channels = 0;
    const std::unique_ptr<stbi_uc, void (*)(void*)> pixels(
        stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4), stbi_image_free);
    if (!pixels) return Error{"Картинка не читается (нужен PNG, JPEG, BMP или TGA)"};
    Image image;
    image.width = w;
    image.height = h;
    image.rgba.assign(pixels.get(), pixels.get() + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    return image;
}

Expected<Image> ReadImageFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return Error{"Не удалось открыть картинку"};
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return DecodeImage(bytes);
}

Image FitWithin(const Image& image, int maxSide) {
    if (maxSide < 1 || (image.width <= maxSide && image.height <= maxSide) || image.width <= 0 || image.height <= 0) return image;
    const double k = static_cast<double>(maxSide) / std::max(image.width, image.height);
    Image out;
    out.width  = std::max(1, static_cast<int>(image.width * k + 0.5));
    out.height = std::max(1, static_cast<int>(image.height * k + 0.5));
    out.rgba.resize(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4);
    for (int y = 0; y < out.height; ++y) {
        // The block of source rows and columns this pixel covers (at least one).
        const int y0 = y * image.height / out.height;
        const int y1 = std::max(y0 + 1, (y + 1) * image.height / out.height);
        for (int x = 0; x < out.width; ++x) {
            const int x0 = x * image.width / out.width;
            const int x1 = std::max(x0 + 1, (x + 1) * image.width / out.width);
            unsigned long sum[4] = {0, 0, 0, 0};
            for (int sy = y0; sy < y1; ++sy)
                for (int sx = x0; sx < x1; ++sx) {
                    const std::size_t i = (static_cast<std::size_t>(sy) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(sx)) * 4;
                    for (int c = 0; c < 4; ++c) sum[c] += image.rgba[i + static_cast<std::size_t>(c)];
                }
            const unsigned long n = static_cast<unsigned long>(y1 - y0) * static_cast<unsigned long>(x1 - x0);
            const std::size_t o = (static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) + static_cast<std::size_t>(x)) * 4;
            for (int c = 0; c < 4; ++c) out.rgba[o + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>((sum[c] + n / 2) / n);
        }
    }
    return out;
}

} // namespace bz
