#pragma once
// Backgrounds of the 3D view: the theme's own, gradients in the spirit of
// other CAD programs, or the user's own picture (static, not turning with
// the camera).

#include "core/Color.h"
#include "ui/Theme.h"

#include <span>
#include <string_view>

namespace bz::app {

struct BackgroundPreset {
    const char* id;     ///< kept in the settings
    const char* name;   ///< translated when shown
    Color       top, bottom;   ///< for a radial one: centre, edge
    bool        radial = false;
    bool        themed = false;   ///< colours come from the theme (light or dark)
};

/// "picture": the user's own picture (Settings::backgroundPicture).
inline constexpr std::string_view kPictureBackground = "picture";

std::span<const BackgroundPreset> BackgroundPresets();
/// The preset with this id, or the theme's gradient.
const BackgroundPreset& FindBackground(std::string_view id);

/// The two colours the view is painted with, for the theme in use.
struct BackgroundColours {
    Color top, bottom;
    bool  radial = false;
};
BackgroundColours ColoursOf(const BackgroundPreset& preset, const ui::ThemeColors& theme);

} // namespace bz::app
