#include "platform/Window.h"

#include "core/Log.h"

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
#endif
    return surface;
}

Window::~Window() {
    if (myWindow) glfwDestroyWindow(myWindow);
    glfwTerminate();
}

bool Window::Create(const WindowConfig& config) {
    glfwSetErrorCallback(OnGlfwError);
    if (!glfwInit()) {
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

    myWindow = glfwCreateWindow(config.width, config.height, config.title.c_str(), nullptr, nullptr);
    if (!myWindow) {
        log::Error("Cannot create a window with OpenGL 3.3. Is the graphics driver installed?");
        return false;
    }
    glfwMakeContextCurrent(myWindow);
    glfwSwapInterval(1);
    glfwSetWindowSizeLimits(myWindow, 960, 600, GLFW_DONT_CARE, GLFW_DONT_CARE);
    log::Info("Window: OpenGL {}", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
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

bool Window::IsMinimized() const { return glfwGetWindowAttrib(myWindow, GLFW_ICONIFIED) != 0; }

void Window::WaitEvents(double timeoutSeconds) { glfwWaitEventsTimeout(timeoutSeconds); }
void Window::PollEvents() { glfwPollEvents(); }

} // namespace bz::platform
