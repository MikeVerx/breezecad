#include "app/shell/Splash.h"

#include "core/I18n.h"
#include "core/Log.h"
#include "core/PathText.h"
#include "core/PngImage.h"
#include "core/Version.h"
#include "ui/Theme.h"
#include "ui/Ui.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <lunasvg.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <thread>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F   // OpenGL 1.2; Windows headers stop at 1.1
#endif

namespace bz::app {
namespace {

constexpr float kWidth = 640, kHeight = 360;   // logical pixels, as in the approved design
constexpr float kBand = 112;                   // the lower band with the name
constexpr auto kAtLeast = std::chrono::milliseconds(700);   // shorter would only flash

/// The current OpenGL and ImGui contexts, put back when it goes.
class Keep {
public:
    Keep() : myGl(glfwGetCurrentContext()), myImGui(ImGui::GetCurrentContext()) {}
    ~Keep() {
        glfwMakeContextCurrent(myGl);
        ImGui::SetCurrentContext(myImGui);
    }
    Keep(const Keep&) = delete;
    Keep& operator=(const Keep&) = delete;

private:
    GLFWwindow*   myGl;
    ImGuiContext* myImGui;
};

unsigned Upload(const std::uint8_t* rgba, int w, int h, int rowLength) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    return texture;
}

} // namespace

Splash::~Splash() { Close(); }

bool Splash::Open(const std::filesystem::path& assets, bool dark) {
    if (std::getenv("BZ_NO_SPLASH") || std::getenv("BZ_SCREENSHOT")) return false;   // test runs
    if (!glfwInit()) return false;
    Keep keep;
    myDark = dark;
    float sx = 1.0f, sy = 1.0f;
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (!monitor) return false;
    glfwGetMonitorContentScale(monitor, &sx, &sy);
    myScale = std::clamp(sx, 1.0f, 3.0f);

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    // Window size in screen coordinates: macOS scales by itself.
#if defined(__APPLE__)
    const float windowScale = 1.0f;
#else
    const float windowScale = myScale;
#endif
    const int w = static_cast<int>(std::lround(kWidth * windowScale)), h = static_cast<int>(std::lround(kHeight * windowScale));
    myWindow = glfwCreateWindow(w, h, "BreezeCAD", nullptr, nullptr);
    glfwDefaultWindowHints();   // the main window starts from clean hints
    if (!myWindow) {
        log::Warn("Splash: no window");
        return false;
    }
    myRounded = glfwGetWindowAttrib(myWindow, GLFW_TRANSPARENT_FRAMEBUFFER) == GLFW_TRUE;
    int ax = 0, ay = 0, aw = 0, ah = 0;
    glfwGetMonitorWorkarea(monitor, &ax, &ay, &aw, &ah);
    glfwSetWindowPos(myWindow, ax + (aw - w) / 2, ay + (ah - h) / 2);
    glfwMakeContextCurrent(myWindow);
    glfwSwapInterval(0);

    myContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(myContext);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui_ImplOpenGL3_Init("#version 330 core");

    // Pixels per logical pixel in the framebuffer (2 on a Retina Mac too).
    int fbw = 1, fbh = 1;
    glfwGetFramebufferSize(myWindow, &fbw, &fbh);
    myScale = static_cast<float>(fbw) / kWidth;
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x0400, 0x04FF, 0x2010, 0x2044, 0};
    ImFontConfig config;
    config.OversampleH = 2;
    const auto font = [&](const char* file, float size) {
        const std::u8string path = (assets / "fonts" / file).u8string();
        return io.Fonts->AddFontFromFileTTF(reinterpret_cast<const char*>(path.c_str()), size * myScale, &config, ranges);
    };
    myRegular = font("IBMPlexSans-Regular.ttf", 11.5f);
    myMedium = font("IBMPlexSans-Medium.ttf", 17.0f);
    myStrong = font("IBMPlexSans-SemiBold.ttf", 26.0f);
    myMono = font("IBMPlexMono-Regular.ttf", 11.0f);
    if (!myRegular || !myMedium || !myStrong || !myMono) {
        log::Warn("Splash: fonts missing");
        Close();
        return false;
    }

    // The part, rendered by BreezeCAD itself in the theme's colours.
    if (const auto image = LoadPng(assets / "splash" / (dark ? "part-dark.png" : "part-light.png"))) {
        myPart = Upload(image->rgba.data(), image->width, image->height, 0);
        myPartW = image->width;
        myPartH = image->height;
    } else {
        log::Warn("Splash picture: {}", image.GetError().message);
    }
    const auto mark = lunasvg::Document::loadFromFile(
        ToUtf8(assets / "brand" / (dark ? "breezecad-mark-dark-ui.svg" : "breezecad-mark.svg")));
    if (mark) {
        const int mh = static_cast<int>(std::lround(45 * myScale)), mw = static_cast<int>(std::lround(38 * myScale));
        lunasvg::Bitmap bitmap = mark->renderToBitmap(mw, mh);
        if (!bitmap.isNull()) {
            bitmap.convertToRGBA();
            myMark = Upload(bitmap.data(), mw, mh, bitmap.stride() / 4);
        }
    }

    myOpenedAt = std::chrono::steady_clock::now();
    myStatus = Tr("Запуск…");
    Draw();   // drawn before it is shown: no empty frame
    glfwShowWindow(myWindow);
    Draw();
    return true;
}

void Splash::Step(const std::string& status, float progress) {
    if (!myWindow) return;
    myStatus = status;
    myProgress = std::clamp(progress, 0.0f, 1.0f);
    Keep keep;
    Draw();
}

void Splash::Close() {
    if (!myWindow) return;
    {
        Keep keep;
        // Long enough to be read rather than flash.
        myProgress = 1.0f;
        while (std::chrono::steady_clock::now() - myOpenedAt < kAtLeast) {
            Draw();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        glfwMakeContextCurrent(myWindow);
        ImGui::SetCurrentContext(myContext);
        const GLuint textures[] = {myPart, myMark};
        glDeleteTextures(2, textures);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext(myContext);
        myContext = nullptr;
    }
    glfwDestroyWindow(myWindow);
    myWindow = nullptr;
}

void Splash::Draw() {
    glfwMakeContextCurrent(myWindow);
    ImGui::SetCurrentContext(myContext);
    glfwPollEvents();   // keeps the system from calling the program "not responding"
    int fbw = 1, fbh = 1;
    glfwGetFramebufferSize(myWindow, &fbw, &fbh);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = {static_cast<float>(fbw), static_cast<float>(fbh)};
    io.DeltaTime = 1.0f / 60.0f;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    const ui::ThemeColors c = (myDark ? ui::Theme::Dark() : ui::Theme::Light()).colors;
    const float s = myScale;
    const float W = kWidth * s, H = kHeight * s, top = (kHeight - kBand) * s;
    const float radius = myRounded ? 14 * s : 0;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // Card, grid (as in a sketch), the part over it.
    dl->AddRectFilled({0, 0}, {W, H}, ui::ToU32(c.panel), radius);
    dl->PushClipRect({0, 0}, {W, top});
    for (float x = 16 * s; x < W; x += 16 * s) {
        const bool major = std::lround(x / s) % 80 == 0;
        dl->AddLine({x, 0}, {x, top}, ui::ToU32(major ? c.sketchGridMajor : c.sketchGrid), 1.0f);
    }
    for (float y = 16 * s; y < top; y += 16 * s) {
        const bool major = std::lround(y / s) % 80 == 0;
        dl->AddLine({0, y}, {W, y}, ui::ToU32(major ? c.sketchGridMajor : c.sketchGrid), 1.0f);
    }
    dl->PopClipRect();
    if (myPart) {
        const float ph = 220 * s, pw = ph * static_cast<float>(myPartW) / static_cast<float>(myPartH);
        dl->AddImage(static_cast<ImTextureID>(myPart), {(W - pw) / 2, 14 * s}, {(W + pw) / 2, 14 * s + ph});
    }

    // The band: name, version, motto, what is loading.
    dl->AddRectFilled({0, top}, {W, H}, ui::ToU32(c.appBackground), radius, ImDrawFlags_RoundCornersBottom);
    dl->AddLine({0, top}, {W, top}, ui::ToU32(c.divider), 1.0f);
    const float bx = 28 * s, by = top + 22 * s;
    if (myMark) dl->AddImage(static_cast<ImTextureID>(myMark), {bx, by}, {bx + 38 * s, by + 45 * s});
    const float nx = bx + 52 * s;
    dl->AddText(myStrong, 26 * s, {nx, by - 3 * s}, ui::ToU32(c.text), "Breeze");
    const float breeze = myStrong->CalcTextSizeA(26 * s, FLT_MAX, 0, "Breeze").x;
    dl->AddText(myStrong, 26 * s, {nx + breeze, by - 3 * s}, ui::ToU32(c.textSecondary), "CAD");
    const std::string version = Version();
    dl->AddText(myStrong, 12 * s, {nx, by + 29 * s}, ui::ToU32(c.textSecondary), version.c_str());
    const float vw = myStrong->CalcTextSizeA(12 * s, FLT_MAX, 0, version.c_str()).x;
    const std::string build = *Commit() ? std::format("{} {} \xC2\xB7 {}", Branch(), Commit(), BuildDate()) : std::string(BuildDate());
    dl->AddText(myMono, 11 * s, {nx + vw + 6 * s, by + 30 * s}, ui::ToU32(c.textMuted), build.c_str());

    const auto right = [&](ImFont* f, float size, float y, const Color& color, const std::string& text) {
        const float tw = f->CalcTextSizeA(size, FLT_MAX, 0, text.c_str()).x;
        dl->AddText(f, size, {W - 28 * s - tw, y}, ui::ToU32(color), text.c_str());
    };
    right(myMedium, 17 * s, by - 1 * s, c.text, Tr("Свобода создавать"));
    right(myRegular, 11.5f * s, by + 25 * s, c.textMuted, Trf("С любовью от {}", "MikeVerx"));
    dl->AddText(myRegular, 11.5f * s, {bx, H - 32 * s}, ui::ToU32(c.textMuted), myStatus.c_str());

    // Progress along the bottom edge (inside the rounded corners).
    const float barH = 3 * s;
    dl->PushClipRect({radius * 0.6f, H - barH}, {W - radius * 0.6f, H});
    dl->AddRectFilled({0, H - barH}, {W, H}, ui::ToU32(c.divider));
    dl->AddRectFilled({0, H - barH}, {W * std::max(0.02f, myProgress), H}, ui::ToU32(c.accent));
    dl->PopClipRect();

    ImGui::Render();
    glViewport(0, 0, fbw, fbh);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(myWindow);
}

} // namespace bz::app
