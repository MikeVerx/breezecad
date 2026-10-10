#pragma once

#include "app/MouseNavigation.h"

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
    int         viewBackground = 0;       ///< 0 gradient, 1 plain light, 2 plain dark (theme colours)
    bool        reopenLast     = false;   ///< open the most recent file at start
    int         newPartLength  = 0;       ///< length unit of new parts: LengthUnit as int (0 = mm)
    double      orbitSpeed     = 1.0;     ///< view rotation sensitivity, 0.5..2
    bool        invertOrbit    = false;   ///< drag up turns the model down
    int         autosaveMinutes = 5;      ///< 0 turns autosave off
    bool        gridSnap       = true;
    bool        pointSnap      = true;
    bool        orthoMode      = false;   ///< "Орто" (F8): lines only horizontal or vertical
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
    std::vector<std::filesystem::path> recentFiles;   ///< most recent first

    static constexpr std::size_t kMaxRecent = 12;

    void AddRecent(const std::filesystem::path& file);
    void RemoveRecent(const std::filesystem::path& file);

    /// Reads settings; missing or broken files give the defaults.
    static Settings Load(const std::filesystem::path& file);
    bool Save(const std::filesystem::path& file) const;
};

} // namespace bz::app
