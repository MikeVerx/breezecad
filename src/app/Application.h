#pragma once

#include "platform/glfw/GlfwWindow.h"
#include <memory>

namespace OrdoCAD {

class Application {
public:
    Application();
    ~Application();

    void Run();

private:
    std::unique_ptr<GlfwWindow> myWindow;
};

} // namespace OrdoCAD
