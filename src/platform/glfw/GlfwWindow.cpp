#include "GlfwWindow.h"
#include <iostream>

namespace OrdoCAD {

GlfwWindow::GlfwWindow(int width, int height, const std::string& title) {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    myWindow = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!myWindow) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return;
    }

    glfwMakeContextCurrent(myWindow);
}

GlfwWindow::~GlfwWindow() {
    if (myWindow) {
        glfwDestroyWindow(myWindow);
    }
    glfwTerminate();
}

bool GlfwWindow::ShouldClose() const {
    return glfwWindowShouldClose(myWindow);
}

void GlfwWindow::PollEvents() {
    glfwPollEvents();
}

void GlfwWindow::SwapBuffers() {
    glfwSwapBuffers(myWindow);
}

} // namespace OrdoCAD
