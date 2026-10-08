#pragma once

#include <string>

struct GLFWwindow;

namespace bz::platform {

struct WindowConfig {
    int         width     = 1440;
    int         height    = 900;
    bool        maximized = true;
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
    bool  ShouldClose() const;
    void  SetShouldClose(bool close);
    void  SetTitle(const std::string& title);
    /// Window icon from RGBA pixels (size x size).
    void  SetIcon(const unsigned char* rgba, int size);
    void  SwapBuffers();
    /// UI scale of the monitor the window is on (1.0 = 96 dpi).
    float ContentScale() const;
    bool  IsMinimized() const;

    /// Waits for input; returns early after `timeoutSeconds`.
    static void WaitEvents(double timeoutSeconds);
    static void PollEvents();

private:
    GLFWwindow* myWindow = nullptr;
};

} // namespace bz::platform
