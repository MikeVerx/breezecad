#include "core/PngImage.h"

#include <miniz.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

namespace bz {
namespace {

std::uint32_t BigEndian(const std::uint8_t* p) {
    return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) | std::uint32_t{p[3]};
}

std::uint8_t Paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return static_cast<std::uint8_t>(a);
    return static_cast<std::uint8_t>(pb <= pc ? b : c);
}

} // namespace

Expected<Image> DecodePng(std::span<const std::uint8_t> bytes) {
    static constexpr std::uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (bytes.size() < 8 || std::memcmp(bytes.data(), kSignature, 8) != 0) return Error{"Not a PNG file"};
    int width = 0, height = 0, channels = 0;
    std::vector<std::uint8_t> packed;   // the IDAT chunks joined
    for (std::size_t at = 8; at + 12 <= bytes.size();) {
        const std::uint32_t length = BigEndian(&bytes[at]);
        if (at + 12 + length > bytes.size()) return Error{"PNG: broken chunk"};
        const std::uint8_t* type = &bytes[at + 4];
        const std::uint8_t* data = &bytes[at + 8];
        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (length < 13) return Error{"PNG: broken header"};
            width = static_cast<int>(BigEndian(data));
            height = static_cast<int>(BigEndian(data + 4));
            const int depth = data[8], colour = data[9], interlace = data[12];
            if (depth != 8 || interlace != 0 || (colour != 2 && colour != 6))
                return Error{"PNG: only 8-bit RGB or RGBA without interlacing is supported"};
            channels = colour == 6 ? 4 : 3;
            if (width <= 0 || height <= 0 || width > 16384 || height > 16384) return Error{"PNG: bad size"};
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            packed.insert(packed.end(), data, data + length);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        at += 12 + length;
    }
    if (channels == 0) return Error{"PNG: no header"};

    const std::size_t stride = static_cast<std::size_t>(width) * static_cast<std::size_t>(channels);
    std::vector<std::uint8_t> raw((stride + 1) * static_cast<std::size_t>(height));
    mz_ulong rawSize = static_cast<mz_ulong>(raw.size());
    if (mz_uncompress(raw.data(), &rawSize, packed.data(), static_cast<mz_ulong>(packed.size())) != MZ_OK || rawSize != raw.size())
        return Error{"PNG: broken image data"};

    // Undo the per-row filters (in place: each row then holds plain pixels).
    for (int y = 0; y < height; ++y) {
        std::uint8_t* row = &raw[static_cast<std::size_t>(y) * (stride + 1)];
        const std::uint8_t filter = row[0];
        std::uint8_t* px = row + 1;
        const std::uint8_t* up = y > 0 ? &raw[static_cast<std::size_t>(y - 1) * (stride + 1) + 1] : nullptr;
        for (std::size_t i = 0; i < stride; ++i) {
            const int a = i >= static_cast<std::size_t>(channels) ? px[i - static_cast<std::size_t>(channels)] : 0;
            const int b = up ? up[i] : 0;
            const int c = up && i >= static_cast<std::size_t>(channels) ? up[i - static_cast<std::size_t>(channels)] : 0;
            switch (filter) {
                case 0: break;
                case 1: px[i] = static_cast<std::uint8_t>(px[i] + a); break;
                case 2: px[i] = static_cast<std::uint8_t>(px[i] + b); break;
                case 3: px[i] = static_cast<std::uint8_t>(px[i] + (a + b) / 2); break;
                case 4: px[i] = static_cast<std::uint8_t>(px[i] + Paeth(a, b, c)); break;
                default: return Error{"PNG: unknown row filter"};
            }
        }
    }

    Image image;
    image.width = width;
    image.height = height;
    image.rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* src = &raw[static_cast<std::size_t>(y) * (stride + 1) + 1];
        std::uint8_t* dst = &image.rgba[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4];
        for (int x = 0; x < width; ++x, src += channels, dst += 4) {
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = channels == 4 ? src[3] : 255;
        }
    }
    return image;
}

Expected<Image> LoadPng(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return Error{"Cannot open the picture"};
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return DecodePng(bytes);
}

} // namespace bz
