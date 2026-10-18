#pragma once

#include "app/MouseNavigation.h"
#include "app/TouchNavigation.h"
#include "app/mobile/Gestures.h"

#include <filesystem>
#include <string>
#include <vector>

namespace bz::app {

enum class ThemeChoice : unsigned char { System, Light, Dark };

/// What a quick tap of two or three fingers does on the touch screen.
enum class FingerTap : unsigned char { None, Undo, Redo, Fit, Home };

/// One set of finger gestures ("Жесты" → "Курсор выкл" / "Курсор вкл").
struct FingerGestures {
    mobile::DragMap drags;
    FingerTap       tap2 = FingerTap::Undo;
    FingerTap       tap3 = FingerTap::Redo;
};

/// Where an island over the view is (the layout editor): stuck to the top or
/// the bottom edge, at a part of the free length, at a size.
struct IslandPlace {
    bool   bottom = false;
    double along = -1.0;   ///< 0 the left end … 1 the right end; < 0 its usual place
    double scale = 1.0;    ///< 0.8 … 1.4
};

/// The phone layout and touch (docs/ADR/0004-mobile-touch-ui.md); used on
/// Android, kept in the file everywhere.
struct MobileSettings {
    int    layout = 0;            ///< 0 by the window's size, 1 always the desktop layout, 2 always the phone one
    bool   leftHanded = false;    ///< blocks 1 and 2 change sides
    int    leftWidth = 20;        ///< block 1, % of the window's width
    int    rightWidth = 20;       ///< block 2, %
    bool   iconsOnly = false;     ///< block 2 shows icons without names (20 / 70 / 10)
    bool   toolsList = false;     ///< block 2 as the long list (default: compact — four buttons a group, the rest in a strip)
    int    hotbarWidth = 0;       ///< dp of the panel at the camera; 0 its safe zone (not narrower than that)
    int    stripHeight = 40;      ///< dp of the projects' strip
    IslandPlace undoIsland, tabsIsland, cubeIsland;   ///< blocks 5, 3 and the cube's button
    bool   firstStartDone = false;   ///< the first-start setup (sensitivity, then the layout) was shown
    double labelScale = 1.0;      ///< labels in the view (cursor, dimensions), apart from the panels
    bool   volumeZoom = false;    ///< the volume keys zoom the view
    // Touch responsiveness ("Управление → Сенсор → Отзывчивость")
    double fingerOrbit = 1.0;     ///< the view turns this much per finger travel (1 = as the mouse)
    double fingerPan = 1.0;       ///< the view moves this much per finger travel (1 = the model under the fingers)
    double fingerZoom = 1.0;      ///< a pinch (or a zoom drag) zooms this much stronger
    FingerGestures gesturesOff{};                                                         ///< the cursor off
    FingerGestures gesturesOn{mobile::GestureRecognizer::DefaultDrags(true)};          ///< the cursor on
    int    tapSlop = 10;          ///< dp a finger may move and still tap (more: taps on a shaky hand; less: quicker drags)
    double longPress = 0.5;       ///< s held still for the context menu
    double cursorSpeed = 1.0;     ///< the cursor's tip moves this part of the finger's travel (less: precise)
    int    cursorReach = 48;      ///< dp from the finger up to the tip
    double scrollSpeed = 1.0;     ///< lists scroll this much per finger travel
    std::vector<std::string> hotbar;   ///< actions added to the panel at the camera, in order
    std::vector<std::string> recentLooks;  ///< the colours chosen last on the phone, newest first («Недавние»)
    std::vector<std::string> openGroups;   ///< block 2's folding groups the user opened ("sketch:Зависимости")
    bool   askedNotifications = false;   ///< the permission for the watchdog's notice was asked once
    std::string projectsFolderName;      ///< for display: the folder chosen (its URI is the system's)
};

/// User preferences, stored as JSON in the config folder.
struct Settings {
    ThemeChoice theme          = ThemeChoice::System;
    bool        animations     = true;    ///< smooth camera moves
    bool        showTriad      = true;    ///< axis triad in the corner of the view
    std::string background = "theme";     ///< BackgroundPreset id, or "picture"
    std::string backgroundPicture;        ///< the user's picture for "picture" (UTF-8 path)
    bool        reopenLast     = false;   ///< open the most recent file at start
    int         newPartLength  = 0;       ///< length unit of new parts: LengthUnit as int (0 = mm)
    std::string lastNewTemplate;          ///< the template last created from in the "Создать" window (empty: none yet)
    double      orbitSpeed     = 1.0;     ///< view rotation sensitivity, 0.5..2
    bool        invertOrbit    = false;   ///< drag up turns the model down
    bool        turntableOrbit = false;   ///< sideways drag turns about world Z (else about the screen's vertical)
    bool        flickGestures  = false;   ///< a quick pull with the right button runs the radial menu's command that way
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
    MobileSettings  mobile;                      ///< phone layout and touch screen
    std::vector<std::filesystem::path> recentFiles;   ///< most recent first
    /// Not stored: set by Load when the file was damaged and defaults were used
    /// (Russian template, translated when shown).
    std::string loadProblem;

    static constexpr std::size_t kMaxRecent = 12;

    /// The largest side, in pixels, a picture on a face keeps at `textureQuality`.
    static int TextureLimit(int textureQuality);

    void AddRecent(const std::filesystem::path& file);
    void RemoveRecent(const std::filesystem::path& file);

    /// Reads settings; missing or broken files give the defaults (a broken
    /// one is copied to "<file>.bak" and `loadProblem` says so).
    static Settings Load(const std::filesystem::path& file);
    bool Save(const std::filesystem::path& file) const;
};

} // namespace bz::app
