#pragma once

#include "platform/glfw/GlfwWindow.h"
#include "occt/OcctView.h"
#include "doc/OrdoDocument.h"

#include <memory>

namespace OrdoCAD {

class Application {
public:
    Application();
    ~Application();

    void Run();

private:
    std::unique_ptr<GlfwWindow>   myWindow;
    std::unique_ptr<OcctView>     myOcctView;
    std::unique_ptr<OrdoDocument> myDocument;

    void DrawDocumentTree();
};

} // namespace OrdoCAD
