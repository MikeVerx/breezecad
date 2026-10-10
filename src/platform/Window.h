#pragma once

#include "core/NativeSurface.h"

#include <string>

struct GLFWwindow;

namespace bz::platform {

struct WindowConfig {
    int         width     = 1440;
    int         height    = 900;
    int         x = -1, y = -1;   ///< -1: centred on the primary monitor
    bool        maximized = true;
    bool        visible   = true;   ///< false: shown later with Window::Show (after the splash)
    std::string title     = "BreezeCAD";
};

/// The main application window with an OpenGL 3.3 context.
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
    /// Shows a window created hidden (maximized then, if it was asked to be).
    void  Show();

    /// Waits for input; returns early after `timeoutSeconds`.
    static void WaitEvents(double timeoutSeconds);
    static void PollEvents();

private:
    GLFWwindow* myWindow = nullptr;
    bool        myMaximizeOnShow = false;
};

} // namespace bz::platform
