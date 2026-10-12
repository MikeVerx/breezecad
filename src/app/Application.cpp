#include "app/Application.h"
#include "app/BetaTesting.h"

#include "app/Language.h"
#include "core/Monitor.h"
#include "app/Operations.h"
#include "app/shell/Shell.h"
#include "app/shell/Splash.h"
#include "app/Backgrounds.h"
#include "modules/ModuleHost.h"
#include "modules/ModuleManager.h"
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

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <lunasvg.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>

// Defined (non-static) in imgui_impl_glfw.cpp but not declared in its header.
ImGuiKey ImGui_ImplGlfw_KeyToImGuiKey(int keycode, int scancode);

namespace bz::app {
namespace {

constexpr GLenum kGlFramebufferSrgb = 0x8DB9;   // GL_FRAMEBUFFER_SRGB (OpenGL 3.0, absent from GL 1.1 headers)

/// Input callbacks installed before ImGui's, which chain to them.
Application::InputTap* TapOf(GLFWwindow* window) {
    return static_cast<Application::InputTap*>(glfwGetWindowUserPointer(window));
}

/// Marks the window as needing frames after any input.
void OnAnyInput(GLFWwindow* window) {
    if (auto* tap = TapOf(window)) tap->seen = true;
}

/// Records a key press together with the modifiers held at that instant.
/// ImGui sees key events spread over frames, so a quick Ctrl+S on a slow
/// frame may reach it with Ctrl already released; this never loses it.
void OnKey(GLFWwindow* window, int key, int scancode, int action, int mods) {
    OnAnyInput(window);
    if (action != GLFW_PRESS) return;
    const ImGuiKey imguiKey = ImGui_ImplGlfw_KeyToImGuiKey(key, scancode);
    if (imguiKey == ImGuiKey_None) return;
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

} // namespace

Application::Application() = default;

Application::~Application() {
    myModules.reset();   // modules shut down while everything they may touch is still there
    if (myUi) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
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

    glfwSetWindowUserPointer(myWindow.Handle(), &myInput);
    glfwSetCursorPosCallback(myWindow.Handle(), [](GLFWwindow* w, double, double) { OnAnyInput(w); });
    glfwSetMouseButtonCallback(myWindow.Handle(), [](GLFWwindow* w, int, int, int) { OnAnyInput(w); });
    glfwSetScrollCallback(myWindow.Handle(), [](GLFWwindow* w, double, double) { OnAnyInput(w); });
    glfwSetKeyCallback(myWindow.Handle(), OnKey);
    glfwSetCharCallback(myWindow.Handle(), [](GLFWwindow* w, unsigned) { OnAnyInput(w); });
    glfwSetWindowSizeCallback(myWindow.Handle(), [](GLFWwindow* w, int, int) { OnAnyInput(w); });
    glfwSetWindowFocusCallback(myWindow.Handle(), [](GLFWwindow* w, int) { OnAnyInput(w); });
    glfwSetWindowRefreshCallback(myWindow.Handle(), [](GLFWwindow* w) { OnAnyInput(w); });

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
    ImGui_ImplGlfw_InitForOpenGL(myWindow.Handle(), true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    // HiDPI: on macOS the framebuffer is 2x the window; elsewhere the UI scales.
    int winW = 1, fbW = 1, h = 1;
    glfwGetWindowSize(myWindow.Handle(), &winW, &h);
    glfwGetFramebufferSize(myWindow.Handle(), &fbW, &h);
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
                                                        myViewport, mySketch, myOps, myShell, myDeferred, 0.0, {}, {}});
    RegisterActions(*myContext);
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
    if (myShell.crashedLastTime && !myShell.recoverable.empty())
        myNotes.Info(Tr("Прошлый сеанс завершился неожиданно. Несохранённую работу можно восстановить на главной странице."));
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
    while (true) {
        mon::Beat();
        if (NeedsAnotherFrame()) {
            platform::Window::PollEvents();
        } else {
            // Wakes up for autosave and theme checks; often enough to show
            // a rebuild running on another thread.
            platform::Window::WaitEvents(WaitsForBackground() ? 1.0 / 30.0 : 1.0);
        }
        const double now = glfwGetTime();
        if (myInput.seen) {
            myInput.seen = false;
            myFramesToDraw = std::max(myFramesToDraw, 3);
            myShell.lastInputTime = now;
        }

        // The window close button: ask about unsaved documents first.
        if (myWindow.ShouldClose()) {
            myWindow.SetShouldClose(false);
            myShell.quitRequested = true;
            myFramesToDraw = std::max(myFramesToDraw, 3);
        }
        PollShotRequest();
        HandleCloseRequests();
        if (myShell.quitRequested && myWorkspace.sessions.empty()) break;

        if (mySettings.background + "|" + mySettings.backgroundPicture != myBackgroundApplied) ApplyTheme(true);   // chosen: at once
        if (now - mySystemThemeCheck > 2.0) {
            mySystemThemeCheck = now;
            ApplyTheme(false);
        }
        for (auto& s : myWorkspace.sessions)
            if (myAutosave->Update(*s, mySettings, now, now - myShell.lastInputTime)) myFramesToDraw = std::max(myFramesToDraw, 1);

        if (myFramesToDraw > 0 || NeedsAnotherFrame() || WaitsForBackground()) {
            Frame();
            if (myFramesToDraw > 0) --myFramesToDraw;
        }
        if (myScreenshotFrame >= 0 && myFrameIndex > myScreenshotFrame) break;
    }
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
    modules::HostInfo host = modules::HostInfo::Detect();
    host.openGL            = myWindow.OpenGLVersion();
    myModules->Discover(platform::AssetsDir().parent_path() / "modules", host);
    myModules->Discover(config / "modules", host);
    myModules->LoadAll();
    myContext->modules = myModules.get();
    if (const std::string& id = myModules->CrashedLastRun(); !id.empty()) {
        const modules::ModuleEntry* m = myModules->Find(id);
        myNotes.Error(Trf("Модуль «{}» аварийно закрыл BreezeCAD в прошлый раз и отключён. Включить снова: Настройки → Модули.",
                          m ? m->manifest.name : id));
    }
}

bool Application::NeedsAnotherFrame() const {
    if (myBusy || myViewport.NeedsRender() || !myDeferred.empty()) return true;
    for (const auto& s : myWorkspace.sessions)
        if (s->Rebuilding() && !s->RebuildingInBackground()) return true;   // the next slice of a rebuild
    if (myScreenshotFrame >= 0) return true;
    const ImGuiIO& io = ImGui::GetIO();
    for (bool down : io.MouseDown)
        if (down) return true;
    return false;
}

bool Application::WaitsForBackground() const {
    // Only the document on screen: another one takes its result when shown.
    const int i = myWorkspace.ActiveIndex();
    return i >= 0 && i < static_cast<int>(myWorkspace.sessions.size()) && myWorkspace.sessions[static_cast<std::size_t>(i)]->RebuildingInBackground();
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
    if (Session* s = myWorkspace.Active(); s && !myWorkspace.ShowsHome())
        title = s->Title() + (s->IsModified() ? " \xE2\x80\xA2" : "") + " \xE2\x80\x94 " + program;
    if (title != myTitle) {
        myTitle = title;
        myWindow.SetTitle(title);
    }
}

float Application::ChooseScale() const {
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
    if (mySettings.uiScale == myAppliedScaleSetting) return;
    myAppliedScaleSetting = mySettings.uiScale;
    const float scale = ChooseScale();
    if (std::abs(scale - myScale) < 1e-3f) return;
    // Fonts are rasterised for one size: build them again, then the texture.
    myScale = scale;
    myFonts.Load(myAssets, myScale * myFramebufferScale);
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    ImGui_ImplOpenGL3_CreateFontsTexture();
    myUi->scale = myScale;
    ui::ApplyToImGui(myTheme, myScale);
    myFramesToDraw = std::max(myFramesToDraw, 3);
}

void Application::Frame() {
    ApplyScaleIfChanged();
    const double now = glfwGetTime();
    const double dt = now - myLastFrameTime;
    myLastFrameTime = now;
    if (dt > 0 && dt < 1.0) myFps = myFps * 0.9f + static_cast<float>(1.0 / dt) * 0.1f;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
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
        myViewport.SetLook(s->doc.look, s, s->doc.LookRevision());
        myViewport.SetDisplayStyle(myShell.displayStyle);
        myViewport.SetQuality(mySettings.graphics);
        if (myShell.keepModelInView) {   // the model was rebuilt and synced above
            myShell.keepModelInView = false;
            KeepInView(app, myViewport.ModelBounds());
        }
    }
    myBusy = myViewport.StepAnimation(now);

    const auto smooth = [](double& into, double ms) { into = into * 0.9 + ms * 0.1; };
    const auto msSince = [](double t) { return (glfwGetTime() - t) * 1000.0; };
    const double uiStart = glfwGetTime();
    {
        prof::Scope scope("interface");
        DrawRoot();
        DrawDialogs(app);
    }
    const ImVec2 corner = myShell.viewportMax.x > 0 && !myWorkspace.ShowsHome() ? myShell.viewportMax : ImGui::GetIO().DisplaySize;
    myBusy = myNotes.Draw(*myUi, corner, now) || myBusy;

    ImGui::Render();
    smooth(myStats.uiMs, msSince(uiStart));
    if (myWorkspace.Active() && !myWorkspace.ShowsHome() && myViewport.NeedsRender()) {
        const double sceneStart = glfwGetTime();
        {
            prof::Scope scope("scene");
            myViewport.Render();
        }
        smooth(myStats.sceneMs, msSince(sceneStart));
        ++mySceneFramesThisSecond;
    }
    const double presentStart = glfwGetTime();

    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(myWindow.Handle(), &fbW, &fbH);
    glViewport(0, 0, fbW, fbH);
    // UI colours are sRGB values meant to be written as they are. OCCT
    // switches sRGB writes on for its own rendering and does not switch them
    // off, which would make the whole interface look washed out.
    glDisable(kGlFramebufferSrgb);
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
    const double frameMs = (glfwGetTime() - now) * 1000.0;
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
    int w = 0, h = 0;
    glfwGetFramebufferSize(myWindow.Handle(), &w, &h);
    std::vector<unsigned char> pixels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream out(file, std::ios::binary);
    if (!out) return;
    out << "P6\n" << w << ' ' << h << "\n255\n";
    const auto rowBytes = static_cast<std::streamsize>(w) * 3;
    for (int row = h - 1; row >= 0; --row)
        out.write(reinterpret_cast<const char*>(pixels.data()) + static_cast<std::ptrdiff_t>(row) * rowBytes, rowBytes);
    log::Info("Screenshot saved: {}", ToUtf8(file));
}

} // namespace bz::app
