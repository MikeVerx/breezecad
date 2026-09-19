#pragma once

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_NeutralWindow.hxx>
#include <Aspect_RenderingContext.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <TDF_Label.hxx>
#include <TopoDS_Shape.hxx>

#include <GLFW/glfw3.h>

#include <vector>

namespace OrdoCAD {

class OcctView {
public:
    OcctView();
    ~OcctView();

    bool Init(GLFWwindow* glfwWindow, int width, int height);
    void Redraw();
    void Resize(int width, int height);

    void OnMouseButton(int button, int action, int mods);
    void OnMouseMove(double x, double y);
    void OnMouseScroll(double xoffset, double yoffset);

    // isSystem = true → серый цвет, полупрозрачность
    void DisplayShape(const TopoDS_Shape& shape,
                      const TDF_Label& label,
                      bool isSystem = false);
    void ClearAllShapes();
    void FitAll();

    TDF_Label GetSelectedLabel() const;
    void ClearSelection();

private:
    struct ShapeEntry {
        TDF_Label         Label;
        Handle(AIS_Shape) Shape;
        bool              IsSystem;
    };

    Handle(Aspect_DisplayConnection) myDisplayConnection;
    Handle(OpenGl_GraphicDriver)     myDriver;
    Handle(V3d_Viewer)               myViewer;
    Handle(V3d_View)                 myView;
    Handle(AIS_InteractiveContext)   myContext;
    Handle(Aspect_NeutralWindow)     myWindow;

    GLFWwindow* myGlfwWindow = nullptr;
    int myWidth  = 0;
    int myHeight = 0;

    bool   myRotating    = false;
    bool   myPanning     = false;
    bool   myLeftPressed = false;
    double myLastX  = 0.0;
    double myLastY  = 0.0;
    double myPressX = 0.0;
    double myPressY = 0.0;

    std::vector<ShapeEntry> myShapeEntries;
};

} // namespace OrdoCAD
