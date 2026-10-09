#pragma once

#include <cstdint>

namespace bz {

/// Linear RGBA colour, components 0..1.
struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    /// From 0xRRGGBB as written in the design mockups.
    static constexpr Color Hex(std::uint32_t rgb, float alpha = 1.0f) {
        return {static_cast<float>((rgb >> 16) & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                static_cast<float>(rgb & 0xFF) / 255.0f, alpha};
    }

    /// Fully transparent. Note that a default-constructed Color is opaque black.
    static constexpr Color Transparent() { return {0.0f, 0.0f, 0.0f, 0.0f}; }

    constexpr Color WithAlpha(float alpha) const { return {r, g, b, alpha}; }
    constexpr bool operator==(const Color&) const = default;
};

} // namespace bz
