#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace bz::app {

enum class ThemeChoice : unsigned char { System, Light, Dark };

/// User preferences, stored as JSON in the config folder.
struct Settings {
    ThemeChoice theme          = ThemeChoice::System;
    bool        animations     = true;    ///< smooth camera moves
    int         autosaveMinutes = 5;      ///< 0 turns autosave off
    bool        gridSnap       = true;
    bool        pointSnap      = true;
    bool        showAllConstraints = false;   ///< sketch: every constraint glyph, or only those of the selection (F8 / F9)
    double      gridStep       = 0.0;     ///< mm; 0 = adaptive to zoom
    double      uiScale        = 0.0;     ///< interface scale; 0 = automatic (screen density and size)
    /// The window as last left (windowed size and place; maximized flag).
    int         windowX = -1, windowY = -1, windowW = 0, windowH = 0;
    bool        windowMaximized = true;
    /// Shortcuts changed by the user: action id -> chord text ("Ctrl+Shift+E"; empty: none).
    std::vector<std::pair<std::string, std::string>> shortcuts;
    std::string shortcutPreset = "breezecad";   ///< the set the shortcuts started from
    bool        shortcutsCustom = false;        ///< changed by hand after choosing the set
    std::vector<std::filesystem::path> recentFiles;   ///< most recent first

    static constexpr std::size_t kMaxRecent = 12;

    void AddRecent(const std::filesystem::path& file);
    void RemoveRecent(const std::filesystem::path& file);

    /// Reads settings; missing or broken files give the defaults.
    static Settings Load(const std::filesystem::path& file);
    bool Save(const std::filesystem::path& file) const;
};

} // namespace bz::app
