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
    s.backgroundRebuild = j.value("backgroundRebuild", s.backgroundRebuild);
    s.showTriad       = j.value("showTriad", s.showTriad);
    s.reopenLast      = j.value("reopenLast", s.reopenLast);
    // "viewBackground" (0..2) from earlier versions.
    static constexpr const char* kOld[] = {"theme", "light", "dark"};
    s.background = kOld[std::clamp(j.value("viewBackground", 0), 0, 2)];
    s.background = j.value("background", s.background);
    s.backgroundPicture = j.value("backgroundPicture", s.backgroundPicture);
    s.newPartLength   = std::clamp(j.value("newPartLength", s.newPartLength), 0, 3);
    s.orbitSpeed      = std::clamp(j.value("orbitSpeed", s.orbitSpeed), 0.25, 4.0);
    s.invertOrbit     = j.value("invertOrbit", s.invertOrbit);
    s.autosaveMinutes = std::clamp(j.value("autosaveMinutes", s.autosaveMinutes), 0, 120);
    s.gridSnap        = j.value("gridSnap", s.gridSnap);
    s.pointSnap       = j.value("pointSnap", s.pointSnap);
    s.orthoMode       = j.value("orthoMode", s.orthoMode);
    s.graphics        = std::clamp(j.value("graphics", s.graphics), 0, 2);
    s.showAllConstraints = j.value("showAllConstraints", s.showAllConstraints);
    s.gridStep        = std::max(0.0, j.value("gridStep", s.gridStep));
    s.uiScale         = j.value("uiScale", s.uiScale);
    s.language        = j.value("language", s.language);
    if (s.uiScale != 0.0) s.uiScale = std::clamp(s.uiScale, 0.75, 3.0);
    if (const auto w = j.find("window"); w != j.end() && w->is_object()) {
        s.windowX = w->value("x", -1);
        s.windowY = w->value("y", -1);
        s.windowW = w->value("width", 0);
        s.windowH = w->value("height", 0);
        s.windowMaximized = w->value("maximized", true);
    }
    s.shortcutPreset = j.value("shortcutPreset", s.shortcutPreset);
    if (const auto m = j.find("mouse"); m != j.end() && m->is_object()) {
        const auto gesture = [&](const char* key, MouseGesture& g) {
            if (const auto it = m->find(key); it != m->end() && it->is_array() && it->size() == 2) {
                g.button = static_cast<MouseButtonChoice>(std::clamp((*it)[0].get<int>(), 0, 2));
                g.modifier = static_cast<ModifierChoice>(std::clamp((*it)[1].get<int>(), 0, 3));
            }
        };
        gesture("orbit", s.mouse.orbit);
        gesture("pan", s.mouse.pan);
        gesture("zoom", s.mouse.zoom);
        s.mouse.invertWheel = m->value("invertWheel", false);
        s.mousePreset = m->value("preset", s.mousePreset);
        s.mouseCustom = m->value("custom", false);
    }
    if (const auto t = j.find("touchpad"); t != j.end() && t->is_object()) {
        const auto move = [&](const char* key, TouchMove& m) { m = static_cast<TouchMove>(std::clamp(t->value(key, static_cast<int>(m)), 0, 3)); };
        move("twoFingers", s.touch.twoFingers);
        move("withShift", s.touch.withShift);
        move("withCtrl", s.touch.withCtrl);
        move("withAlt", s.touch.withAlt);
        s.touch.pinchZooms = t->value("pinchZooms", s.touch.pinchZooms);
        s.touch.turn = static_cast<TouchTurn>(std::clamp(t->value("turn", static_cast<int>(s.touch.turn)), 0, 2));
        s.touch.doubleTap = t->value("doubleTap", s.touch.doubleTap);
        s.touch.forceClick = t->value("forceClick", s.touch.forceClick);
        s.touch.speed = std::clamp(t->value("speed", s.touch.speed), 0.25, 4.0);
        s.touch.invert = t->value("invert", s.touch.invert);
        s.touch.smoothIsTouchpad = t->value("smoothIsTouchpad", s.touch.smoothIsTouchpad);
    }
    s.shortcutsCustom = j.value("shortcutsCustom", s.shortcutsCustom);
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
        {"orthoMode", orthoMode},
        {"graphics", graphics},
        {"backgroundRebuild", backgroundRebuild},
        {"showAllConstraints", showAllConstraints},
        {"gridStep", gridStep},
        {"uiScale", uiScale},
        {"showTriad", showTriad},
        {"reopenLast", reopenLast},
        {"background", background},
        {"backgroundPicture", backgroundPicture},
        {"newPartLength", newPartLength},
        {"orbitSpeed", orbitSpeed},
        {"invertOrbit", invertOrbit},
        {"language", language},
        {"window", {{"x", windowX}, {"y", windowY}, {"width", windowW}, {"height", windowH}, {"maximized", windowMaximized}}},
        {"shortcuts", ShortcutsJson(shortcuts)},
        {"shortcutPreset", shortcutPreset},
        {"mouse", {{"orbit", {static_cast<int>(mouse.orbit.button), static_cast<int>(mouse.orbit.modifier)}},
                   {"pan", {static_cast<int>(mouse.pan.button), static_cast<int>(mouse.pan.modifier)}},
                   {"zoom", {static_cast<int>(mouse.zoom.button), static_cast<int>(mouse.zoom.modifier)}},
                   {"invertWheel", mouse.invertWheel},
                   {"preset", mousePreset},
                   {"custom", mouseCustom}}},
        {"touchpad", {{"twoFingers", static_cast<int>(touch.twoFingers)},
                      {"withShift", static_cast<int>(touch.withShift)},
                      {"withCtrl", static_cast<int>(touch.withCtrl)},
                      {"withAlt", static_cast<int>(touch.withAlt)},
                      {"pinchZooms", touch.pinchZooms},
                      {"turn", static_cast<int>(touch.turn)},
                      {"doubleTap", touch.doubleTap},
                      {"forceClick", touch.forceClick},
                      {"speed", touch.speed},
                      {"invert", touch.invert},
                      {"smoothIsTouchpad", touch.smoothIsTouchpad}}},
        {"shortcutsCustom", shortcutsCustom},
        {"recentFiles", recent},
    };
    std::ofstream out(file, std::ios::trunc);
    out << j.dump(2);
    return static_cast<bool>(out);
}

} // namespace bz::app
