#include "platform/Window.h"

#include "core/Log.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <vector>

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
#if defined(__linux__)
    // No alpha channel in the window: it is opaque. With one, X11 gives a
    // 32-bit visual, and a compositor (XWayland under Hyprland) blends the
    // window with what is behind it by that alpha — a window black or
    // see-through wherever alpha went unwritten.
    glfwWindowHint(GLFW_ALPHA_BITS, 0);
#endif

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
    XWindowAttributes attributes{};
    const int depth = XGetWindowAttributes(glfwGetX11Display(), glfwGetX11Window(myWindow), &attributes) ? attributes.depth : 0;
    log::Info("Window system: X11{}, desktop {}, window depth {} bits", wayland && *wayland ? " through XWayland (a Wayland session)" : "",
              desktop && *desktop ? desktop : "unknown", depth);
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

bool Window::ReclaimDrawable() {
#if defined(__linux__)
    // GLFW draws and swaps through a GLXWindow made over the X window; OCCT
    // makes the same context current on the plain X window, a second GLX
    // drawable with buffers of its own. From then on everything went into
    // buffers that are never shown, and the window kept the last picture
    // from before the first 3D render: the screen froze as soon as a
    // document opened, on any Linux (testers on Hyprland, 2026-10; the same
    // on plain X11 with Mesa). OCCT's own check compares the current
    // drawable with the X window, so it takes it again at its next render:
    // this runs every frame, and costs one comparison when nothing changed.
    const GLXWindow ours = glfwGetGLXWindow(myWindow);
    if (!ours || glXGetCurrentDrawable() == ours) return false;
    glXMakeCurrent(glfwGetX11Display(), ours, glfwGetGLXContext(myWindow));
    return true;
#else
    return false;   // one drawable per window: WGL and NSOpenGL have nothing to take back
#endif
}

void SaveFramebufferPpm(GLFWwindow* window, const std::filesystem::path& file) {
    int w = 0, h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    std::vector<unsigned char> pixels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream out(file, std::ios::binary);
    if (!out) return;
    out << "P6\n" << w << ' ' << h << "\n255\n";
    const auto rowBytes = static_cast<std::streamsize>(w) * 3;
    for (int row = h - 1; row >= 0; --row)   // OpenGL's rows go up, the picture's down
        out.write(reinterpret_cast<const char*>(pixels.data()) + static_cast<std::ptrdiff_t>(row) * rowBytes, rowBytes);
}

void Window::WaitEvents(double timeoutSeconds) { glfwWaitEventsTimeout(timeoutSeconds); }
void Window::PollEvents() { glfwPollEvents(); }
void Window::PostEmptyEvent() { glfwPostEmptyEvent(); }

} // namespace bz::platform
