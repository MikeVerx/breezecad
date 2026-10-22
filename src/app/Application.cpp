#include "app/Application.h"
#include "app/BetaTesting.h"
#include "app/CrashGuard.h"

#include "app/Language.h"
#include "core/Monitor.h"
#include "app/Operations.h"
#include "app/shell/Shell.h"
#include "app/shell/Splash.h"
#include "app/Backgrounds.h"
#include "modules/ModuleHost.h"
#include "modules/ModuleManager.h"
#include "app/scripting/Scripting.h"
#include "core/Image.h"
#include "app/viewport/ViewportPanel.h"
#include "core/Log.h"
#include "core/Version.h"
#include "core/Profile.h"
#include "core/PathText.h"
#include "kernel/TextGeometry.h"
#include "platform/Paths.h"
#include "platform/ProcessInfo.h"
#include "platform/SystemTheme.h"

#include "app/mobile/MobileLayout.h"
#include "ui/Gl.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <lunasvg.h>
#if defined(__ANDROID__)
#include "app/android/AndroidInput.h"
#include "platform/Android.h"
#else
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <thread>

#if !defined(__ANDROID__)
// Defined (non-static) in imgui_impl_glfw.cpp but not declared in its header.
ImGuiKey ImGui_ImplGlfw_KeyToImGuiKey(int keycode, int scancode);
#endif

namespace bz::app {
namespace {

/// Test hook (BZ_FAULT_TEST): "crash" — an access violation, "hang" — the
/// window's loop stops for 20 s. Tries the crash and hang windows out
/// (app/CrashGuard.h) on a real build; CI takes pictures of them.
void FaultTest(std::string_view kind) {
    if (kind == "crash") {
        // A null pointer the compiler cannot see as one (5 letters - 5).
        volatile int* nowhere = reinterpret_cast<int*>(static_cast<std::uintptr_t>(kind.size() - 5));
        *nowhere = 1;
    } else if (kind == "hang") {
        std::this_thread::sleep_for(std::chrono::seconds(20));
    }
}

constexpr GLenum kGlFramebufferSrgb = 0x8DB9;   // GL_FRAMEBUFFER_SRGB (OpenGL 3.0, absent from GL 1.1 headers)

#if !defined(__ANDROID__)
/// Input callbacks installed before ImGui's, which chain to them.
Application::InputTap* TapOf(GLFWwindow* window) {
    return static_cast<Application::InputTap*>(glfwGetWindowUserPointer(window));
}

/// Marks the window as needing frames after any input.
void OnAnyInput(GLFWwindow* window, Application::Wake wake) {
    if (auto* tap = TapOf(window)) {
        tap->seen = true;
        ++tap->wakes[static_cast<std::size_t>(wake)];
    }
}

/// Records a key press together with the modifiers held at that instant.
/// ImGui sees key events spread over frames, so a quick Ctrl+S on a slow
/// frame may reach it with Ctrl already released; this never loses it.
void OnKey(GLFWwindow* window, int key, int scancode, int action, int mods) {
    OnAnyInput(window, Application::Wake::Key);
    if (action != GLFW_PRESS) return;
    ImGuiKey imguiKey = ImGui_ImplGlfw_KeyToImGuiKey(key, scancode);
    if (imguiKey == ImGuiKey_None) return;
    if (imguiKey == ImGuiKey_KeypadEnter) imguiKey = ImGuiKey_Enter;   // one Enter for shortcuts, whichever was pressed
#if defined(__APPLE__)
    const int ctrl = GLFW_MOD_SUPER;   // Cmd plays the role of Ctrl, as in ImGui
#else
    const int ctrl = GLFW_MOD_CONTROL;
#endif
    ImGuiKeyChord chord = imguiKey;
    if (mods & ctrl) chord |= ImGuiMod_Ctrl;
    if (mods & GLFW_MOD_SHIFT) chord |= ImGuiMod_Shift;
    if (mods & GLFW_MOD_ALT) chord |= ImGuiMod_Alt;
    if (auto* tap = TapOf(window)) tap->chords.push_back(chord);
}
#endif

constexpr const char* kWakeNames[] = {"cursor", "button", "scroll", "key", "text", "size", "focus", "refresh", "drop"};
static_assert(std::size(kWakeNames) == static_cast<std::size_t>(Application::Wake::Count));

} // namespace

Application::Application() = default;

Application::~Application() {
    platform::StopTouchpad();   // before ImGui puts its window procedure back (Windows)
    myModules.reset();   // modules shut down while everything they may touch is still there
    modules::SetCommandHandler({});
    if (myUi) {
        ImGui_ImplOpenGL3_Shutdown();
#if !defined(__ANDROID__)
        ImGui_ImplGlfw_Shutdown();
#endif
        ImGui::DestroyContext();
    }
    std::error_code ec;
    std::filesystem::remove(platform::ConfigDir() / "running.lock", ec);
}

bool Application::Init(const std::vector<std::filesystem::path>& files) {
    const std::filesystem::path config = platform::ConfigDir();
    // The testers' build logs into its session folder (packed into reports).
    log::OpenFile(beta::SessionDir().empty() ? config / "logs" / "breezecad.log" : beta::SessionDir() / "output.log");
    log::Info("BreezeCAD {} starting", VersionLine());

    // A lock file left from the last run means it did not exit normally.
    myShell.crashedLastTime = std::filesystem::exists(config / "running.lock");
    std::ofstream(config / "running.lock") << "running";

    mySettings = Settings::Load(config / "settings.json");
    if (!mySettings.loadProblem.empty()) myNotes.Error(Tr(mySettings.loadProblem));
    if (const std::string problem = ApplyLanguage(mySettings); !problem.empty())
        myNotes.Error(Tr(problem));
    // The splash while the rest starts; the main window appears when ready.
    const std::filesystem::path assets = platform::AssetsDir();
    Splash splash;
    const bool dark = mySettings.theme == ThemeChoice::Dark ||
                      (mySettings.theme == ThemeChoice::System && platform::SystemPrefersDark());
    // After a run that did not end normally, no splash: should the splash
    // itself be what failed, the next start still works.
    const bool splashShown = !myShell.crashedLastTime && splash.Open(assets, dark);
    const int steps = 4 + static_cast<int>(files.size());
    int step = 0;
    const auto progress = [&](const std::string& status) { splash.Step(status, static_cast<float>(++step) / static_cast<float>(steps)); };

    platform::WindowConfig windowConfig;
    windowConfig.visible = !splashShown;
    if (mySettings.windowW > 0 && mySettings.windowH > 0) {
        windowConfig.width = mySettings.windowW;
        windowConfig.height = mySettings.windowH;
        windowConfig.x = mySettings.windowX;
        windowConfig.y = mySettings.windowY;
    }
    windowConfig.maximized = mySettings.windowMaximized;
    progress(Tr("Создание окна…"));
    if (!myWindow.Create(windowConfig)) return false;
    beta::WriteSystemInfo(myWindow, mySettings.language.empty() ? std::string("system") : mySettings.language);
    crash::NoteGraphics(myWindow.OpenGLRenderer() + ", OpenGL " + myWindow.OpenGLVersion());

#if defined(__ANDROID__)
    StartAndroid();
#else
    glfwSetWindowUserPointer(myWindow.Handle(), &myInput);
    glfwSetCursorPosCallback(myWindow.Handle(), [](GLFWwindow* w, double, double) { OnAnyInput(w, Wake::Cursor); });
    glfwSetMouseButtonCallback(myWindow.Handle(), [](GLFWwindow* w, int, int, int) { OnAnyInput(w, Wake::Button); });
    glfwSetScrollCallback(myWindow.Handle(), [](GLFWwindow* w, double, double) { OnAnyInput(w, Wake::Scroll); });
    glfwSetKeyCallback(myWindow.Handle(), OnKey);
    glfwSetCharCallback(myWindow.Handle(), [](GLFWwindow* w, unsigned) { OnAnyInput(w, Wake::Text); });
    glfwSetWindowSizeCallback(myWindow.Handle(), [](GLFWwindow* w, int, int) { OnAnyInput(w, Wake::Size); });
    glfwSetWindowFocusCallback(myWindow.Handle(), [](GLFWwindow* w, int) { OnAnyInput(w, Wake::Focus); });
    glfwSetWindowRefreshCallback(myWindow.Handle(), [](GLFWwindow* w) { OnAnyInput(w, Wake::Refresh); });
    glfwSetDropCallback(myWindow.Handle(), [](GLFWwindow* w, int count, const char** paths) {   // UTF-8 paths
        auto* tap = TapOf(w);
        if (!tap) return;
        for (int i = 0; i < count; ++i) {
            const std::string_view p(paths[i]);
            tap->dropped.emplace_back(std::u8string(p.begin(), p.end()));
        }
        OnAnyInput(w, Wake::Drop);
    });
#endif

    kernel::SetTextFont(assets / "fonts" / "IBMPlexSans-Medium.ttf", assets / "fonts" / "IBMPlexSans-SemiBold.ttf");   // sketch text
    log::Info("Assets: {}", ToUtf8(assets));

    // Window icon from the brand SVG.
    if (auto icon = lunasvg::Document::loadFromFile(ToUtf8(assets / "brand" / "breezecad-app-icon.svg"))) {
        lunasvg::Bitmap bitmap = icon->renderToBitmap(128, 128);
        bitmap.convertToRGBA();
        myWindow.SetIcon(bitmap.data(), 128);
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // layout is fixed by the design, nothing to remember
    io.ConfigWindowsMoveFromTitleBarOnly = true;
#if defined(NDEBUG)
    // Dear ImGui's red "conflicting ID" window is for us: on in debug builds and
    // the kept previews (preview/android-N), off where testers and users see it
    // (BZ_IMGUI_IDS=1 turns it on anywhere).
    io.ConfigDebugHighlightIdConflicts = std::string_view(Branch()).starts_with("preview/") || std::getenv("BZ_IMGUI_IDS") != nullptr;
#endif
#if defined(__ANDROID__)
    // No platform backend: input comes through mobile::TouchInput and
    // android/AndroidInput; the size and time are set every frame.
    io.BackendPlatformName = "breezecad-android";
    InstallAndroidClipboard();
    ImGui_ImplOpenGL3_Init("#version 300 es");
#else
    ImGui_ImplGlfw_InitForOpenGL(myWindow.Handle(), true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    platform::StartTouchpad(myWindow.Handle());   // after ImGui: it hooks the window too (Windows)
#endif

    // HiDPI: on macOS the framebuffer is 2x the window; elsewhere the UI scales.
    int winW = 1, fbW = 1, h = 1;
    myWindow.WindowSize(winW, h);
    myWindow.FramebufferSize(fbW, h);
    const float framebufferScale = winW > 0 ? static_cast<float>(fbW) / static_cast<float>(winW) : 1.0f;
    myFramebufferScale = framebufferScale;
    myAssets = assets;
    myScale = ChooseScale();
    myAppliedScaleSetting = mySettings.uiScale;
    progress(Tr("Шрифты и значки…"));
    myFonts.Load(assets, myScale * framebufferScale);
    io.FontGlobalScale = 1.0f / framebufferScale;
    myIcons.SetDirectory(assets / "icons");
    myIcons.SetScale(framebufferScale);

    progress(Tr("Загрузка ядра геометрии…"));
    if (!myViewport.Init(myWindow.NativeSurface())) return false;

    myUi = std::make_unique<ui::Ui>(ui::Ui{myTheme, myFonts, myIcons, myScale});
    ApplyTheme(true);

    myAutosave = std::make_unique<Autosave>(platform::AutosaveDir());
    myShell.recoverable = myAutosave->FindRecoverable();

    myContext = std::make_unique<AppContext>(AppContext{*myUi, mySettings, myWorkspace, myActions, myNotes, *myAutosave,
                                                        myViewport, mySketch, myOps, myShell, myDeferred, 0.0, {}, {}, {}});
    RegisterActions(*myContext);
    LoadStandardPacks(*myContext);
    StartModules();
    for (const auto& [id, chord] : mySettings.shortcuts) myActions.SetChord(id, ActionRegistry::ParseChord(chord));

    for (const auto& file : files) {
        progress(Trf("Открытие {}…", ToUtf8(file.filename())));
        myWorkspace.Open(file, myNotes, mySettings);
    }
    // Nothing asked for: the last file, if the user wants it (and it is still there).
    if (files.empty() && mySettings.reopenLast && !mySettings.recentFiles.empty()) {
        std::error_code ec;
        const std::filesystem::path last = mySettings.recentFiles.front();
        if (std::filesystem::exists(last, ec)) myWorkspace.Open(last, myNotes, mySettings);
    }
    progress(Tr("Готово"));
    splash.Close();
    myWindow.Show();
    // The testers' build: a crash last time — ask what happened (newest first).
    if (const std::vector<beta::PastSession> crashes = beta::UnreportedCrashes(); !crashes.empty()) {
        for (std::size_t i = 1; i < crashes.size(); ++i) beta::Dismiss(crashes[i].dir);   // only the last one is asked about
        myShell.betaReport = {true, true, crashes.front().dir, crashes.front().started, {}, {}};
    }
#if defined(__ANDROID__)
    CheckLastExit();
    RunTestCommands();
#endif
    if (myShell.crashedLastTime && !myShell.recoverable.empty())
        myNotes.Info(Tr("Прошлый сеанс завершился неожиданно. Несохранённую работу можно восстановить на главной странице."));
    // Test hook (screenshots of the settings in CI): BZ_SETTINGS_TAB=<tab>[.<group>],
    // e.g. 1.2 for "Управление" → "Тачпад".
    if (const char* tab = std::getenv("BZ_SETTINGS_TAB")) {
        myShell.showSettings = true;
        myShell.settingsTab = std::clamp(std::atoi(tab), 0, 3);
        if (const char* dot = std::strchr(tab, '.')) myShell.settingsSub = std::clamp(std::atoi(dot + 1), 0, 2);
    }
    return true;
}

void Application::ApplyTheme(bool force) {
    const bool dark = mySettings.theme == ThemeChoice::Dark ||
                      (mySettings.theme == ThemeChoice::System && platform::SystemPrefersDark());
    const std::string background = mySettings.background + "|" + mySettings.backgroundPicture;
    if (!force && dark == myDarkApplied && background == myBackgroundApplied) return;
    myDarkApplied = dark;
    const bool pictureChanged = background != myBackgroundApplied;
    myBackgroundApplied = background;
    myTheme = dark ? ui::Theme::Dark() : ui::Theme::Light();
    myIcons.SetDark(dark);
    ui::ApplyToImGui(myTheme, myScale);
    const ui::ThemeColors& c = myTheme.colors;
    // The view background: a preset (under a picture, if there is one).
    const BackgroundColours bg = ColoursOf(FindBackground(mySettings.background), c);
    Color top = bg.top, bottom = bg.bottom;
    // Test hook: a flat background of a given colour (renders keyed out
    // against black and white for the splash picture, scripts/splash).
    if (const char* bg = std::getenv("BZ_VIEW_BG")) top = bottom = Color::Hex(static_cast<std::uint32_t>(std::strtoul(bg, nullptr, 16)));
    myViewport.SetColors({top, bottom, c.body, c.bodyEdges, c.hoverHighlight, c.selection, c.danger, bg.radial});
    if (pictureChanged) {
        if (mySettings.background == kPictureBackground && !mySettings.backgroundPicture.empty()) {
            if (const auto image = ReadImageFile(PathFromUtf8(mySettings.backgroundPicture))) {
                myViewport.SetBackgroundPicture(image->width, image->height, image->rgba);
            } else {
                myNotes.Error(Tr(image.GetError().message));
                myViewport.SetBackgroundPicture(0, 0, {});
            }
        } else {
            myViewport.SetBackgroundPicture(0, 0, {});
        }
    }
    myFramesToDraw = 3;
}

int Application::Run() {
    // Test hook (CI): BZ_TEST_NEW_PART=<seconds> opens a new part that long
    // after the start, so pictures of the screen before and after can show
    // that the window still updates once a document is open.
    const char* newPartSetting = std::getenv("BZ_TEST_NEW_PART");
    double newPartAt = newPartSetting ? std::atof(newPartSetting) : -1.0;
    const double runStart = platform::Window::Time();

    // A frozen loop for this long gets the "not responding" window (a
    // rebuild runs on its own thread; this is the window itself stuck).
    // BZ_HANG_SECONDS changes it, to try the window out.
    const char* hangSetting = std::getenv("BZ_HANG_SECONDS");
#if defined(__ANDROID__)
    // Sooner than the watchdog's notice (10 s): its error screen finds the report.
    crash::StartHangGuard(8.0);
#else
    crash::StartHangGuard(hangSetting ? std::max(2.0, std::atof(hangSetting)) : 15.0);
#endif
    const char* faultTest = std::getenv("BZ_FAULT_TEST");
    while (true) {
        mon::Beat();
        if (NeedsAnotherFrame()) {
            platform::Window::PollEvents();
        } else {
            // Wakes up for autosave and theme checks; often enough to show
            // a rebuild running on another thread.
            // A message on screen wakes the loop when it starts to fade out.
            const double wait = std::min(WaitsForBackground() ? 1.0 / 30.0 : 1.0,
                                         myNotes.SecondsToNextChange(platform::Window::Time()) + 0.001);
            platform::Window::WaitEvents(wait);
        }
        const double now = platform::Window::Time();
#if defined(__ANDROID__)
        if (!AndroidBetweenFrames(now)) break;   // the system is closing the app: the autosave keeps the work
#endif
        platform::SetSmoothScrollIsTouchpad(mySettings.touch.smoothIsTouchpad);
        myContext->touch = platform::TakeTouchEvents();
        if (!myContext->touch.empty()) myInput.seen = true;
        if (myInput.seen) {
            myInput.seen = false;
            myFramesToDraw = std::max(myFramesToDraw, 3);
            myShell.lastInputTime = now;
        }

        // The window close button: ask about unsaved documents first.
        if (myWindow.ShouldClose()) {
            myWindow.SetShouldClose(false);
            // Logged: a desktop closing the window on its own looks like a
            // crash to the user, and only this line tells them apart.
            log::Info("Quit: the window system asked to close the window (its close button or the desktop)");
            mon::Action("window", "close requested by the system");
            myShell.quitRequested = true;
            myFramesToDraw = std::max(myFramesToDraw, 3);
        }
        PollShotRequest();
        if (!myInput.dropped.empty()) {   // a module package or documents dragged in
            const std::vector<std::filesystem::path> files = std::move(myInput.dropped);
            myInput.dropped.clear();
            for (const std::filesystem::path& f : files) OpenDropped(*myContext, f);
        }
        if (myModules && myModules->Tick()) myFramesToDraw = std::max(myFramesToDraw, 3);   // show what they changed
        HandleCloseRequests();
        if (myShell.quitRequested && myWorkspace.sessions.empty()) {
            log::Info("Quit: no documents left to ask about");
            break;
        }

        if (mySettings.background + "|" + mySettings.backgroundPicture != myBackgroundApplied) ApplyTheme(true);   // chosen: at once
        if (now - mySystemThemeCheck > 2.0) {
            mySystemThemeCheck = now;
            ApplyTheme(false);
        }
        for (auto& s : myWorkspace.sessions)
            if (myAutosave->Update(*s, mySettings, now, now - myShell.lastInputTime)) myFramesToDraw = std::max(myFramesToDraw, 1);

        const char* why = myFramesToDraw > 0 ? "input or a change" : WhyAnotherFrame();
        if (!why && WaitsForBackground()) why = "background rebuild";
        if (why && !myWindow.CanDraw()) why = nullptr;   // Android in the background: no surface to draw into
        if (why) {
            CountRedraw(why);
            Frame();
            if (myFramesToDraw > 0) --myFramesToDraw;
        }
        ReportRedraws(now);
        if (myScreenshotFrame >= 0 && myFrameIndex > myScreenshotFrame) break;
        if (newPartAt >= 0 && now - runStart >= newPartAt) {
            newPartAt = -1.0;
            NewDocument(*myContext);
            myFramesToDraw = std::max(myFramesToDraw, 3);
        }
        if (faultTest && myFrameIndex == 3) {
            FaultTest(faultTest);
            faultTest = nullptr;
        }
    }
    crash::StopHangGuard();
    myWindow.Geometry(mySettings.windowX, mySettings.windowY, mySettings.windowW, mySettings.windowH, mySettings.windowMaximized);
    mySettings.Save(platform::ConfigDir() / "settings.json");
    log::Info("BreezeCAD exits normally");
    return 0;
}

/// Modules from the folder next to the program (shipped) and from the user's
/// config folder (installed by the user); see src/modules/ModuleManager.h.
void Application::StartModules() {
    const std::filesystem::path config = platform::ConfigDir();
    myModules = std::make_unique<modules::ModuleManager>(config / "modules.json", modules::HostApi());
    // Modules' own threads wake the loop up; their commands run here, between frames.
    modules::SetWakeHandler(&platform::Window::PostEmptyEvent);
    const std::u8string generic = config.generic_u8string();
    modules::SetConfigDir(std::string(generic.begin(), generic.end()));
    modules::SetCommandHandler([this](std::string_view request) { return scripting::Run(*myContext, request); });
    modules::HostInfo host = modules::HostInfo::Detect();
    host.openGL            = myWindow.OpenGLVersion();
    myModules->Discover(platform::AssetsDir().parent_path() / "modules", host);
    myModules->SetInstallDir(config / "modules");
    myModules->Discover(config / "modules", host);
    myModules->LoadAll();
    myContext->modules = myModules.get();
    if (const std::string& id = myModules->CrashedLastRun(); !id.empty()) {
        const modules::ModuleEntry* m = myModules->Find(id);
        myNotes.Error(Trf("Модуль «{}» аварийно закрыл BreezeCAD в прошлый раз и отключён. Включить снова: Настройки → Модули.",
                          m ? m->manifest.name : id));
    }
}

const char* Application::WhyAnotherFrame() const {
    if (myBusy) return "animation";
    if (myNotes.Animating(platform::Window::Time())) return "animation";
    // Only a 3D view on screen needs drawing: on the home page nothing renders
    // it, so "needs render" stayed true and the window redrew ~130 times a
    // second doing nothing (testers' logs, 2026-10).
    if (myViewport.NeedsRender() && myWorkspace.ActiveIndex() >= 0 && !myWorkspace.ShowsHome()) return "viewport";
    if (!myDeferred.empty()) return "deferred work";
    for (const auto& s : myWorkspace.sessions)
        if (s->Rebuilding() && !s->RebuildingInBackground()) return "rebuild";   // the next slice of a rebuild
    if (myScreenshotFrame >= 0) return "screenshot";
    if (myShell.mobile.framesWanted > 0) return "touch";
    const ImGuiIO& io = ImGui::GetIO();
    for (bool down : io.MouseDown)
        if (down) return "mouse button held";
    return nullptr;
}

void Application::CountRedraw(const char* why) {
    ++myRedraws.frames;
    ++myRedraws.why[why];
}

void Application::ReportRedraws(double now) {
    constexpr double kWindow = 10.0;   // s
    if (myRedraws.start < 0) myRedraws.start = now;
    if (now - myRedraws.start < kWindow) return;
    const int frames = myRedraws.frames;
    int userInput = 0;   // what a person does; size, focus and refresh come from the system
    for (const Wake w : {Wake::Cursor, Wake::Button, Wake::Scroll, Wake::Key, Wake::Text, Wake::Drop})
        userInput += myInput.wakes[static_cast<std::size_t>(w)];
    if (frames > 0) {
        std::string text;
        for (const auto& [why, n] : myRedraws.why) text += std::format("{}{} {}", text.empty() ? "" : ", ", why, n);
        text += "; window events:";
        for (std::size_t i = 0; i < myInput.wakes.size(); ++i)
            if (myInput.wakes[i] > 0) text += std::format(" {} {}", kWakeNames[i], myInput.wakes[i]);
        mon::Redraws(frames, now - myRedraws.start, text);
        // A sleeping window draws nothing; 10 frames a second with nobody
        // touching it means something keeps waking it up.
        if (userInput == 0 && frames >= 100 && now - myRedraws.loggedAt >= 60.0) {
            myRedraws.loggedAt = now;
            log::Info("Redraw without input: {} frames in {:.0f} s ({})", frames, now - myRedraws.start, text);
        }
    }
    myRedraws.start = now;
    myRedraws.frames = 0;
    myRedraws.why.clear();
    myInput.wakes.fill(0);
}

bool Application::WaitsForBackground() const {
    // Only the document on screen: another one takes its result when shown.
    const Session* s = myWorkspace.Active();
    return s && s->RebuildingInBackground();
}

void Application::HandleCloseRequests() {
    if (!myShell.quitRequested || myShell.closeRequest) return;
    for (std::size_t i = 0; i < myWorkspace.sessions.size(); ++i) {
        if (myWorkspace.sessions[i]->IsModified()) {
            myWorkspace.Activate(static_cast<int>(i));
            myShell.closeRequest = static_cast<int>(i);
            return;
        }
    }
    // Nothing left to ask about: close everything.
    while (!myWorkspace.sessions.empty()) {
        myAutosave->Discard(*myWorkspace.sessions.back());
        myWorkspace.Close(static_cast<int>(myWorkspace.sessions.size()) - 1);
    }
}

void Application::UpdateTitle() {
    // The testers' build says so in the title (and in screenshots of it).
    const std::string program = beta::Enabled() ? std::string("BreezeCAD ") + Version() + " \xE2\x80\x94 " + Tr("для бета-тестеров") : "BreezeCAD";
    std::string title = program;
    if (Session* s = myWorkspace.Active(); s && !myWorkspace.ShowsHome()) {
        // A part edited in place: "Узел › Вал", modified with its assembly.
        const Session* tab = s->host ? s->host : s;
        const std::string name = s->host ? tab->Title() + " \xE2\x80\xBA " + s->Title() : s->Title();
        title = name + (tab->IsModified() ? " \xE2\x80\xA2" : "") + " \xE2\x80\x94 " + program;
    }
    if (title != myTitle) {
        myTitle = title;
        myWindow.SetTitle(title);
    }
}

float Application::ChooseScale() const {
#if defined(__ANDROID__)
    // Coordinates are dp already. The phone layout is sized for the finger
    // at 1.0; a tablet's desktop layout a little bigger than a monitor's.
    const float chosen = mySettings.uiScale > 0 ? static_cast<float>(mySettings.uiScale) : 0.0f;
    // Not below 1: the main buttons stay ≈7 mm (44 dp) for a finger (ADR-0004).
    if (myShell.mobile.phone) return chosen > 0 ? std::clamp(chosen, 1.0f, 1.6f) : 1.0f;
    return chosen > 0 ? chosen : 1.25f;
#endif
    if (mySettings.uiScale > 0) return static_cast<float>(mySettings.uiScale);
    // The screen's density, then small enough that the layout (about
    // 1280 x 720 design pixels) fits a small screen such as 1366 x 768.
    float scale = myWindow.ContentScale() / myFramebufferScale;
    int x = 0, y = 0, w = 0, h = 0;
    myWindow.MonitorWorkArea(x, y, w, h);
    if (w > 0 && h > 0) scale = std::min({scale, static_cast<float>(w) / 1280.0f, static_cast<float>(h) / 700.0f});
    return std::clamp(scale, 0.75f, 3.0f);
}

void Application::ApplyScaleIfChanged() {
    // The window moved to a screen of another density (a Retina laptop and an
    // external monitor on macOS): text and icons are rasterised for its pixels.
    int winW = 0, fbW = 0, h = 0;
    myWindow.WindowSize(winW, h);
    myWindow.FramebufferSize(fbW, h);
    const float density = winW > 0 && fbW > 0 ? static_cast<float>(fbW) / static_cast<float>(winW) : myFramebufferScale;
    const bool densityChanged = std::abs(density - myFramebufferScale) > 0.01f;
    const bool layoutChanged = myShell.mobile.phone != myScaledForPhone;
    if (mySettings.uiScale == myAppliedScaleSetting && !densityChanged && !layoutChanged) return;
    myScaledForPhone = myShell.mobile.phone;
    myAppliedScaleSetting = mySettings.uiScale;
    if (densityChanged) {
        log::Info("Screen density {:.2f} -> {:.2f}: text and icons again", myFramebufferScale, density);
        myFramebufferScale = density;
        ImGui::GetIO().FontGlobalScale = 1.0f / density;
        myIcons.SetScale(density);
    }
    const float scale = ChooseScale();
    if (!densityChanged && std::abs(scale - myScale) < 1e-3f) return;
    // Fonts are rasterised for one size: build them again, then the texture.
    myScale = scale;
    myFonts.Load(myAssets, myScale * myFramebufferScale);
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    ImGui_ImplOpenGL3_CreateFontsTexture();
    myUi->scale = myScale;
    ui::ApplyToImGui(myTheme, myScale);
    myFramesToDraw = std::max(myFramesToDraw, 3);
}

void Application::LeaveSwitchedDocument(AppContext& app) {
    // Another document came to the front (a tab, the phone's projects strip, a
    // close that asks first): the open operation and the sketch's picks belong
    // to the one left. Ids repeat across documents, so they must not reach the
    // new one — an extrude panel once edited the other part's sketch as if it
    // were an extrude (18.10.2026). What was not confirmed is dropped, as when
    // another tool starts.
    Session* now = myWorkspace.Active();
    // Open: a tab, or a part edited in place in one (it is not a tab of its own).
    const auto alive = [&](const Session* p) -> Session* {
        return p && myWorkspace.IsOpen(p) ? const_cast<Session*>(p) : nullptr;
    };
    // An operation opened in another document than the one in front (it may have
    // been opened in the new one in the same frame, as the emulator's test does).
    if (OperationEditor* open = myOps.Active(); open && open->Owner() && open->Owner() != now) {
        if (Session* owner = alive(open->Owner())) open->Cancel(app, *owner);
        else open->Drop(app);
    }
    if (now == myActiveSession) return;
    if (myActiveSession) {
        mySketch.LeaveDocument(alive(myActiveSession));
        app.shell.measuring = false;
    }
    myActiveSession = now;
}

void Application::Frame() {
    // The phone or the desktop layout, by the window's size (ADR-0004).
    myShell.mobile.phone = mobile::WantsPhoneLayout(mySettings, myWindow);
    ApplyScaleIfChanged();
    const double now = platform::Window::Time();
    const double dt = now - myLastFrameTime;
    myLastFrameTime = now;
    if (dt > 0 && dt < 1.0) myFps = myFps * 0.9f + static_cast<float>(1.0 / dt) * 0.1f;

    ImGui_ImplOpenGL3_NewFrame();
#if defined(__ANDROID__)
    {
        ImGuiIO& io = ImGui::GetIO();
        int w = 0, h = 0, fw = 0, fh = 0;
        myWindow.WindowSize(w, h);
        myWindow.FramebufferSize(fw, fh);
        io.DisplaySize = {static_cast<float>(w), static_cast<float>(h)};
        io.DisplayFramebufferScale = {w > 0 ? static_cast<float>(fw) / static_cast<float>(w) : 1.0f,
                                      h > 0 ? static_cast<float>(fh) / static_cast<float>(h) : 1.0f};
        io.DeltaTime = dt > 0 && dt < 1.0 ? static_cast<float>(dt) : 1.0f / 60.0f;
        myTouch.BeginFrame(*myContext, now);   // fingers into ImGui's queue, before it reads it
    }
#else
    ImGui_ImplGlfw_NewFrame();
#endif
    ImGui::NewFrame();

    AppContext& app = *myContext;
    app.now = now;
    myStats.fps = myFps;
    if (now - myMemoryReadAt >= 1.0) {   // a system call: once a second is enough
        myStats.memoryMb = platform::ResidentMemoryMb();
        mon::Memory(myStats.memoryMb);
        myMemoryReadAt = now;
    }
    myStats.hoverMs = app.stats.hoverMs;
    app.stats = myStats;

    app.pressedChords = myInput.chords;
    if (myShell.captureShortcut.empty()) myActions.HandleShortcuts(myInput.chords);
    myInput.chords.clear();
    LeaveSwitchedDocument(app);
    if (Session* s = myWorkspace.Active()) {
        // Editing a sketch or an operation waits only for the part up to it;
        // what comes after is rebuilt once editing ends.
        if (s->mode == EditMode::Sketch && s->doc.IndexOf(s->activeSketch) >= 0) {
            s->LimitRebuild(static_cast<std::size_t>(s->doc.IndexOf(s->activeSketch)), false);
        } else if (const OperationEditor* op = myOps.Active(); op && s->doc.IndexOf(op->EditedFeature()) >= 0) {
            s->LimitRebuild(static_cast<std::size_t>(s->doc.IndexOf(op->EditedFeature())), true);
        } else {
            s->LimitRebuild(std::nullopt, false);
        }
        bool rebuilt = false;
        s->SetMeshQuality(mySettings.graphics);
        s->AllowBackground(mySettings.backgroundRebuild);
        {
            prof::Scope scope("rebuild");
            rebuilt = s->RebuildIfNeeded();
        }
        if (rebuilt) {
            myFramesToDraw = std::max(myFramesToDraw, 2);
            // A changed variable, undo or redo may push the body off screen.
            if (s->mode == EditMode::Model && !myOps.Active()) myShell.keepModelInView = true;
        }
        {
            prof::Scope scope("sync");
            std::vector<ObjectId> hidden;   // operations hidden in the tree: their bodies too
            for (const auto& f : s->doc.Features())
                if (f->hidden) hidden.push_back(f->id);
            myViewport.SetHiddenBodies(std::move(hidden));
            myViewport.Sync(s->model);
        }
        // A part edited in place: the assembly around it follows its changes
        // (shown from memory), and the other components stand faded around it.
        if (s->host && s->host->assembly) {
            prof::Scope scope("in-place");
            s->host->assembly->Update(s->host->path, s->host->ContextName());
            myViewport.SetContext(s->host->assembly->InPlaceContext());
        } else {
            myViewport.SetContext(nullptr);
        }
        if (s->assembly) {
            const doc::Look& look = s->assembly->ShownLook();
            myViewport.SetLook(look, s, s->assembly->ShownLookRevision());
        } else {
            myViewport.SetLook(s->doc.look, s, s->doc.LookRevision());
        }
        myViewport.SetDisplayStyle(myShell.displayStyle);
        myViewport.SetQuality(mySettings.graphics);
        myViewport.SetTextureLimit(Settings::TextureLimit(mySettings.textureQuality));
        if (myShell.keepModelInView) {   // the model was rebuilt and synced above
            myShell.keepModelInView = false;
            KeepInView(app, myViewport.ModelBounds());
        }
    }
    myBusy = myViewport.StepAnimation(now);

    const auto smooth = [](double& into, double ms) { into = into * 0.9 + ms * 0.1; };
    const auto msSince = [](double t) { return (platform::Window::Time() - t) * 1000.0; };
    const double uiStart = platform::Window::Time();
    {
        prof::Scope scope("interface");
        myShell.animateUi = false;
        if (myShell.mobile.phone) mobile::DrawPhoneRoot(app);
        else DrawRoot();
        DrawDialogs(app);
        mobile::DrawLastCrash(app);   // Android: the last run crashed or hung (never set on the desktop)
        mobile::DrawSaveAsk(app);     // Android: the first save's name and folder (never opened on the desktop)
    }
    const ImVec2 corner = myShell.viewportMax.x > 0 && !myWorkspace.ShowsHome() ? myShell.viewportMax : ImGui::GetIO().DisplaySize;
    myBusy = myNotes.Draw(*myUi, corner, now) || myBusy || myShell.animateUi;

    ImGui::Render();
    smooth(myStats.uiMs, msSince(uiStart));
    if (myWorkspace.Active() && !myWorkspace.ShowsHome() && myViewport.NeedsRender()) {
        const double sceneStart = platform::Window::Time();
        {
            prof::Scope scope("scene");
            myViewport.Render();
        }
        // Logged when it changes: the first 3D render and anything unusual
        // after it (20 lines at most: an error every other frame must not flood the log).
        if (std::string state = myViewport.HandBackToWindow(); state != myGlStateAfterScene) {
            if (myGlStateLogs < 20) {
                ++myGlStateLogs;
                log::Info("OpenGL after the 3D view: {}", state);
            }
            myGlStateAfterScene = std::move(state);
        }
        smooth(myStats.sceneMs, msSince(sceneStart));
        ++mySceneFramesThisSecond;
    }
    const double presentStart = platform::Window::Time();
    if (myWindow.ReclaimDrawable() && !myDrawableReclaimLogged) {
        myDrawableReclaimLogged = true;
        log::Info("OpenGL: the window's drawable taken back from the 3D view (Linux, GLX)");
    }

    int fbW = 0, fbH = 0;
    myWindow.FramebufferSize(fbW, fbH);
    glViewport(0, 0, fbW, fbH);
#if !defined(__ANDROID__)
    // UI colours are sRGB values meant to be written as they are. OCCT
    // switches sRGB writes on for its own rendering and does not switch them
    // off, which would make the whole interface look washed out.
    glDisable(kGlFramebufferSrgb);
#endif
    // OCCT leaves alpha writes off ("colour writes rgb-" in the log). With
    // them off the window's alpha is never written; where the window has an
    // alpha channel the desktop shows it see-through or black (Mesa under
    // XWayland: a black window). Every frame, not only after a 3D render:
    // OCCT also sets up its context when the program starts.
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_SCISSOR_TEST);
    const Color bg = myTheme.colors.appBackground;
    glClearColor(bg.r, bg.g, bg.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (myFrameIndex == myScreenshotFrame) TakeScreenshot(myScreenshotFile);
    if (!myPendingShot.empty()) {
        TakeScreenshot(myPendingShot);
        myPendingShot.clear();
    }
    myWindow.SwapBuffers();
    smooth(myStats.presentMs, msSince(presentStart));
    prof::Add("present", msSince(presentStart));   // drawing the interface and the swap
    ++myFrameIndex;
    if (now - mySecondStart >= 1.0) {
        myStats.sceneFrames = mySceneFramesThisSecond;
        mySceneFramesThisSecond = 0;
        mySecondStart = now;
    }

    // Work requested by panels during the frame, now that nobody iterates.
    std::vector<std::function<void()>> work;
    work.swap(myDeferred);
    {
        prof::Scope scope("commands");
        for (auto& w : work) w();
    }
    if (!work.empty()) myFramesToDraw = std::max(myFramesToDraw, 2);

    // A frame long enough to be felt: say in the log where the time went.
    const double frameMs = (platform::Window::Time() - now) * 1000.0;
    const std::vector<prof::Part> parts = prof::TakeFrame();
    if (frameMs > 80.0) log::Info("Slow frame {:.0f} ms: {}", frameMs, prof::Describe(parts));
    mon::Frame(frameMs, parts);
    mon::Beat();

    UpdateTitle();
}

void Application::DrawRoot() {
    AppContext& app = *myContext;
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::ToVec4(myTheme.colors.appBackground));
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();

    const ui::ThemeMetrics& m = myTheme.metrics;
    const float width = io.DisplaySize.x;
    float y = 0;
    DrawHeader(app, {0, y}, width);
    y += myUi->S(m.headerHeight);

    const float bottom = io.DisplaySize.y - myUi->S(m.statusHeight) - myUi->S(m.tabBarHeight);
    Session* session = myWorkspace.Active();
    if (myWorkspace.ShowsHome() || !session) {
        DrawHomeScreen(app, {0, y}, {width, bottom - y});
    } else {
        DrawModeBar(app, {0, y}, width);
        y += myUi->S(m.modeBarHeight);
        y += DrawRibbon(app, *session, {0, y}, width);
        const float leftWidth = myUi->S(m.railWidth + m.panelWidth);
        DrawLeftArea(app, *session, {0, y}, {leftWidth, bottom - y});
        DrawViewport(app, *session, {leftWidth, y}, {width - leftWidth, bottom - y});
    }
    DrawProjectTabs(app, {0, bottom}, width);
    DrawStatusBar(app, {0, bottom + myUi->S(m.tabBarHeight)}, width);
    ImGui::End();
}

void Application::PollShotRequest() {
    if (myShotDir.empty() || !myPendingShot.empty()) return;
    const std::filesystem::path request = myShotDir / "request";
    std::error_code ec;
    if (!std::filesystem::exists(request, ec)) return;
    std::string name;
    std::getline(std::ifstream(request), name);
    std::filesystem::remove(request, ec);
    myPendingShot = myShotDir / ((name.empty() ? std::string("shot") : name) + ".ppm");
    myFramesToDraw = std::max(myFramesToDraw, 2);
}

void Application::TakeScreenshot(const std::filesystem::path& file) const {
    platform::SaveFramebufferPpm(myWindow, file);
    log::Info("Screenshot saved: {}", ToUtf8(file));
}

} // namespace bz::app
