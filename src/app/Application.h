#pragma once

#include "platform/glfw/GlfwWindow.h"
#include "occt/OcctView.h"

#include <memory>

namespace OrdoCAD {

class Application {
public:
    Application();
    ~Application();

    void Run();

private:
    std::unique_ptr<GlfwWindow> myWindow;
    std::unique_ptr<OcctView>   myOcctView;
};

} // namespace OrdoCAD