#include "app/Application.h"

#include "app/Operations.h"
#include "app/shell/Shell.h"
#include "app/viewport/ViewportPanel.h"
#include "core/Log.h"
#include "core/PathText.h"
#include "platform/Paths.h"
#include "platform/ProcessInfo.h"
#include "platform/SystemTheme.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <lunasvg.h>

#include <cstdio>
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
    log::OpenFile(config / "logs" / "breezecad.log");
    log::Info("BreezeCAD 0.2.0 starting");

    // A lock file left from the last run means it did not exit normally.
    myShell.crashedLastTime = std::filesystem::exists(config / "running.lock");
    std::ofstream(config / "running.lock") << "running";

    mySettings = Settings::Load(config / "settings.json");
    if (!myWindow.Create({})) return false;

    glfwSetWindowUserPointer(myWindow.Handle(), &myInput);
    glfwSetCursorPosCallback(myWindow.Handle(), [](GLFWwindow* w, double, double) { OnAnyInput(w); });
    glfwSetMouseButtonCallback(myWindow.Handle(), [](GLFWwindow* w, int, int, int) { OnAnyInput(w); });
    glfwSetScrollCallback(myWindow.Handle(), [](GLFWwindow* w, double, double) { OnAnyInput(w); });
    glfwSetKeyCallback(myWindow.Handle(), OnKey);
    glfwSetCharCallback(myWindow.Handle(), [](GLFWwindow* w, unsigned) { OnAnyInput(w); });
    glfwSetWindowSizeCallback(myWindow.Handle(), [](GLFWwindow* w, int, int) { OnAnyInput(w); });
    glfwSetWindowFocusCallback(myWindow.Handle(), [](GLFWwindow* w, int) { OnAnyInput(w); });
    glfwSetWindowRefreshCallback(myWindow.Handle(), [](GLFWwindow* w) { OnAnyInput(w); });

    const std::filesystem::path assets = platform::AssetsDir();
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
    myScale = std::max(1.0f, myWindow.ContentScale() / framebufferScale);
    myFonts.Load(assets, myScale * framebufferScale);
    io.FontGlobalScale = 1.0f / framebufferScale;
    myIcons.SetDirectory(assets / "icons");
    myIcons.SetScale(framebufferScale);

    if (!myViewport.Init(myWindow.NativeSurface())) return false;

    myUi = std::make_unique<ui::Ui>(ui::Ui{myTheme, myFonts, myIcons, myScale});
    ApplyTheme(true);

    myAutosave = std::make_unique<Autosave>(platform::AutosaveDir());
    myShell.recoverable = myAutosave->FindRecoverable();

    myContext = std::make_unique<AppContext>(AppContext{*myUi, mySettings, myWorkspace, myActions, myNotes, *myAutosave,
                                                        myViewport, mySketch, myOps, myShell, myDeferred, 0.0, {}});
    RegisterActions(*myContext);

    for (const auto& file : files) myWorkspace.Open(file, myNotes, mySettings);
    if (myShell.crashedLastTime && !myShell.recoverable.empty())
        myNotes.Info("Прошлый сеанс завершился неожиданно. Несохранённую работу можно восстановить на главной странице.");
    return true;
}

void Application::ApplyTheme(bool force) {
    const bool dark = mySettings.theme == ThemeChoice::Dark ||
                      (mySettings.theme == ThemeChoice::System && platform::SystemPrefersDark());
    if (!force && dark == myDarkApplied) return;
    myDarkApplied = dark;
    myTheme = dark ? ui::Theme::Dark() : ui::Theme::Light();
    ui::ApplyToImGui(myTheme, myScale);
    const ui::ThemeColors& c = myTheme.colors;
    myViewport.SetColors({c.viewportTop, c.viewportBottom, c.body, c.bodyEdges, c.hoverHighlight, c.selection});
    myFramesToDraw = 3;
}

int Application::Run() {
    while (true) {
        if (NeedsAnotherFrame()) {
            platform::Window::PollEvents();
        } else {
            platform::Window::WaitEvents(1.0);   // wakes up for autosave and theme checks
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

        if (now - mySystemThemeCheck > 2.0) {
            mySystemThemeCheck = now;
            ApplyTheme(false);
        }
        for (auto& s : myWorkspace.sessions)
            if (myAutosave->Update(*s, mySettings, now, now - myShell.lastInputTime)) myFramesToDraw = std::max(myFramesToDraw, 1);

        if (myFramesToDraw > 0 || NeedsAnotherFrame()) {
            Frame();
            if (myFramesToDraw > 0) --myFramesToDraw;
        }
        if (myScreenshotFrame >= 0 && myFrameIndex > myScreenshotFrame) break;
    }
    mySettings.Save(platform::ConfigDir() / "settings.json");
    log::Info("BreezeCAD exits normally");
    return 0;
}

bool Application::NeedsAnotherFrame() const {
    if (myBusy || myViewport.NeedsRender() || !myDeferred.empty()) return true;
    if (myScreenshotFrame >= 0) return true;
    const ImGuiIO& io = ImGui::GetIO();
    for (bool down : io.MouseDown)
        if (down) return true;
    return false;
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
    std::string title = "BreezeCAD";
    if (Session* s = myWorkspace.Active(); s && !myWorkspace.ShowsHome())
        title = s->Title() + (s->IsModified() ? " \xE2\x80\xA2" : "") + " \xE2\x80\x94 BreezeCAD";
    if (title != myTitle) {
        myTitle = title;
        myWindow.SetTitle(title);
    }
}

void Application::Frame() {
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
    if (myFrameIndex % 30 == 0) myStats.memoryMb = platform::ResidentMemoryMb();
    myStats.hoverMs = app.stats.hoverMs;
    app.stats = myStats;

    myActions.HandleShortcuts(myInput.chords);
    myInput.chords.clear();
    if (Session* s = myWorkspace.Active()) {
        if (s->RebuildIfNeeded()) {
            myFramesToDraw = std::max(myFramesToDraw, 2);
            // A changed variable, undo or redo may push the body off screen.
            if (s->mode == EditMode::Model && !myOps.Active()) myShell.keepModelInView = true;
        }
        myViewport.Sync(s->model);
        myViewport.SetDisplayStyle(myShell.displayStyle);
        if (myShell.keepModelInView) {   // the model was rebuilt and synced above
            myShell.keepModelInView = false;
            KeepInView(app, myViewport.ModelBounds());
        }
    }
    myBusy = myViewport.StepAnimation(now);

    const auto smooth = [](double& into, double ms) { into = into * 0.9 + ms * 0.1; };
    const auto msSince = [](double t) { return (glfwGetTime() - t) * 1000.0; };
    const double uiStart = glfwGetTime();
    DrawRoot();
    DrawDialogs(app);
    const ImVec2 corner = myShell.viewportMax.x > 0 && !myWorkspace.ShowsHome() ? myShell.viewportMax : ImGui::GetIO().DisplaySize;
    myBusy = myNotes.Draw(*myUi, corner, now) || myBusy;

    ImGui::Render();
    smooth(myStats.uiMs, msSince(uiStart));
    if (myWorkspace.Active() && !myWorkspace.ShowsHome() && myViewport.NeedsRender()) {
        const double sceneStart = glfwGetTime();
        myViewport.Render();
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
    ++myFrameIndex;
    if (now - mySecondStart >= 1.0) {
        myStats.sceneFrames = mySceneFramesThisSecond;
        mySceneFramesThisSecond = 0;
        mySecondStart = now;
    }

    // Work requested by panels during the frame, now that nobody iterates.
    std::vector<std::function<void()>> work;
    work.swap(myDeferred);
    for (auto& w : work) w();
    if (!work.empty()) myFramesToDraw = std::max(myFramesToDraw, 2);

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
