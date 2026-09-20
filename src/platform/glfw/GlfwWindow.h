#pragma once

#include <GLFW/glfw3.h>
#include <string>
#include <functional>

namespace BreezeCAD {

class GlfwWindow {
public:
    using MouseButtonCallback = std::function<void(int, int, int)>;
    using CursorPosCallback   = std::function<void(double, double)>;
    using ScrollCallback      = std::function<void(double, double)>;
    using ResizeCallback      = std::function<void(int, int)>;

    GlfwWindow(int width, int height, const std::string& title);
    ~GlfwWindow();

    bool ShouldClose() const;
    void PollEvents();
    void SwapBuffers();

    GLFWwindow* GetNativeWindow() const { return myWindow; }

    void SetMouseButtonCallback(MouseButtonCallback cb);
    void SetCursorPosCallback(CursorPosCallback cb);
    void SetScrollCallback(ScrollCallback cb);
    void SetResizeCallback(ResizeCallback cb);

    // F11 — переключение настоящего полноэкранного режима
    void ToggleFullscreen();
    // Развернуть/восстановить в оконном режиме
    void Maximize();
    bool IsFullscreen() const { return myIsFullscreen; }

private:
    GLFWwindow* myWindow = nullptr;
    bool myIsFullscreen = false;
    int mySavedX = 0, mySavedY = 0, mySavedW = 0, mySavedH = 0;

    MouseButtonCallback myMouseButtonCb;
    CursorPosCallback   myCursorPosCb;
    ScrollCallback      myScrollCb;
    ResizeCallback      myResizeCb;
};

} // namespace BreezeCAD
