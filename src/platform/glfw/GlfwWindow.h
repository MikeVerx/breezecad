#pragma once

#include <GLFW/glfw3.h>
#include <string>

namespace OrdoCAD {

class GlfwWindow {
public:
    GlfwWindow(int width, int height, const std::string& title);
    ~GlfwWindow();

    bool ShouldClose() const;
    void PollEvents();
    void SwapBuffers();

    GLFWwindow* GetNativeWindow() const { return myWindow; }

private:
    GLFWwindow* myWindow = nullptr;
};

} // namespace OrdoCAD
