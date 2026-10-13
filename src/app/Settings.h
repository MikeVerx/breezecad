#pragma once

#include "app/MouseNavigation.h"
#include "app/TouchNavigation.h"

#include <filesystem>
#include <string>
#include <vector>

namespace bz::app {

enum class ThemeChoice : unsigned char { System, Light, Dark };

/// User preferences, stored as JSON in the config folder.
struct Settings {
    ThemeChoice theme          = ThemeChoice::System;
    bool        animations     = true;    ///< smooth camera moves
    bool        showTriad      = true;    ///< axis triad in the corner of the view
    std::string background = "theme";     ///< BackgroundPreset id, or "picture"
    std::string backgroundPicture;        ///< the user's picture for "picture" (UTF-8 path)
    bool        reopenLast     = false;   ///< open the most recent file at start
    int         newPartLength  = 0;       ///< length unit of new parts: LengthUnit as int (0 = mm)
    double      orbitSpeed     = 1.0;     ///< view rotation sensitivity, 0.5..2
    bool        invertOrbit    = false;   ///< drag up turns the model down
    bool        turntableOrbit = false;   ///< sideways drag turns about world Z (else about the screen's vertical)
    int         autosaveMinutes = 5;      ///< 0 turns autosave off
    bool        gridSnap       = true;
    bool        pointSnap      = true;
    bool        orthoMode      = false;   ///< "Орто" (F8): lines only horizontal or vertical
    int         graphics       = 0;       ///< 0 high, 1 balanced, 2 for weak computers (smoothing, round faces)
    int         textureQuality = 0;   ///< pictures on faces: 0 high, 1 medium, 2 low (TextureLimit)
    bool        backgroundRebuild = true;   ///< long rebuilds on another thread (the window stays responsive)
    bool        showAllConstraints = false;   ///< sketch: every constraint glyph, or only those of the selection (F8 / F9)
    double      gridStep       = 0.0;     ///< mm; 0 = adaptive to zoom
    double      uiScale        = 0.0;     ///< interface scale; 0 = automatic (screen density and size)
    std::string language;                 ///< interface language code ("ru", "en"); empty = as the system
    /// The window as last left (windowed size and place; maximized flag).
    int         windowX = -1, windowY = -1, windowW = 0, windowH = 0;
    bool        windowMaximized = true;
    /// Shortcuts changed by the user: action id -> chord text ("Ctrl+Shift+E"; empty: none).
    std::vector<std::pair<std::string, std::string>> shortcuts;
    std::string shortcutPreset = "breezecad";   ///< the set the shortcuts started from
    bool        shortcutsCustom = false;        ///< changed by hand after choosing the set
    MouseNavigation mouse;                       ///< view gestures
    std::string mousePreset = "breezecad";
    bool        mouseCustom = false;
    TouchNavigation touch;                       ///< touchpad gestures
    std::vector<std::filesystem::path> recentFiles;   ///< most recent first

    static constexpr std::size_t kMaxRecent = 12;

    /// The largest side, in pixels, a picture on a face keeps at `textureQuality`.
    static int TextureLimit(int textureQuality);

    void AddRecent(const std::filesystem::path& file);
    void RemoveRecent(const std::filesystem::path& file);

    /// Reads settings; missing or broken files give the defaults.
    static Settings Load(const std::filesystem::path& file);
    bool Save(const std::filesystem::path& file) const;
};

} // namespace bz::app
