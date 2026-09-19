#include "platform/glfw/GlfwWindow.h"
#include <iostream>

namespace OrdoCAD {

namespace {
GlfwWindow* Self(GLFWwindow* w) {
    return static_cast<GlfwWindow*>(glfwGetWindowUserPointer(w));
}
}

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
    glfwSwapInterval(1);  // VSync — убирает мигание при быстрой перерисовке
    glfwSetWindowUserPointer(myWindow, this);

    glfwSetMouseButtonCallback(myWindow, [](GLFWwindow* w, int b, int a, int m) {
        if (auto s = Self(w); s && s->myMouseButtonCb) s->myMouseButtonCb(b, a, m);
    });
    glfwSetCursorPosCallback(myWindow, [](GLFWwindow* w, double x, double y) {
        if (auto s = Self(w); s && s->myCursorPosCb) s->myCursorPosCb(x, y);
    });
    glfwSetScrollCallback(myWindow, [](GLFWwindow* w, double x, double y) {
        if (auto s = Self(w); s && s->myScrollCb) s->myScrollCb(x, y);
    });
    glfwSetFramebufferSizeCallback(myWindow, [](GLFWwindow* w, int width, int height) {
        if (auto s = Self(w); s && s->myResizeCb) s->myResizeCb(width, height);
    });
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

void GlfwWindow::SetMouseButtonCallback(MouseButtonCallback cb) { myMouseButtonCb = std::move(cb); }
void GlfwWindow::SetCursorPosCallback(CursorPosCallback cb)     { myCursorPosCb   = std::move(cb); }
void GlfwWindow::SetScrollCallback(ScrollCallback cb)           { myScrollCb      = std::move(cb); }
void GlfwWindow::SetResizeCallback(ResizeCallback cb)           { myResizeCb      = std::move(cb); }

void GlfwWindow::ToggleFullscreen() {
    if (!myWindow) return;

    if (!myIsFullscreen) {
        // Запоминаем текущее состояние
        glfwGetWindowPos(myWindow, &mySavedX, &mySavedY);
        glfwGetWindowSize(myWindow, &mySavedW, &mySavedH);

        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        if (!monitor) return;
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (!mode) return;

        glfwSetWindowMonitor(myWindow, monitor,
                             0, 0,
                             mode->width, mode->height,
                             mode->refreshRate);
        myIsFullscreen = true;
    } else {
        glfwSetWindowMonitor(myWindow, nullptr,
                             mySavedX, mySavedY,
                             mySavedW, mySavedH,
                             0);
        myIsFullscreen = false;
    }
}

void GlfwWindow::Maximize() {
    if (!myWindow) return;
    if (glfwGetWindowAttrib(myWindow, GLFW_MAXIMIZED)) {
        glfwRestoreWindow(myWindow);
    } else {
        glfwMaximizeWindow(myWindow);
    }
}

} // namespace OrdoCAD
