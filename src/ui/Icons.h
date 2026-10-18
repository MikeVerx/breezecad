#pragma once

#include "core/Color.h"

#include <imgui.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace bz::ui {

/// SVG icons from assets/icons, rasterised on demand at the exact pixel size
/// (crisp at any UI scale) and cached as OpenGL textures.
///
/// Coloured icons keep their colours. Monochrome icons (those drawn with
/// `currentColor`, the "ui-*" set) are rasterised white and tinted when drawn.
class Icons {
public:
    Icons() = default;
    ~Icons();
    Icons(const Icons&) = delete;
    Icons& operator=(const Icons&) = delete;

    void SetDirectory(const std::filesystem::path& iconsDir) { myDir = iconsDir; }
    /// Drops all textures (after a scale change they are re-rasterised).
    void SetScale(float scale);
    /// The dark theme: the colour icons' dark arrows and outlines (#3D4552) are drawn light,
    /// or they vanish on a dark panel (the author, 18.10.2026). Drops the cache when it changes.
    void SetDark(bool dark);

    /// Texture of `name` at `size` logical pixels; 0 when the icon is missing.
    ImTextureID Texture(std::string_view name, float size);
    bool IsMonochrome(std::string_view name);

    /// Draws an icon at `pos` (top-left). Monochrome icons take `tint`.
    void Draw(ImDrawList* list, std::string_view name, ImVec2 pos, float size, Color tint);

private:
    struct Svg {
        std::string text;
        bool        monochrome = false;
        bool        missing    = false;
    };
    const Svg& Load(std::string_view name);
    void Clear();

    std::filesystem::path                     myDir;
    float                                     myScale = 1.0f;
    bool                                      myDark = false;
    std::unordered_map<std::string, Svg>      mySvgs;
    std::unordered_map<std::string, unsigned> myTextures;   // "name@px" -> GL texture
};

} // namespace bz::ui
