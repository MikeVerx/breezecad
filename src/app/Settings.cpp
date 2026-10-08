#include "app/Settings.h"

#include "core/Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace bz::app {
namespace {

std::string Utf8(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return {s.begin(), s.end()};
}

std::filesystem::path FromUtf8(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

} // namespace

void Settings::AddRecent(const std::filesystem::path& file) {
    RemoveRecent(file);
    recentFiles.insert(recentFiles.begin(), file);
    if (recentFiles.size() > kMaxRecent) recentFiles.resize(kMaxRecent);
}

void Settings::RemoveRecent(const std::filesystem::path& file) {
    std::erase_if(recentFiles, [&](const auto& p) { return p == file; });
}

Settings Settings::Load(const std::filesystem::path& file) {
    Settings s;
    std::ifstream in(file);
    if (!in) return s;
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        log::Warn("Settings file is damaged, using defaults");
        return s;
    }
    const std::string theme = j.value("theme", "system");
    s.theme = theme == "light" ? ThemeChoice::Light : theme == "dark" ? ThemeChoice::Dark : ThemeChoice::System;
    s.animations      = j.value("animations", s.animations);
    s.autosaveMinutes = std::clamp(j.value("autosaveMinutes", s.autosaveMinutes), 0, 120);
    s.gridSnap        = j.value("gridSnap", s.gridSnap);
    s.pointSnap       = j.value("pointSnap", s.pointSnap);
    s.gridStep        = std::max(0.0, j.value("gridStep", s.gridStep));
    for (const auto& r : j.value("recentFiles", nlohmann::json::array()))
        if (r.is_string()) s.recentFiles.push_back(FromUtf8(r.get<std::string>()));
    return s;
}

bool Settings::Save(const std::filesystem::path& file) const {
    nlohmann::json recent = nlohmann::json::array();
    for (const auto& r : recentFiles) recent.push_back(Utf8(r));
    const nlohmann::json j{
        {"theme", theme == ThemeChoice::Light ? "light" : theme == ThemeChoice::Dark ? "dark" : "system"},
        {"animations", animations},
        {"autosaveMinutes", autosaveMinutes},
        {"gridSnap", gridSnap},
        {"pointSnap", pointSnap},
        {"gridStep", gridStep},
        {"recentFiles", recent},
    };
    std::ofstream out(file, std::ios::trunc);
    out << j.dump(2);
    return static_cast<bool>(out);
}

} // namespace bz::app
