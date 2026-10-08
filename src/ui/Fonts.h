#pragma once

#include <filesystem>

struct ImFont;

namespace bz::ui {

/// The font set of the design system: IBM Plex Sans in three weights and
/// IBM Plex Mono for numbers. Sizes follow the mockup.
struct Fonts {
    ImFont* body      = nullptr;   ///< Sans 13 regular: most text
    ImFont* bodySmall = nullptr;   ///< Sans 12 regular: labels, ribbon
    ImFont* caption   = nullptr;   ///< Sans 11 regular: group captions, status bar
    ImFont* medium    = nullptr;   ///< Sans 13 medium: buttons, tabs
    ImFont* strong    = nullptr;   ///< Sans 13 semibold: panel titles
    ImFont* title     = nullptr;   ///< Sans 14 semibold: operation panel title
    ImFont* display   = nullptr;   ///< Sans 22 semibold: home screen
    ImFont* mono      = nullptr;   ///< Mono 12: values, coordinates
    ImFont* monoLarge = nullptr;   ///< Mono 14: numeric fields

    /// Loads fonts from `assets/fonts` at the given UI scale. Replaces the
    /// ImGui font atlas; call before the first frame or when the scale changes.
    bool Load(const std::filesystem::path& assetsDir, float scale);
};

} // namespace bz::ui
