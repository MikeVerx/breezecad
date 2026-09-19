#pragma once

#include <GLFW/glfw3.h>
#include <string>
#include <functional>

namespace OrdoCAD {

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

private:
    GLFWwindow* myWindow = nullptr;

    MouseButtonCallback myMouseButtonCb;
    CursorPosCallback   myCursorPosCb;
    ScrollCallback      myScrollCb;
    ResizeCallback      myResizeCb;
};

} // namespace OrdoCAD
