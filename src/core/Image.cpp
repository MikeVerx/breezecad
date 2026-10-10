#include "core/Image.h"

#include <stb_image.h>

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

} // namespace bz
