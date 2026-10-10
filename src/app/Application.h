#pragma once

#include "app/AppContext.h"
#include "app/Settings.h"
#include "app/features/OperationEditors.h"
#include "app/sketch/SketchEditor.h"
#include "platform/Window.h"
#include "render/Viewport.h"
#include "ui/Fonts.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace bz::app {

/// The desktop application: owns the window, the UI system and the open
/// documents, and runs the frame loop. Redraws only when something changes.
class Application {
public:
    Application();
    ~Application();

    /// Sets everything up. `files` are documents to open (command line).
    bool Init(const std::vector<std::filesystem::path>& files);
    int  Run();

    /// Test hook: save a screenshot of frame `frame` to `file`, then quit.
    void ScreenshotAndQuit(std::filesystem::path file, int frame) {
        myScreenshotFile = std::move(file);
        myScreenshotFrame = frame;
    }
    /// Test hook for driving the app from outside (xdotool): while running,
    /// a file `request` appearing in `dir` makes the next frame save itself as
    /// `dir/<file content>.ppm`. Enabled by the BZ_SHOT_DIR variable.
    void EnableShotRequests(std::filesystem::path dir) { myShotDir = std::move(dir); }
    AppContext& Context() { return *myContext; }

    /// Filled by the GLFW callbacks between frames.
    struct InputTap {
        bool                       seen = false;   ///< any input: draw frames
        std::vector<ImGuiKeyChord> chords;         ///< key presses with the modifiers held at that moment
    };

private:
    void Frame();
    /// The interface scale: the setting, or one from the screen's density and size.
    float ChooseScale() const;
    /// Rebuilds fonts and sizes when the scale setting changed.
    void ApplyScaleIfChanged();
    void DrawRoot();
    void ApplyTheme(bool force);
    void UpdateTitle();
    void HandleCloseRequests();
    bool NeedsAnotherFrame() const;
    /// A document rebuilds on another thread: frames at a calm pace.
    bool WaitsForBackground() const;
    double myMemoryReadAt = -10.0;   ///< when the memory figure was last read
    void TakeScreenshot(const std::filesystem::path& file) const;
    void PollShotRequest();

    platform::Window myWindow;
    Settings         mySettings;
    ui::Theme        myTheme;
    ui::Fonts        myFonts;
    ui::Icons        myIcons;
    float            myScale = 1.0f;
    bool             myDarkApplied = false;
    int              myBackgroundApplied = -1;   ///< Settings::viewBackground in use
    double           mySystemThemeCheck = 0.0;

    Workspace        myWorkspace;
    ActionRegistry   myActions;
    Notifications    myNotes;
    std::unique_ptr<Autosave> myAutosave;
    render::Viewport myViewport;
    SketchEditor     mySketch;
    OperationEditors myOps;
    ShellState       myShell;
    std::vector<std::function<void()>> myDeferred;

    std::unique_ptr<ui::Ui>     myUi;
    std::unique_ptr<AppContext> myContext;

    InputTap myInput;
    int    myFramesToDraw = 3;
    double myLastFrameTime = 0.0;
    float  myFps = 0.0f;
    float  myFramebufferScale = 1.0f;
    double myAppliedScaleSetting = 0.0;
    std::filesystem::path myAssets;
    FrameStats myStats;
    int    mySceneFramesThisSecond = 0;
    double mySecondStart = 0.0;
    bool   myBusy = false;   ///< something animates: keep drawing

    std::filesystem::path myScreenshotFile;
    int                   myScreenshotFrame = -1;
    int                   myFrameIndex = 0;
    std::filesystem::path myShotDir;        ///< see EnableShotRequests
    std::filesystem::path myPendingShot;    ///< file for the next frame, if requested
    std::string           myTitle;
};

/// All user commands (ribbon, menus, search, shortcuts). AppActions.cpp.
void RegisterActions(AppContext& app);

} // namespace bz::app
