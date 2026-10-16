#pragma once

#include "core/NativeSurface.h"

#include <filesystem>
#include <functional>
#include <string>

struct GLFWwindow;

namespace bz::platform {

/// glfwInit with the hints this program needs; every window starts with it.
/// On Linux GLFW 3.4+ would prefer Wayland, but OCCT draws into an X11
/// window, so X11 (XWayland on Wayland desktops) is asked for.
bool InitGlfw();

struct WindowConfig {
    int         width     = 1440;
    int         height    = 900;
    int         x = -1, y = -1;   ///< -1: centred on the primary monitor
    bool        maximized = true;
    bool        visible   = true;   ///< false: shown later with Window::Show (after the splash)
    std::string title     = "BreezeCAD";
};

/// The main application window with an OpenGL 3.3 context (desktop, GLFW;
/// Window.cpp) or OpenGL ES 3 (Android, EGL; Window_android.cpp).
class Window {
public:
    Window() = default;
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    /// Creates the window; returns false (and logs why) on failure.
    bool Create(const WindowConfig& config);

    GLFWwindow* Handle() const { return myWindow; }
    /// OS handles of the window and its OpenGL context, for OCCT.
    NativeGlSurface NativeSurface() const;
    bool  ShouldClose() const;
    void  SetShouldClose(bool close);
    void  SetTitle(const std::string& title);
    /// Window icon from RGBA pixels (size x size).
    void  SetIcon(const unsigned char* rgba, int size);
    void  SwapBuffers();
    /// UI scale of the monitor the window is on (1.0 = 96 dpi).
    float ContentScale() const;
    /// Usable area of the monitor the window is on (without the taskbar), in screen coordinates.
    void  MonitorWorkArea(int& x, int& y, int& width, int& height) const;
    /// The windowed place and size, and whether it is maximized now.
    void  Geometry(int& x, int& y, int& width, int& height, bool& maximized) const;
    bool  IsMinimized() const;
    /// "4.6.0 NVIDIA …" as the driver reports it; empty before Create.
    const std::string& OpenGLVersion() const { return myGLVersion; }
    /// Graphics card and driver as OpenGL names them (for reports).
    const std::string& OpenGLVendor() const { return myGLVendor; }
    const std::string& OpenGLRenderer() const { return myGLRenderer; }
    /// Shows a window created hidden (maximized then, if it was asked to be).
    void  Show();
    /// Makes the window's own drawable current again if something else took
    /// it (OCCT does, on Linux). Call before drawing into the window. True
    /// when it had to: the caller may log the first time.
    bool  ReclaimDrawable();

    /// Waits for input; returns early after `timeoutSeconds`.
    static void WaitEvents(double timeoutSeconds);
    static void PollEvents();
    /// Wakes WaitEvents up; any thread.
    static void PostEmptyEvent();

    /// Seconds since the program started (glfwGetTime on the desktop).
    static double Time();
    /// Pixels of the drawable per window coordinate of the window whose
    /// context is current (2 on a Retina Mac, 1 elsewhere on the desktop;
    /// the screen density on Android, where coordinates are dp).
    static float CurrentFramebufferScale();
    /// The window in its own coordinates and in drawable pixels.
    void WindowSize(int& width, int& height) const;
    void FramebufferSize(int& width, int& height) const;
    /// False while there is nothing to draw into (Android: the app is in
    /// the background and its surface is gone). Always true on the desktop.
    bool CanDraw() const;

#if defined(__ANDROID__)
    /// Every input event of the window (an AInputEvent*); returns 1 when
    /// taken. Set once by the application.
    void SetInputHandler(std::function<int(const void* event)> handler);
    /// Called on the window's thread when the surface came back or the
    /// window changed size or configuration (density, keyboard, rotation).
    void SetChangeHandler(std::function<void()> handler);
    /// Screen density: pixels per dp.
    float Density() const { return myDensity; }
#endif

private:
#if defined(__ANDROID__)
    bool CreateSurface();
    void DestroySurface();
    void OnCommand(int cmd);
    friend struct AndroidWindowAccess;
    void* myDisplay = nullptr;   ///< EGLDisplay
    void* myConfig = nullptr;    ///< EGLConfig
    void* myContext = nullptr;   ///< EGLContext: lives as long as the window object (OCCT's resources are in it)
    void* mySurface = nullptr;   ///< EGLSurface of the current ANativeWindow, null in the background
    void* myPbuffer = nullptr;   ///< a 1x1 EGLSurface: OCCT's own "window" (its view draws into a framebuffer)
    float myDensity = 1.0f;
    bool  myCloseRequested = false;
    std::function<int(const void*)> myInput;
    std::function<void()> myChanged;
#endif
    GLFWwindow* myWindow = nullptr;
    bool        myMaximizeOnShow = false;
    std::string myGLVersion, myGLVendor, myGLRenderer;
};

/// Saves what `window` shows now (its framebuffer) as a binary PPM picture:
/// test runs and CI pictures (BZ_SCREENSHOT).
void SaveFramebufferPpm(GLFWwindow* window, const std::filesystem::path& file);
void SaveFramebufferPpm(const Window& window, const std::filesystem::path& file);

} // namespace bz::platform
