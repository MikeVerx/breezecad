#pragma once

#include <AIS_InteractiveContext.hxx>
#include <AIS_ViewController.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_NeutralWindow.hxx>
#include <Aspect_RenderingContext.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>

#include <TopoDS_Shape.hxx>

#include <GLFW/glfw3.h>

namespace OrdoCAD {

class OcctView : public AIS_ViewController {
public:
    OcctView();
    ~OcctView() override;

    bool Init(GLFWwindow* glfwWindow, int width, int height);
    void Redraw();
    void Resize(int width, int height);

    void OnMouseButton(int button, int action, int mods);
    void OnMouseMove(double x, double y);
    void OnMouseScroll(double xoffset, double yoffset);

    // === Работа с геометрией из документа ===
    void DisplayShape(const TopoDS_Shape& shape);   // добавить форму в сцену
    void ClearAllShapes();                          // убрать всё из сцены
    void FitAll();                                  // вписать всё в вид

private:
    Handle(Aspect_DisplayConnection) myDisplayConnection;
    Handle(OpenGl_GraphicDriver)     myDriver;
    Handle(V3d_Viewer)               myViewer;
    Handle(V3d_View)                 myView;
    Handle(AIS_InteractiveContext)   myContext;
    Handle(Aspect_NeutralWindow)     myWindow;

    GLFWwindow* myGlfwWindow = nullptr;
    int myWidth = 0;
    int myHeight = 0;

    Aspect_VKeyMouse myMouseButtons = Aspect_VKeyMouse_NONE;
    Aspect_VKeyFlags myMouseFlags   = Aspect_VKeyFlags_NONE;
};

} // namespace OrdoCAD
