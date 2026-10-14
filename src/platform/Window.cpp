#include "platform/Window.h"

#include "core/Log.h"

#include <algorithm>
#include <cstdlib>

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WGL
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#define GLFW_EXPOSE_NATIVE_NSGL
#else
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_GLX
#endif
#include <GLFW/glfw3native.h>

namespace bz::platform {
namespace {

void OnGlfwError(int code, const char* message) { log::Error("GLFW {}: {}", code, message); }

} // namespace

bool InitGlfw() {
    glfwSetErrorCallback(OnGlfwError);
#if defined(__linux__) && (GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4))
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
    return glfwInit() == GLFW_TRUE;
}

NativeGlSurface Window::NativeSurface() const {
    NativeGlSurface surface;
    if (!myWindow) return surface;
#if defined(_WIN32)
    surface.window = reinterpret_cast<std::uintptr_t>(glfwGetWin32Window(myWindow));
    surface.glContext = glfwGetWGLContext(myWindow);
#elif defined(__APPLE__)
    // In C++ (not Objective-C) GLFW declares these as plain pointers.
    surface.window = reinterpret_cast<std::uintptr_t>(glfwGetCocoaWindow(myWindow));
    surface.glContext = glfwGetNSGLContext(myWindow);
#else
    surface.window = static_cast<std::uintptr_t>(glfwGetX11Window(myWindow));
    surface.glContext = glfwGetGLXContext(myWindow);
    surface.display = glfwGetX11Display();
#endif
    return surface;
}

Window::~Window() {
    if (myWindow) glfwDestroyWindow(myWindow);
    glfwTerminate();
}

bool Window::Create(const WindowConfig& config) {
    if (!InitGlfw()) {
        log::Error("GLFW init failed");
        return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#if defined(__APPLE__)
    // macOS only offers 3.2+ as a core, forward-compatible profile.
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_MAXIMIZED, config.maximized ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);

    // The windowed size never exceeds the screen (a 1366x768 laptop gets a
    // window that fits), and it opens centred unless a place was remembered.
    int wx = 0, wy = 0, ww = config.width, wh = config.height;
    if (GLFWmonitor* monitor = glfwGetPrimaryMonitor()) glfwGetMonitorWorkarea(monitor, &wx, &wy, &ww, &wh);
    const int width = std::min(config.width, std::max(640, ww * 9 / 10));
    const int height = std::min(config.height, std::max(480, wh * 9 / 10));
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    myWindow = glfwCreateWindow(width, height, config.title.c_str(), nullptr, nullptr);
    if (!myWindow) {
        log::Error("Cannot create a window with OpenGL 3.3. Is the graphics driver installed?");
        return false;
    }
    const bool remembered = config.x >= 0 && config.y >= 0 && config.x < wx + ww - 100 && config.y < wy + wh - 100;
    glfwSetWindowPos(myWindow, remembered ? config.x : wx + (ww - width) / 2, remembered ? config.y : wy + (wh - height) / 2);
    // Maximized only once shown: maximizing a hidden window shows it on some systems.
    myMaximizeOnShow = config.maximized;
    if (config.visible) Show();
    glfwMakeContextCurrent(myWindow);
    glfwSwapInterval(1);
    glfwSetWindowSizeLimits(myWindow, 800, 500, GLFW_DONT_CARE, GLFW_DONT_CARE);
    if (const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION))) myGLVersion = version;
    if (const auto* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR))) myGLVendor = vendor;
    if (const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER))) myGLRenderer = renderer;
    log::Info("Window: OpenGL {}", myGLVersion);
    log::Info("Graphics: {} / {}", myGLVendor, myGLRenderer);
#if defined(__linux__)
    // Wayland sessions run the program through XWayland: reports from them
    // differ, so the log says which one this was.
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    const char* desktop = std::getenv("XDG_CURRENT_DESKTOP");
    log::Info("Window system: X11{}, desktop {}", wayland && *wayland ? " through XWayland (a Wayland session)" : "",
              desktop && *desktop ? desktop : "unknown");
#endif
    return true;
}

bool Window::ShouldClose() const { return glfwWindowShouldClose(myWindow) != 0; }
void Window::SetShouldClose(bool close) { glfwSetWindowShouldClose(myWindow, close ? GLFW_TRUE : GLFW_FALSE); }
void Window::SetTitle(const std::string& title) { glfwSetWindowTitle(myWindow, title.c_str()); }

void Window::SetIcon(const unsigned char* rgba, int size) {
    GLFWimage image{size, size, const_cast<unsigned char*>(rgba)};   // GLFW copies the pixels
    glfwSetWindowIcon(myWindow, 1, &image);
}

void Window::SwapBuffers() { glfwSwapBuffers(myWindow); }

float Window::ContentScale() const {
    float x = 1.0f, y = 1.0f;
    glfwGetWindowContentScale(myWindow, &x, &y);
    return x;
}

void Window::MonitorWorkArea(int& x, int& y, int& width, int& height) const {
    x = y = 0;
    glfwGetWindowSize(myWindow, &width, &height);
    // The monitor holding the window's centre; the primary one otherwise.
    int px = 0, py = 0, pw = 0, ph = 0;
    glfwGetWindowPos(myWindow, &px, &py);
    glfwGetWindowSize(myWindow, &pw, &ph);
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    GLFWmonitor* chosen = glfwGetPrimaryMonitor();
    for (int i = 0; i < count; ++i) {
        int mx = 0, my = 0, mw = 0, mh = 0;
        glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
        const int cx = px + pw / 2, cy = py + ph / 2;
        if (cx >= mx && cx < mx + mw && cy >= my && cy < my + mh) chosen = monitors[i];
    }
    if (chosen) glfwGetMonitorWorkarea(chosen, &x, &y, &width, &height);
}

void Window::Geometry(int& x, int& y, int& width, int& height, bool& maximized) const {
    maximized = glfwGetWindowAttrib(myWindow, GLFW_MAXIMIZED) != 0;
    glfwGetWindowPos(myWindow, &x, &y);
    glfwGetWindowSize(myWindow, &width, &height);
}

void Window::Show() {
    if (myMaximizeOnShow) glfwMaximizeWindow(myWindow);
    myMaximizeOnShow = false;
    glfwShowWindow(myWindow);
}

bool Window::IsMinimized() const { return glfwGetWindowAttrib(myWindow, GLFW_ICONIFIED) != 0; }

void Window::WaitEvents(double timeoutSeconds) { glfwWaitEventsTimeout(timeoutSeconds); }
void Window::PollEvents() { glfwPollEvents(); }
void Window::PostEmptyEvent() { glfwPostEmptyEvent(); }

} // namespace bz::platform
