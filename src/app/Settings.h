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
    std::vector<std::filesystem::path> recentFiles;   ///< most recent first

    static constexpr std::size_t kMaxRecent = 12;

    void AddRecent(const std::filesystem::path& file);
    void RemoveRecent(const std::filesystem::path& file);

    /// Reads settings; missing or broken files give the defaults.
    static Settings Load(const std::filesystem::path& file);
    bool Save(const std::filesystem::path& file) const;
};

} // namespace bz::app
