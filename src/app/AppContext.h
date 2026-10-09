#pragma once
// What every screen and panel of the application can reach. Built once per
// frame by Application and passed down by reference: no globals.

#include "app/Actions.h"
#include "app/Autosave.h"
#include "app/Notifications.h"
#include "app/Settings.h"
#include "app/Workspace.h"
#include "core/ViewProjection.h"
#include "render/Viewport.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"

#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace bz::app {

class SketchEditor;
struct OperationEditors;

enum class LeftTab : unsigned char { Tree, Properties, Variables };
enum class RibbonTab : unsigned char { Model, Sketch, View };

/// UI state of the window chrome that outlives a frame.
struct ShellState {
    LeftTab   leftTab   = LeftTab::Tree;
    RibbonTab ribbonTab = RibbonTab::Model;

    bool        paletteOpen = false;   ///< command search (Ctrl+K)
    std::string paletteQuery;
    bool        focusPalette = false;

    std::optional<int> closeRequest;   ///< tab waiting for "save changes?" answer
    bool               quitRequested = false;
    bool               showAbout = false;
    bool               showMassProperties = false;   ///< the "МЦХ" window

    render::DisplayStyle displayStyle = render::DisplayStyle::ShadedWithEdges;

    /// "Разрез вида": the view cut by an origin plane moved through the part.
    struct Section {
        bool  on = false;
        int   plane = 1;           ///< 0 XY, 1 XZ, 2 YZ
        float position = 0.5f;     ///< 0..1 across the part
        bool  flipped = false;     ///< keep the other half
    } section;
    bool measuring = false;   ///< "Измерить" is on

    /// The radial menu (see viewport/MarkingMenu).
    struct MarkingMenu {
        bool   armed = false;   ///< left button pressed over the model, waiting to see if it is held
        bool   open = false;
        bool   held = false;    ///< the press that opened it is still down
        ImVec2 pressAt{0, 0};
        ImVec2 centre{0, 0};
        double pressTime = 0.0;
    } marking;

    ObjectId    renaming;              ///< tree row being renamed
    std::string renameText;
    bool        renameFocus = false;
    ObjectId    hoveredTreeItem;       ///< for highlighting origin planes in the viewport
    std::string treeFilter;
    std::unordered_set<ObjectId> expanded;   ///< tree rows opened by the user
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
        bool                 moved = false;
        Vec3                 pivot;
        std::optional<Frame> facePlane;   ///< planar face under the cursor at the right click
        ObjectId             faceBody;    ///< and the body it belongs to
    } nav;
    const Session* viewportSession = nullptr;   ///< whose camera the viewport shows
    ImVec2         viewportMax{0, 0};           ///< bottom-right of the scene, for notifications
};

/// The viewport rectangle and camera of the current frame.
struct ViewportFrame {
    ImVec2         min;          ///< top-left, screen coordinates
    ImVec2         max;
    ViewProjection projection;
    bool           hovered = false;   ///< mouse over the scene, not over overlays

    Vec2 ToLocal(ImVec2 screen) const { return {screen.x - min.x, screen.y - min.y}; }
    ImVec2 ToScreen(Vec2 local) const { return {static_cast<float>(local.x) + min.x, static_cast<float>(local.y) + min.y}; }
    ImVec2 Project(Vec3 world) const { return ToScreen(projection.Project(world)); }
};

struct FrameStats {
    float  fps = 0.0f;
    double memoryMb = 0.0;
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

    Session* Doc() { return workspace.Active(); }

    /// Runs `f` after the frame, when no panel is iterating the document.
    void Later(std::function<void()> f) { deferred.push_back(std::move(f)); }
};

} // namespace bz::app
