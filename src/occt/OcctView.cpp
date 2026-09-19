#include "occt/OcctView.h"

#include <AIS_Shape.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <NCollection_Vec2.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Quantity_Color.hxx>

#ifdef _WIN32
  #define GLFW_EXPOSE_NATIVE_WIN32
  #define GLFW_EXPOSE_NATIVE_WGL
#elif defined(__APPLE__)
  #define GLFW_EXPOSE_NATIVE_COCOA
  #define GLFW_EXPOSE_NATIVE_NSGL
#else
  #define GLFW_EXPOSE_NATIVE_X11
  #define GLFW_EXPOSE_NATIVE_GLX
#endif

#include <GLFW/glfw3native.h>

#include <iostream>

namespace OrdoCAD {

OcctView::OcctView() = default;

OcctView::~OcctView() = default;

bool OcctView::Init(GLFWwindow* glfwWindow, int width, int height) {
    myGlfwWindow = glfwWindow;
    myWidth  = width;
    myHeight = height;

    glfwMakeContextCurrent(myGlfwWindow);

    myDisplayConnection = new Aspect_DisplayConnection();
    myDriver = new OpenGl_GraphicDriver(myDisplayConnection);
    myDriver->ChangeOptions().buffersNoSwap = true;  // GLFW сам делает SwapBuffers

    myViewer = new V3d_Viewer(myDriver);
    myViewer->SetDefaultLights();
    myViewer->SetLightOn();

    myContext = new AIS_InteractiveContext(myViewer);
    myContext->SetDisplayMode(AIS_Shaded, true);

    Handle(Prs3d_Drawer) aDrawer = myContext->DefaultDrawer();
    aDrawer->SetFaceBoundaryDraw(true);
    aDrawer->SetFaceBoundaryAspect(
        new Prs3d_LineAspect(Quantity_NOC_BLACK, Aspect_TOL_SOLID, 1.5));

    myView = myViewer->CreateView();
    myView->SetImmediateUpdate(false);  // не даём OCCT самому делать swap/redraw

    myWindow = new Aspect_NeutralWindow();
    myWindow->SetVirtual(true);

    Aspect_Drawable nativeHandle = 0;
#ifdef _WIN32
    nativeHandle = (Aspect_Drawable)glfwGetWin32Window(myGlfwWindow);
#elif defined(__APPLE__)
    nativeHandle = (Aspect_Drawable)glfwGetCocoaWindow(myGlfwWindow);
#else
    nativeHandle = (Aspect_Drawable)glfwGetX11Window(myGlfwWindow);
#endif

    myWindow->SetNativeHandle(nativeHandle);

    int fbWidth = 0, fbHeight = 0;
    glfwGetFramebufferSize(myGlfwWindow, &fbWidth, &fbHeight);
    myWindow->SetSize(fbWidth, fbHeight);

    Aspect_RenderingContext nativeGlCtx = nullptr;
#ifdef _WIN32
    nativeGlCtx = (Aspect_RenderingContext)glfwGetWGLContext(myGlfwWindow);
#elif defined(__APPLE__)
    nativeGlCtx = (Aspect_RenderingContext)glfwGetNSGLContext(myGlfwWindow);
#else
    nativeGlCtx = (Aspect_RenderingContext)glfwGetGLXContext(myGlfwWindow);
#endif

    if (!nativeGlCtx) {
        std::cerr << "OcctView: failed to get native GL context from GLFW\n";
        return false;
    }

    myView->SetWindow(myWindow, nativeGlCtx);

    if (!myWindow->IsMapped()) {
        myWindow->Map();
    }

    myView->MustBeResized();
    CreateTestShape();
    myView->FitAll(0.01, false);
    myView->ZFitAll();
    myView->Redraw();

    return true;
}

void OcctView::CreateTestShape() {
    TopoDS_Shape box = BRepPrimAPI_MakeBox(100.0, 100.0, 100.0).Shape();
    Handle(AIS_Shape) aisBox = new AIS_Shape(box);
    myContext->Display(aisBox, AIS_Shaded, 0, false);
}

void OcctView::Redraw() {
    if (!myView.IsNull()) {
        myView->Redraw();
    }
}

void OcctView::Resize(int width, int height) {
    myWidth  = width;
    myHeight = height;
    if (myWindow.IsNull() || myView.IsNull()) return;
    myWindow->SetSize(width, height);
    myView->MustBeResized();
}

void OcctView::OnMouseButton(int button, int action, int mods) {
    Aspect_VKeyMouse btn = Aspect_VKeyMouse_NONE;
    switch (button) {
        case GLFW_MOUSE_BUTTON_LEFT:   btn = Aspect_VKeyMouse_LeftButton; break;
        case GLFW_MOUSE_BUTTON_MIDDLE: btn = Aspect_VKeyMouse_MiddleButton; break;
        case GLFW_MOUSE_BUTTON_RIGHT:  btn = Aspect_VKeyMouse_RightButton; break;
        default: return;
    }

    if (action == GLFW_PRESS) {
        myMouseButtons |= btn;
    } else if (action == GLFW_RELEASE) {
        myMouseButtons = static_cast<Aspect_VKeyMouse>(myMouseButtons & ~btn);
    } else {
        return;
    }

    myMouseFlags = Aspect_VKeyFlags_NONE;
    if (mods & GLFW_MOD_SHIFT)   myMouseFlags |= Aspect_VKeyFlags_SHIFT;
    if (mods & GLFW_MOD_CONTROL) myMouseFlags |= Aspect_VKeyFlags_CTRL;
    if (mods & GLFW_MOD_ALT)     myMouseFlags |= Aspect_VKeyFlags_ALT;
    if (mods & GLFW_MOD_SUPER)   myMouseFlags |= Aspect_VKeyFlags_META;

    double xpos, ypos;
    glfwGetCursorPos(myGlfwWindow, &xpos, &ypos);
    NCollection_Vec2<int> pos((int)xpos, (int)ypos);

    UpdateMouseButtons(pos, myMouseButtons, myMouseFlags, false);
    FlushViewEvents(myContext, myView, true);
}

void OcctView::OnMouseMove(double x, double y) {
    NCollection_Vec2<int> pos((int)x, (int)y);
    UpdateMousePosition(pos, myMouseButtons, myMouseFlags, false);
    FlushViewEvents(myContext, myView, true);
}

void OcctView::OnMouseScroll(double /*xoffset*/, double yoffset) {
    double xpos, ypos;
    glfwGetCursorPos(myGlfwWindow, &xpos, &ypos);
    NCollection_Vec2<int> pos((int)xpos, (int)ypos);

    Aspect_ScrollDelta scrollDelta(pos, yoffset);
    UpdateMouseScroll(scrollDelta);
    FlushViewEvents(myContext, myView, true);
}

} // namespace OrdoCAD
