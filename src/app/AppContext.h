#pragma once
// What every screen and panel of the application can reach. Built once per
// frame by Application and passed down by reference: no globals.

#include "app/Actions.h"
#include "app/Autosave.h"
#include "app/Notifications.h"
#include "app/Settings.h"
#include "app/Workspace.h"
#include "app/mobile/MobileState.h"
#include "core/ViewProjection.h"
#include "platform/Touchpad.h"
#include "render/Viewport.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bz::modules {
class ModuleManager;
}

namespace bz::app {

class Recovery;

class SketchEditor;
struct OperationEditors;

enum class LeftTab : unsigned char { Tree, Properties, Variables };
enum class RibbonTab : unsigned char { Model, Sketch, View };

/// UI state of the window chrome that outlives a frame.
struct ShellState {
    LeftTab   leftTab   = LeftTab::Tree;
    RibbonTab ribbonTab = RibbonTab::Model;
    bool      handleHot = false;      ///< a drag handle in the view is under the cursor or held (no radial menu)
    float     ribbonScroll = 0.0f;   ///< sideways scroll of a ribbon wider than the window
    float     ribbonContent = 0.0f;  ///< the ribbon's laid-out width in the last frame
    /// Ribbon groups folding to icons: 0 with captions … 1 icons only, moving
    /// towards Settings::ribbonIconsOnly over half a second (Ribbon.cpp).
    std::unordered_map<std::string, float> ribbonFold;
    std::vector<std::string> ribbonFoldable;   ///< the current tab's folding groups (last frame), for the ribbon's menu
    float     ribbonEdgeX = 0.0f;      ///< where the drag of a group's edge last changed the group
    bool      ribbonEdgeMoved = false; ///< the edge was dragged (not just clicked)
    bool      finishDrag = false;      ///< "Принять эскиз" is being carried to the other end

    bool        paletteOpen = false;   ///< command search (Ctrl+K)
    std::string paletteQuery;
    bool        focusPalette = false;

    std::optional<int> closeRequest;   ///< tab waiting for "save changes?" answer
    bool               quitRequested = false;
    bool               showAbout = false;
    /// The testers' build: "what happened?" — after a crash (at start) or
    /// asked from the menu. Packs the session into a zip to send by hand.
    struct BetaReport {
        bool                  open = false;
        bool                  crash = false;
        std::filesystem::path session;
        std::string           started;
        std::string           text;
        std::filesystem::path packed;   ///< the zip once made
    } betaReport;
    bool               showSettings = false;
    bool               backgroundMenu = false;   ///< open the "Фон" menu at the cursor
    int                settingsTab = 0;          ///< 0 general, 1 controls, 2 graphics, 3 modules
    int                settingsSub = 0;          ///< group inside: controls — keyboard, mouse, touchpad; graphics — look, quality, performance
    std::string        moduleToRemove;           ///< "Модули": the module whose removal waits for "Да, удалить"
    std::string        captureShortcut;          ///< action whose new shortcut is being pressed
    std::string        shortcutFilter;
    std::string        shortcutNote;             ///< "taken from …" after a reassignment
    bool               showMassProperties = false;   ///< the "МЦХ" window
    bool               showFlatPattern = false;      ///< the «Развёртка» window (sheet metal)
    bool               showLibrary = false;          ///< the standard parts window ("Библиотека")
    /// "В библиотеку": the open part saved as an item of the user's library
    /// (SaveToLibrary.cpp). `variables` are the part's user variables, `use`
    /// says which become columns; `cells` holds the rows as typed.
    struct SaveToLibrary {
        bool                                  open = false;
        std::string                           title, group = "Мои изделия";
        std::vector<std::string>              variables;
        std::vector<char>                     use;
        std::vector<std::vector<std::string>> cells;
    } saveToLibrary;
    /// The "Создать" window: the folder shown and the template chosen in it.
    struct NewDocument {
        bool        open = false;
        std::string group;      ///< DocumentTypeInfo::group
        std::string chosen;     ///< template id (NewTemplates)
    } newDocument;
    /// The standard parts window: what it shows, kept while the program runs
    /// (not saved: the recent parts are those of this session only).
    struct Library {
        struct Pick {
            std::string standard, size;   ///< doc::parts ids
            double      length = 0;
            std::vector<std::pair<std::string, double>> inputs{};   ///< typed sizes (a sheet's width), doc::parts::Inputs
        };
        int              family = 0;        ///< filter: 0 ГОСТ, 1 ISO, 2 DIN
        std::set<int>    openGroups;        ///< expanded folders (index in parts::Groups())
        std::set<int>    openKinds;         ///< expanded varieties (parts::PartKind)
        Pick             chosen;            ///< empty standard: nothing chosen
        bool             realThread = false;
        bool             dropWasher = true, dropNut = true;   ///< AutoDrop: add them with the screw
        std::string      appearance;        ///< empty: the default of the kind
        std::vector<Pick> recent;           ///< newest first
        bool             sizeListOpen = false;   ///< the list of sizes is dropped down: Esc closes it, not the window
        std::string      sizeFilter;             ///< typed in that list: only the sizes containing it
        bool             sizeFilterActive = false;   ///< that filter had the keyboard last frame
        std::string      confirmRemove;          ///< a user's item asked to be removed: the second click removes it
        int              phoneGroup = -1;        ///< the phone: the folder shown (index in parts::Groups())
    } library;

    render::DisplayStyle displayStyle = render::DisplayStyle::ShadedWithEdges;

    /// "Разрез вида": the view cut by an origin plane moved through the part.
    struct Section {
        bool  on = false;
        int   plane = 1;           ///< 0 XY, 1 XZ, 2 YZ
        float position = 0.5f;     ///< 0..1 across the part
        bool  flipped = false;     ///< keep the other half
    } section;
    bool measuring = false;   ///< "Измерить" is on
    int  measureSlot = 0;     ///< which of its two fields the next click fills
    bool interference = false;   ///< "Коллизии": overlaps of the bodies shown

    /// Choosing faces with the left button (see viewport/FaceSelection).
    struct FaceSelecting {
        bool   pressed = false;    ///< the left button went down over the scene
        bool   framing = false;    ///< it is dragged: a selection frame
        ImVec2 from{0, 0};         ///< where it went down
    } faceSelecting;

    /// The radial menu (see viewport/MarkingMenu).
    struct MarkingMenu {
        bool   armed = false;   ///< left button pressed over the model, waiting to see if it is held
        bool   open = false;
        bool   held = false;    ///< the press that opened it is still down
        ImVec2 pressAt{0, 0};
        ImVec2 centre{0, 0};
        double pressTime = 0.0;
        double pathLength = 0.0;           ///< how far the cursor went since the press (pixels), to tell a straight pull
        render::CameraState cameraAtPress;  ///< put back when the press turns out to be a flick, not an orbit
    } marking;

    ObjectId    renaming;              ///< tree row being renamed
    std::string renameText;
    bool        renameFocus = false;
    ObjectId    hoveredTreeItem;       ///< for highlighting origin planes in the viewport
    std::string treeFilter;
    std::unordered_set<ObjectId> expanded;   ///< tree rows opened by the user
    /// Assembly tree: the components (or kAssemblyOrigin) whose origin and
    /// work geometry are shown.
    std::unordered_set<ObjectId> elementsOpen;
    /// An origin element or work geometry of a component (or of the assembly)
    /// under the cursor in the tree: shown in the view.
    struct PointedElement {
        ObjectId component, element;
    } hoveredElement;
    ui::EditState edit;                      ///< the text field being edited
    bool        treeSearch = false;
    bool        originExpanded = false;

    std::vector<RecoverableDocument> recoverable;
    bool                             crashedLastTime = false;

    double lastInputTime = 0.0;
    /// Set after an operation changes the model: once the rebuilt model is
    /// shown, zoom out if it no longer fits (KeepModelInView).
    bool   keepModelInView = false;
    /// Planes queued for the 3D view this frame (see DrawPlane).
    std::vector<render::PlaneQuad> planeQuads;

    /// Mouse navigation in the viewport (KOMPAS: right drag orbits,
    /// middle drag pans, wheel zooms; short clicks open a menu / confirm).
    struct Navigation {
        bool                 orbiting = false;
        bool                 panning = false;
        bool                 zooming = false;   ///< zoom by dragging
        int                  button = -1;       ///< the ImGui button that started the gesture
        ImVec2               zoomAt{};          ///< where the drag-zoom started
        bool                 moved = false;
        Vec3                 pivot;
        std::optional<Frame> facePlane;   ///< planar face under the cursor at the right click
        ObjectId             faceBody;    ///< and the body it belongs to
        double               touchAt = -10.0;   ///< when the last touchpad gesture moved the view
        int                  touchKind = -1;    ///< what it did (another kind is a new gesture: new pivot)
    } nav;
    bool           animateUi = false;           ///< something in the interface moves by itself this frame
    const Session* viewportSession = nullptr;   ///< whose camera the viewport shows
    ImVec2         viewportMax{0, 0};           ///< bottom-right of the scene, for notifications
    mobile::MobileState mobile;                 ///< phone layout and touch (Android); unused on the desktop
};

/// The viewport rectangle and camera of the current frame.
struct ViewportFrame {
    ImVec2         min;          ///< top-left, screen coordinates
    ImVec2         max;
    ViewProjection projection;
    bool           hovered = false;   ///< mouse over the scene, not over overlays
    float          promptTop = 0;     ///< where prompts over the view start (under the phone's islands); 0: min.y + 14

    Vec2 ToLocal(ImVec2 screen) const { return {screen.x - min.x, screen.y - min.y}; }
    ImVec2 ToScreen(Vec2 local) const { return {static_cast<float>(local.x) + min.x, static_cast<float>(local.y) + min.y}; }
    ImVec2 Project(Vec3 world) const { return ToScreen(projection.Project(world)); }
};

/// Where the time of a frame goes (milliseconds, smoothed), for the stats
/// tooltip: a slow machine report can then say what is slow.
struct FrameStats {
    float  fps = 0.0f;
    double memoryMb = 0.0;
    double hoverMs = 0.0;     ///< what is under the cursor in the model
    double uiMs = 0.0;        ///< building the interface (hover included)
    double sceneMs = 0.0;     ///< drawing the model
    double presentMs = 0.0;   ///< drawing the interface and showing the frame (waits for the screen)
    int    sceneFrames = 0;   ///< model redraws in the last second
};

struct AppContext {
    ui::Ui&           ui;
    Settings&         settings;
    Workspace&        workspace;
    ActionRegistry&   actions;
    Notifications&    notes;
    Autosave&         autosave;
    render::Viewport& viewport;
    SketchEditor&     sketch;
    OperationEditors& ops;
    ShellState&       shell;
    std::vector<std::function<void()>>& deferred;
    double            now = 0.0;
    FrameStats        stats;
    std::vector<ImGuiKeyChord> pressedChords;    ///< key chords pressed this frame (for shortcut capture)
    std::vector<platform::TouchEvent> touch;     ///< touchpad gestures of this frame
    modules::ModuleManager*    modules = nullptr; ///< set once the modules are loaded
    /// Puts the documents back when a piece of work fails (app/Recovery.h); null in tests.
    Recovery*                  recovery = nullptr;

    Session* Doc() { return workspace.Active(); }

    /// Runs `f` after the frame, when no panel is iterating the document.
    void Later(std::function<void()> f) { deferred.push_back(std::move(f)); }
};

} // namespace bz::app
