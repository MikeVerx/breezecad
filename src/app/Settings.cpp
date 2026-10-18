#include "app/Settings.h"

#include "core/I18n.h"
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

int Settings::TextureLimit(int textureQuality) {
    // High keeps what a usual photo has; low is plenty for a small preview.
    static constexpr int kLimits[] = {4096, 1024, 256};
    return kLimits[std::clamp(textureQuality, 0, 2)];
}

namespace {

/// Defaults for a damaged file, which is kept next to it as "<name>.bak":
/// the next Save overwrites the original, and the user may want it back.
Settings Damaged(const std::filesystem::path& file, std::string_view why) {
    log::Warn("Settings file is damaged ({}), using defaults", why);
    std::filesystem::path backup = file;
    backup += ".bak";
    std::error_code ec;
    std::filesystem::copy_file(file, backup, std::filesystem::copy_options::overwrite_existing, ec);
    Settings s;
    s.loadProblem = N_("Файл настроек повреждён, настройки сброшены. Прежний файл сохранён как settings.json.bak");
    return s;
}

Settings FromJson(const nlohmann::json& j) {
    Settings s;
    const std::string theme = j.value("theme", "system");
    s.theme = theme == "light" ? ThemeChoice::Light : theme == "dark" ? ThemeChoice::Dark : ThemeChoice::System;
    s.animations      = j.value("animations", s.animations);
    s.backgroundRebuild = j.value("backgroundRebuild", s.backgroundRebuild);
    s.showTriad       = j.value("showTriad", s.showTriad);
    s.reopenLast      = j.value("reopenLast", s.reopenLast);
    s.lastNewTemplate = j.value("lastNewTemplate", s.lastNewTemplate);
    // "viewBackground" (0..2) from earlier versions.
    static constexpr const char* kOld[] = {"theme", "light", "dark"};
    s.background = kOld[std::clamp(j.value("viewBackground", 0), 0, 2)];
    s.background = j.value("background", s.background);
    s.backgroundPicture = j.value("backgroundPicture", s.backgroundPicture);
    s.newPartLength   = std::clamp(j.value("newPartLength", s.newPartLength), 0, 3);
    s.orbitSpeed      = std::clamp(j.value("orbitSpeed", s.orbitSpeed), 0.25, 4.0);
    s.invertOrbit     = j.value("invertOrbit", s.invertOrbit);
    s.turntableOrbit  = j.value("turntableOrbit", s.turntableOrbit);
    s.flickGestures   = j.value("flickGestures", s.flickGestures);
    s.autosaveMinutes = std::clamp(j.value("autosaveMinutes", s.autosaveMinutes), 0, 120);
    s.gridSnap        = j.value("gridSnap", s.gridSnap);
    s.pointSnap       = j.value("pointSnap", s.pointSnap);
    s.orthoMode       = j.value("orthoMode", s.orthoMode);
    s.graphics        = std::clamp(j.value("graphics", s.graphics), 0, 2);
    s.textureQuality  = std::clamp(j.value("textureQuality", s.textureQuality), 0, 2);
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
    if (const auto m = j.find("mobile"); m != j.end() && m->is_object()) {
        MobileSettings& o = s.mobile;
        o.layout = std::clamp(m->value("layout", o.layout), 0, 2);
        o.leftHanded = m->value("leftHanded", o.leftHanded);
        o.leftWidth = std::clamp(m->value("leftWidth", o.leftWidth), 12, 40);
        o.rightWidth = std::clamp(m->value("rightWidth", o.rightWidth), 8, 40);
        o.iconsOnly = m->value("iconsOnly", o.iconsOnly);
        o.labelScale = std::clamp(m->value("labelScale", o.labelScale), 0.6, 2.5);
        o.volumeZoom = m->value("volumeZoom", o.volumeZoom);
        o.fingerOrbit = std::clamp(m->value("fingerOrbit", o.fingerOrbit), 0.25, 4.0);
        o.fingerPan = std::clamp(m->value("fingerPan", o.fingerPan), 0.25, 4.0);
        o.fingerZoom = std::clamp(m->value("fingerZoom", o.fingerZoom), 0.25, 4.0);
        // The gesture sets: drags 0 none, 1 orbit, 2 pan, 3 zoom; taps — FingerTap.
        const auto readSet = [&](const char* key, FingerGestures& g) {
            const auto it = m->find(key);
            if (it == m->end() || !it->is_object()) return;
            const auto drag = [&](const char* k, mobile::FingerDrag& d) {
                d = static_cast<mobile::FingerDrag>(std::clamp(it->value(k, static_cast<int>(d)), 0, 3));
            };
            const auto tap = [&](const char* k, FingerTap& t) { t = static_cast<FingerTap>(std::clamp(it->value(k, static_cast<int>(t)), 0, 4)); };
            drag("one", g.drags.one);
            drag("two", g.drags.two);
            drag("three", g.drags.three);
            g.drags.pinch = it->value("pinch", g.drags.pinch);
            tap("tap2", g.tap2);
            tap("tap3", g.tap3);
        };
        o.toolsList = m->value("toolsList", o.toolsList);
        o.hotbarWidth = std::clamp(m->value("hotbarWidth", o.hotbarWidth), 0, 120);
        o.stripHeight = std::clamp(m->value("stripHeight", o.stripHeight), 32, 80);
        o.firstStartDone = m->value("firstStartDone", o.firstStartDone);
        const auto readIsland = [&](const char* key, IslandPlace& p) {
            const auto it = m->find(key);
            if (it == m->end() || !it->is_object()) return;
            p.bottom = it->value("bottom", p.bottom);
            p.along = std::clamp(it->value("along", p.along), -1.0, 1.0);
            p.scale = std::clamp(it->value("scale", p.scale), 0.8, 1.4);
        };
        readIsland("undoIsland", o.undoIsland);
        readIsland("tabsIsland", o.tabsIsland);
        readIsland("cubeIsland", o.cubeIsland);
        readSet("gesturesOff", o.gesturesOff);
        readSet("gesturesOn", o.gesturesOn);
        o.tapSlop = std::clamp(m->value("tapSlop", o.tapSlop), 3, 40);
        o.longPress = std::clamp(m->value("longPress", o.longPress), 0.2, 2.0);
        o.cursorSpeed = std::clamp(m->value("cursorSpeed", o.cursorSpeed), 0.1, 1.0);
        o.cursorReach = std::clamp(m->value("cursorReach", o.cursorReach), 16, 120);
        o.scrollSpeed = std::clamp(m->value("scrollSpeed", o.scrollSpeed), 0.25, 4.0);
        o.askedNotifications = m->value("askedNotifications", o.askedNotifications);
        o.projectsFolderName = m->value("projectsFolderName", o.projectsFolderName);
        o.hotbar.clear();
        for (const auto& id : m->value("hotbar", nlohmann::json::array()))
            if (id.is_string()) o.hotbar.push_back(id.get<std::string>());
        o.recentLooks.clear();
        for (const auto& id : m->value("recentLooks", nlohmann::json::array()))
            if (id.is_string() && o.recentLooks.size() < 6) o.recentLooks.push_back(id.get<std::string>());
        o.openGroups.clear();
        for (const auto& id : m->value("openGroups", nlohmann::json::array()))
            if (id.is_string()) o.openGroups.push_back(id.get<std::string>());
    }
    s.shortcutsCustom = j.value("shortcutsCustom", s.shortcutsCustom);
    // Named: items() of a temporary would outlive it (ASan: stack-use-after-scope).
    const nlohmann::json shortcuts = j.value("shortcuts", nlohmann::json::object());
    for (const auto& [id, chord] : shortcuts.items())
        if (chord.is_string()) s.shortcuts.emplace_back(id, chord.get<std::string>());
    for (const auto& r : j.value("recentFiles", nlohmann::json::array()))
        if (r.is_string()) s.recentFiles.push_back(PathFromUtf8(r.get<std::string>()));
    return s;
}

} // namespace

Settings Settings::Load(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) return {};
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    in.close();
    if (j.is_discarded() || !j.is_object()) return Damaged(file, "not a JSON object");
    // A field of another type ("uiScale": "1,25") throws inside nlohmann::json;
    // uncaught, it closed the program before the window on every start (B-11).
    try {
        return FromJson(j);
    } catch (const nlohmann::json::exception& e) {
        return Damaged(file, e.what());
    }
}

namespace {
nlohmann::json ShortcutsJson(const std::vector<std::pair<std::string, std::string>>& shortcuts) {
    nlohmann::json out = nlohmann::json::object();
    for (const auto& [id, chord] : shortcuts) out[id] = chord;
    return out;
}
nlohmann::json IslandJson(const IslandPlace& p) { return {{"bottom", p.bottom}, {"along", p.along}, {"scale", p.scale}}; }
nlohmann::json GestureSetJson(const FingerGestures& g) {
    return {{"one", static_cast<int>(g.drags.one)},     {"two", static_cast<int>(g.drags.two)},
            {"three", static_cast<int>(g.drags.three)}, {"pinch", g.drags.pinch},
            {"tap2", static_cast<int>(g.tap2)},         {"tap3", static_cast<int>(g.tap3)}};
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
        {"textureQuality", textureQuality},
        {"backgroundRebuild", backgroundRebuild},
        {"showAllConstraints", showAllConstraints},
        {"gridStep", gridStep},
        {"uiScale", uiScale},
        {"showTriad", showTriad},
        {"reopenLast", reopenLast},
        {"lastNewTemplate", lastNewTemplate},
        {"background", background},
        {"backgroundPicture", backgroundPicture},
        {"newPartLength", newPartLength},
        {"orbitSpeed", orbitSpeed},
        {"invertOrbit", invertOrbit},
        {"turntableOrbit", turntableOrbit},
        {"flickGestures", flickGestures},
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
        {"mobile", {{"layout", mobile.layout},
                    {"leftHanded", mobile.leftHanded},
                    {"leftWidth", mobile.leftWidth},
                    {"rightWidth", mobile.rightWidth},
                    {"iconsOnly", mobile.iconsOnly},
                    {"labelScale", mobile.labelScale},
                    {"volumeZoom", mobile.volumeZoom},
                    {"fingerOrbit", mobile.fingerOrbit},
                    {"fingerPan", mobile.fingerPan},
                    {"fingerZoom", mobile.fingerZoom},
                    {"toolsList", mobile.toolsList},
                    {"hotbarWidth", mobile.hotbarWidth},
                    {"stripHeight", mobile.stripHeight},
                    {"firstStartDone", mobile.firstStartDone},
                    {"undoIsland", IslandJson(mobile.undoIsland)},
                    {"tabsIsland", IslandJson(mobile.tabsIsland)},
                    {"cubeIsland", IslandJson(mobile.cubeIsland)},
                    {"gesturesOff", GestureSetJson(mobile.gesturesOff)},
                    {"gesturesOn", GestureSetJson(mobile.gesturesOn)},
                    {"tapSlop", mobile.tapSlop},
                    {"longPress", mobile.longPress},
                    {"cursorSpeed", mobile.cursorSpeed},
                    {"cursorReach", mobile.cursorReach},
                    {"scrollSpeed", mobile.scrollSpeed},
                    {"hotbar", mobile.hotbar},
                    {"openGroups", mobile.openGroups},
                    {"recentLooks", mobile.recentLooks},
                    {"askedNotifications", mobile.askedNotifications},
                    {"projectsFolderName", mobile.projectsFolderName}}},
        {"shortcutsCustom", shortcutsCustom},
        {"recentFiles", recent},
    };
    std::ofstream out(file, std::ios::trunc);
    out << j.dump(2);
    return static_cast<bool>(out);
}

} // namespace bz::app
