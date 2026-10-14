// SPDX-License-Identifier: MPL-2.0
#include "app/shell/CrashWindow.h"

#include "app/Language.h"
#include "app/Settings.h"
#include "core/CrashCode.h"
#include "core/I18n.h"
#include "core/PathText.h"
#include "platform/Launch.h"
#include "platform/Paths.h"
#include "platform/Process.h"
#include "platform/SystemInfo.h"
#include "platform/SystemTheme.h"
#include "platform/Window.h"
#include "ui/Fonts.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>

namespace bz::app {
namespace {

constexpr float kWidth = 600, kHeight = 390;   // logical pixels

enum class Mode { Crash, Hang, Ended };

struct Request {
    Mode                  mode = Mode::Crash;
    platform::ProcessId   pid = 0;        ///< the frozen process (hang)
    std::filesystem::path report;
    int                   seconds = 0;    ///< how long it had not responded when the window was asked for
};

bool ParseRequest(const std::vector<std::filesystem::path>& args, Request& r) {
    if (args.empty()) return false;
    const std::string flag = ToUtf8(args[0]);
    if (flag == "--crash-window" && args.size() >= 2) {
        r.mode = Mode::Crash;
        r.report = args[1];
        return true;
    }
    if (flag == "--hang-window" && args.size() >= 4) {
        r.mode = Mode::Hang;
        r.pid = std::atoll(ToUtf8(args[1]).c_str());
        r.report = args[2];
        r.seconds = std::atoi(ToUtf8(args[3]).c_str());
        return r.pid > 0;
    }
    return false;
}

std::string ReadText(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// What the user pastes into a chat: the code first, then the report and
/// the computer (the report has the program and its graphics already).
std::string FullReport(const std::string& code, const std::string& report) {
    std::string out = "Error code: " + code + "\n\n" + report;
    if (!out.ends_with('\n')) out += '\n';
    out += "\nComputer:\n";
    for (const platform::SystemFact& f : platform::DescribeSystem(platform::ConfigDir())) out += "  " + f.key + ": " + f.value + "\n";
    return out;
}

void Wrapped(ImFont* font, const Color& colour, const std::string& text) {
    ui::FontScope scope(font);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(colour));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

class CrashWindow {
public:
    explicit CrashWindow(Request request) : myRequest(std::move(request)) {}

    int Run() {
        if (!Open()) return 1;
        const double opened = glfwGetTime();
        bool copiedCode = false, copiedReport = false;
        // Test hook: BZ_CRASH_WINDOW_SHOT=out.ppm[:frame] saves a frame and closes (CI
        // pictures). Not BZ_SCREENSHOT: the program that started this window may use it.
        std::filesystem::path shot;
        int shotFrame = 5, frame = 0;
        if (const char* spec = std::getenv("BZ_CRASH_WINDOW_SHOT")) {
            std::string s = spec;
            if (const auto colon = s.rfind(':'); colon != std::string::npos && colon > 1) {
                shotFrame = std::atoi(s.c_str() + colon + 1);
                s.resize(colon);
            }
            shot = PathFromUtf8(s);
        }
        while (!glfwWindowShouldClose(myWindow) && !myDone) {
            if (shot.empty()) glfwWaitEventsTimeout(0.5);   // also counts the seconds of a hang, and notices the program ending
            else glfwPollEvents();
            if (myRequest.mode == Mode::Hang && !platform::ProcessRunning(myRequest.pid)) break;   // it ended by itself (or crashed: its own window takes over)
            const int seconds = myRequest.seconds + static_cast<int>(glfwGetTime() - opened);
            BeginFrame();
            DrawContent(seconds, copiedCode, copiedReport);
            EndFrame();
            if (!shot.empty() && ++frame >= shotFrame) {
                platform::SaveFramebufferPpm(myWindow, shot);
                break;
            }
        }
        Close();
        return 0;
    }

private:
    bool Open() {
        const Settings settings = Settings::Load(platform::ConfigDir() / "settings.json");
        ApplyLanguage(settings);
        const bool dark = settings.theme == ThemeChoice::Dark ||
                          (settings.theme == ThemeChoice::System && platform::SystemPrefersDark());
        myTheme = dark ? ui::Theme::Dark() : ui::Theme::Light();
        myReport = ReadText(myRequest.report);
        myCode = crash::ErrorCode(myReport);
        myLastAction = crash::ReportField(myReport, "Last action");

        if (!platform::InitGlfw()) return false;
        float sx = 1.0f, sy = 1.0f;
        if (GLFWmonitor* monitor = glfwGetPrimaryMonitor()) glfwGetMonitorContentScale(monitor, &sx, &sy);
        const float contentScale = std::clamp(sx, 1.0f, 3.0f);
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#if defined(__APPLE__)
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        const float windowScale = 1.0f;   // macOS scales windows itself
#else
        const float windowScale = contentScale;
#endif
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);   // over the frozen window, not lost behind it
        const int w = static_cast<int>(std::lround(kWidth * windowScale)), h = static_cast<int>(std::lround(kHeight * windowScale));
        const std::string title = myRequest.mode == Mode::Hang ? Tr("BreezeCAD не отвечает") : Tr("BreezeCAD — ошибка");
        myWindow = glfwCreateWindow(w, h, title.c_str(), nullptr, nullptr);
        if (!myWindow) return false;
        if (GLFWmonitor* monitor = glfwGetPrimaryMonitor()) {
            int ax = 0, ay = 0, aw = 0, ah = 0;
            glfwGetMonitorWorkarea(monitor, &ax, &ay, &aw, &ah);
            glfwSetWindowPos(myWindow, ax + (aw - w) / 2, ay + (ah - h) / 2);
        }
        glfwMakeContextCurrent(myWindow);
        glfwSwapInterval(1);

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        ImGui_ImplGlfw_InitForOpenGL(myWindow, true);
        ImGui_ImplOpenGL3_Init("#version 330 core");
        int winW = 1, fbW = 1, unused = 1;
        glfwGetWindowSize(myWindow, &winW, &unused);
        glfwGetFramebufferSize(myWindow, &fbW, &unused);
        const float framebufferScale = winW > 0 ? static_cast<float>(fbW) / static_cast<float>(winW) : 1.0f;
        myScale = static_cast<float>(winW) / kWidth;
        ui::ApplyToImGui(myTheme, myScale);
        const std::filesystem::path assets = platform::AssetsDir();
        if (!myFonts.Load(assets, myScale * framebufferScale)) return false;
        io.FontGlobalScale = 1.0f / framebufferScale;
        myIcons.SetDirectory(assets / "icons");   // nothing here draws icons; ui::Ui needs the set
        myUi = std::make_unique<ui::Ui>(ui::Ui{myTheme, myFonts, myIcons, myScale});
        glfwShowWindow(myWindow);
        glfwFocusWindow(myWindow);
        return true;
    }

    void Close() {
        if (!myWindow) return;
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(myWindow);
        myWindow = nullptr;
        glfwTerminate();
    }

    void BeginFrame() {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void EndFrame() {
        ImGui::Render();
        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(myWindow, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        const Color bg = myTheme.colors.panel;
        glClearColor(bg.r, bg.g, bg.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(myWindow);
    }

    void DrawContent(int seconds, bool& copiedCode, bool& copiedReport) {
        ui::Ui& ui = *myUi;
        const ui::ThemeColors& c = myTheme.colors;
        const ImGuiIO& io = ImGui::GetIO();
        const float pad = ui.S(28);
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::ToVec4(c.panel));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {pad, pad});
        ImGui::Begin("##crash", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        // What happened, in plain words.
        std::string title, body;
        switch (myRequest.mode) {
            case Mode::Crash:
                title = Tr("BreezeCAD аварийно завершил работу");
                body = Tr("Произошла ошибка, и программе пришлось закрыться. Работу из автосохранения можно будет "
                          "восстановить при следующем запуске.");
                break;
            case Mode::Hang:
                title = Tr("BreezeCAD не отвечает");
                body = Trf("Программа не отвечает уже {} с. Возможно, идёт долгая операция, и стоит подождать. Если ждать "
                           "бессмысленно, закройте программу: работу из автосохранения можно будет восстановить при "
                           "следующем запуске.",
                           seconds);
                break;
            case Mode::Ended:
                title = Tr("BreezeCAD закрыт");
                body = Tr("Программа закрыта. Работу из автосохранения можно будет восстановить при следующем запуске.");
                break;
        }
        {
            ui::FontScope font(ui.fonts.display);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(myRequest.mode == Mode::Hang ? c.warning : c.danger));
            ImGui::TextUnformatted(title.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::Dummy({0, ui.S(6)});
        Wrapped(ui.fonts.body, c.textSecondary, body);
        ImGui::Dummy({0, ui.S(14)});

        // The error code: big, in a box, with its own copy button.
        Wrapped(ui.fonts.caption, c.textMuted, Tr("Код ошибки"));
        {
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x, h = ui.S(44);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(at, {at.x + w, at.y + h}, ui::ToU32(c.field), ui.S(6));
            dl->AddRect(at, {at.x + w, at.y + h}, ui::ToU32(c.fieldBorder), ui.S(6));
            const float fs = ui.S(18);
            const ImVec2 ts = ui.fonts.monoLarge->CalcTextSizeA(fs, FLT_MAX, 0, myCode.c_str());
            dl->AddText(ui.fonts.monoLarge, fs, {at.x + ui.S(14), at.y + (h - ts.y) / 2}, ui::ToU32(c.text), myCode.c_str());
            const float bw = ui.S(130);
            ImGui::SetCursorScreenPos({at.x + w - bw - ui.S(6), at.y + ui.S(6)});
            if (ui::SecondaryButton(ui, "##copy-code", copiedCode ? Tr("Скопировано") : Tr("Копировать"), "",
                                    {bw, h - ui.S(12)})) {
                ImGui::SetClipboardText(myCode.c_str());
                copiedCode = true;
            }
            ImGui::SetCursorScreenPos({at.x, at.y + h + ui.S(6)});
        }
        if (!myLastAction.empty()) Wrapped(ui.fonts.caption, c.textMuted, Trf("Последнее действие: {}", myLastAction));
        ImGui::Dummy({0, ui.S(4)});
        if (ui::LinkButton(ui, "##copy-report", copiedReport ? Tr("Отчёт скопирован") : Tr("Скопировать отчёт"))) {
            ImGui::SetClipboardText(FullReport(myCode, myReport).c_str());
            copiedReport = true;
        }
        ImGui::SameLine(0, ui.S(4));
        if (ui::LinkButton(ui, "##open-folder", Tr("Открыть папку с отчётом"))) platform::OpenFolder(myRequest.report.parent_path());
        Wrapped(ui.fonts.caption, c.textMuted,
                Tr("Отчёт сохранён только на этом компьютере. Чтобы ошибку исправили, отправьте код и отчёт разработчику."));

        // The buttons, at the bottom right.
        const float bh = ui.S(36), gap = ui.S(8);
        const float right = io.DisplaySize.x - pad, bottom = io.DisplaySize.y - pad - bh;
        if (myRequest.mode == Mode::Hang) {
            const float wWait = ui.S(150), wEnd = ui.S(190);
            ImGui::SetCursorScreenPos({right - wWait - gap - wEnd, bottom});
            if (ui::SecondaryButton(ui, "##end", Tr("Закрыть BreezeCAD"), "", {wEnd, bh})) {
                platform::StopProcess(myRequest.pid);
                myRequest.mode = Mode::Ended;
            }
            ImGui::SameLine(0, gap);
            if (ui::PrimaryButton(ui, "##wait", Tr("Подождать"), "", {wWait, bh})) myDone = true;
        } else {
            const float wClose = ui.S(120), wRestart = ui.S(220);
            ImGui::SetCursorScreenPos({right - wRestart - gap - wClose, bottom});
            if (ui::SecondaryButton(ui, "##close", Tr("Закрыть"), "Esc", {wClose, bh}) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                myDone = true;
            ImGui::SameLine(0, gap);
            if (ui::PrimaryButton(ui, "##restart", Tr("Перезапустить BreezeCAD"), "", {wRestart, bh})) {
                platform::StartProcess(platform::ExecutablePath(), {});
                myDone = true;
            }
        }
        ImGui::End();
    }

    Request                  myRequest;
    GLFWwindow*              myWindow = nullptr;
    ui::Theme                myTheme = ui::Theme::Light();
    ui::Fonts                myFonts;
    ui::Icons                myIcons;
    std::unique_ptr<ui::Ui>  myUi;
    float                    myScale = 1.0f;
    std::string              myReport;
    std::string              myCode;
    std::string              myLastAction;
    bool                     myDone = false;
};

} // namespace

bool IsCrashWindowRequest(const std::vector<std::filesystem::path>& args) {
    Request r;
    return ParseRequest(args, r);
}

int RunCrashWindow(const std::vector<std::filesystem::path>& args) {
    Request r;
    if (!ParseRequest(args, r)) return 2;
    return CrashWindow(std::move(r)).Run();
}

} // namespace bz::app
