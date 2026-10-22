#pragma once

#include "app/AppContext.h"
#include "app/Settings.h"
#include "app/features/OperationEditors.h"
#include "app/mobile/Cursor.h"
#include "app/mobile/TouchInput.h"
#include "app/sketch/SketchEditor.h"
#include "platform/Window.h"
#include "render/Viewport.h"
#include "ui/Fonts.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
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

    /// What woke the window (counted for the redraw report, see ReportRedraws).
    enum class Wake { Cursor, Button, Scroll, Key, Text, Size, Focus, Refresh, Drop, Count };

    /// Filled by the GLFW callbacks between frames.
    struct InputTap {
        bool                       seen = false;   ///< any input: draw frames
        std::array<int, static_cast<std::size_t>(Wake::Count)> wakes{};   ///< callbacks since the last redraw report
        std::vector<ImGuiKeyChord> chords;         ///< key presses with the modifiers held at that moment
        std::vector<std::filesystem::path> dropped;   ///< files dropped on the window, handled between frames
    };

private:
    void Frame();
    /// The active document changed since the last frame: its open operation and sketch picks are dropped.
    void LeaveSwitchedDocument(AppContext& app);
    /// The interface scale: the setting, or one from the screen's density and size.
    float ChooseScale() const;
    /// Rebuilds fonts and sizes when the scale setting changed.
    void ApplyScaleIfChanged();
    void DrawRoot();
    void ApplyTheme(bool force);
    void UpdateTitle();
    void HandleCloseRequests();
    bool NeedsAnotherFrame() const { return WhyAnotherFrame() != nullptr; }
    /// Why the next frame must be drawn even without input; nullptr if it need not.
    const char* WhyAnotherFrame() const;
    /// Counts a frame drawn because of `why` (a string literal).
    void CountRedraw(const char* why);
    /// Every 10 s: how many frames were drawn and why, into the monitoring;
    /// into the log when the window kept redrawing with no user input at all
    /// (it should sleep then: a laptop's battery and fan notice).
    void ReportRedraws(double now);
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
    std::string      myBackgroundApplied;        ///< Settings::background (and picture) in use
    double           mySystemThemeCheck = 0.0;

    Workspace        myWorkspace;
    Session*         myActiveSession = nullptr;   ///< the document the open tools belong to (see Frame)
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
    std::unique_ptr<modules::ModuleManager> myModules;   ///< unloaded first in ~Application
    void StartModules();
    bool myScaledForPhone = false;   ///< the scale was chosen for the phone layout

#if defined(__ANDROID__)
    // Android (docs/ADR/0004-mobile-touch-ui.md); Application_android.cpp.
    void StartAndroid();
    void InstallAndroidClipboard();
    /// Between frames: the system's answers (insets, keyboard text, a picked
    /// file), the keyboard shown or hidden, the background. False when the
    /// system is closing the app.
    bool AndroidBetweenFrames(double now);
    void CheckLastExit();
    void RunTestCommands();
    /// Test command "saveback": a save reaches the document outside the app (smoke-apk.sh).
    void TestSaveBack();
    mobile::TouchInput myTouch;
    int    myScreenRevision = -1;
    bool   myKeyboardUp = false;
    bool   myTestTouchKeys = false;   ///< test command "touchkeys": a keyboard plugged in is ignored (emulator)
    bool   myInBackground = false;
    double myStartedAt = 0.0;
#endif

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
    struct RedrawCount {
        double start = -1.0;                     ///< start of the current 10 s window
        int    frames = 0;
        std::map<std::string_view, int> why;     ///< keys are string literals
        double loggedAt = -1e9;                  ///< last log line (at most one a minute)
    };
    RedrawCount myRedraws;
    std::string myGlStateAfterScene;   ///< what the 3D view left in OpenGL (logged when it changes)
    int         myGlStateLogs = 0;
    bool        myDrawableReclaimLogged = false;

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
