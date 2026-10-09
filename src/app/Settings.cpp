#include "app/Settings.h"

#include "core/Log.h"
#include "core/PathText.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace bz::app {
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
    s.showAllConstraints = j.value("showAllConstraints", s.showAllConstraints);
    s.gridStep        = std::max(0.0, j.value("gridStep", s.gridStep));
    s.uiScale         = j.value("uiScale", s.uiScale);
    if (s.uiScale != 0.0) s.uiScale = std::clamp(s.uiScale, 0.75, 3.0);
    if (const auto w = j.find("window"); w != j.end() && w->is_object()) {
        s.windowX = w->value("x", -1);
        s.windowY = w->value("y", -1);
        s.windowW = w->value("width", 0);
        s.windowH = w->value("height", 0);
        s.windowMaximized = w->value("maximized", true);
    }
    for (const auto& [id, chord] : j.value("shortcuts", nlohmann::json::object()).items())
        if (chord.is_string()) s.shortcuts.emplace_back(id, chord.get<std::string>());
    for (const auto& r : j.value("recentFiles", nlohmann::json::array()))
        if (r.is_string()) s.recentFiles.push_back(PathFromUtf8(r.get<std::string>()));
    return s;
}

namespace {
nlohmann::json ShortcutsJson(const std::vector<std::pair<std::string, std::string>>& shortcuts) {
    nlohmann::json out = nlohmann::json::object();
    for (const auto& [id, chord] : shortcuts) out[id] = chord;
    return out;
}
} // namespace

bool Settings::Save(const std::filesystem::path& file) const {
    nlohmann::json recent = nlohmann::json::array();
    for (const auto& r : recentFiles) recent.push_back(ToUtf8(r));
    const nlohmann::json j{
        {"theme", theme == ThemeChoice::Light ? "light" : theme == ThemeChoice::Dark ? "dark" : "system"},
        {"animations", animations},
        {"autosaveMinutes", autosaveMinutes},
        {"gridSnap", gridSnap},
        {"pointSnap", pointSnap},
        {"showAllConstraints", showAllConstraints},
        {"gridStep", gridStep},
        {"uiScale", uiScale},
        {"window", {{"x", windowX}, {"y", windowY}, {"width", windowW}, {"height", windowH}, {"maximized", windowMaximized}}},
        {"shortcuts", ShortcutsJson(shortcuts)},
        {"recentFiles", recent},
    };
    std::ofstream out(file, std::ios::trunc);
    out << j.dump(2);
    return static_cast<bool>(out);
}

} // namespace bz::app
