#include "occt/OcctView.h"

#include <Prs3d_Drawer.hxx>
#include <Prs3d_TypeOfHighlight.hxx>
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
    myDriver->ChangeOptions().buffersNoSwap = true;

    myViewer = new V3d_Viewer(myDriver);
    myViewer->SetDefaultLights();
    myViewer->SetLightOn();

    myContext = new AIS_InteractiveContext(myViewer);
    myContext->SetDisplayMode(AIS_Shaded, true);

    Handle(Prs3d_Drawer) aDrawer = myContext->DefaultDrawer();
    aDrawer->SetFaceBoundaryDraw(true);
    aDrawer->SetFaceBoundaryAspect(
        new Prs3d_LineAspect(Quantity_NOC_BLACK, Aspect_TOL_SOLID, 1.5));

    // Явно задаём цвета выделения (на случай, если тема не задаёт)
    myContext->HighlightStyle(Prs3d_TypeOfHighlight_LocalSelected)
        ->SetColor(Quantity_NOC_GREEN);
    myContext->HighlightStyle(Prs3d_TypeOfHighlight_LocalDynamic)
        ->SetColor(Quantity_NOC_YELLOW);

    myView = myViewer->CreateView();
    myView->SetImmediateUpdate(false);

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
    myView->Redraw();

    return true;
}

void OcctView::DisplayShape(const TopoDS_Shape& shape, const TDF_Label& label) {
    if (shape.IsNull()) return;
    Handle(AIS_Shape) ais = new AIS_Shape(shape);
    // Делаем объект выбираемым
    myContext->Display(ais, AIS_Shaded, 0, false);
    myContext->Activate(ais, 0);  // selection mode 0 — whole shape
    myShapeEntries.push_back({ label, ais });
}

void OcctView::ClearAllShapes() {
    if (myContext.IsNull()) return;
    myContext->RemoveAll(false);
    myShapeEntries.clear();
}

TDF_Label OcctView::GetSelectedLabel() const {
    if (myContext.IsNull()) return TDF_Label();
    if (myContext->NbSelected() == 0) return TDF_Label();

    myContext->InitSelected();
    if (!myContext->MoreSelected()) return TDF_Label();

    Handle(AIS_InteractiveObject) obj = myContext->SelectedInteractive();
    if (obj.IsNull()) return TDF_Label();

    for (const auto& e : myShapeEntries) {
        if (e.Shape == obj) return e.Label;
    }
    return TDF_Label();
}

void OcctView::ClearSelection() {
    if (myContext.IsNull()) return;
    myContext->ClearSelected(false);
    myContext->UpdateCurrentViewer();
    if (!myView.IsNull()) myView->Redraw();
}

void OcctView::FitAll() {
    if (myView.IsNull()) return;
    myView->FitAll(0.01, false);
    myView->ZFitAll();
    myView->Redraw();
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

// Преобразование координат мыши GLFW (пиксели окна) в координаты framebuffer
// (учитывает HiDPI-масштабирование)
static void CursorToFb(GLFWwindow* w, double x, double y, int& outX, int& outY) {
    int winW = 1, winH = 1;
    glfwGetWindowSize(w, &winW, &winH);
    int fbW = 1, fbH = 1;
    glfwGetFramebufferSize(w, &fbW, &fbH);

    double scaleX = (winW > 0) ? (double)fbW / winW : 1.0;
    double scaleY = (winH > 0) ? (double)fbH / winH : 1.0;

    outX = (int)(x * scaleX);
    outY = (int)(y * scaleY);
}

void OcctView::OnMouseButton(int button, int action, int /*mods*/) {
    double xpos, ypos;
    glfwGetCursorPos(myGlfwWindow, &xpos, &ypos);

    int px, py;
    CursorToFb(myGlfwWindow, xpos, ypos, px, py);

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            myLeftPressed = true;
            myPressX = xpos;
            myPressY = ypos;
        } else if (action == GLFW_RELEASE) {
            if (myLeftPressed) {
                double dx = xpos - myPressX;
                double dy = ypos - myPressY;
                if (dx*dx + dy*dy < 25.0) {
                    // Это клик — пробуем выделить
                    myContext->ClearSelected(false);
                    myContext->MoveTo(px, py, myView, false);

                    Handle(AIS_InteractiveObject) detected = myContext->DetectedInteractive();
                    if (!detected.IsNull()) {
                        myContext->AddSelect(detected);
                        std::cout << "Selection: detected and added, nb=" 
                                  << myContext->NbSelected() << "\n";
                    } else {
                        std::cout << "Selection: nothing at (" << px << "," << py << ")\n";
                    }
                    myContext->UpdateCurrentViewer();
                    myView->Redraw();
                }
                myLeftPressed = false;
            }
        }
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            myRotating = true;
            myLastX = xpos;
            myLastY = ypos;
            myView->StartRotation(px, py);
        } else if (action == GLFW_RELEASE) {
            myRotating = false;
        }
    } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (action == GLFW_PRESS) {
            myPanning = true;
            myLastX = xpos;
            myLastY = ypos;
        } else if (action == GLFW_RELEASE) {
            myPanning = false;
        }
    }
}

void OcctView::OnMouseMove(double x, double y) {
    int px, py;
    CursorToFb(myGlfwWindow, x, y, px, py);

    if (myRotating) {
        myView->Rotation(px, py);
        myView->Redraw();
    } else if (myPanning) {
        // Панорамирование — в пикселях framebuffer
        int lastPx, lastPy;
        CursorToFb(myGlfwWindow, myLastX, myLastY, lastPx, lastPy);
        myView->Pan(px - lastPx, -(py - lastPy));
        myView->Redraw();
    }
    myLastX = x;
    myLastY = y;
}

void OcctView::OnMouseScroll(double /*xoffset*/, double yoffset) {
    double xpos, ypos;
    glfwGetCursorPos(myGlfwWindow, &xpos, &ypos);

    int px, py;
    CursorToFb(myGlfwWindow, xpos, ypos, px, py);

    myView->StartZoomAtPoint(px, py);
    myView->ZoomAtPoint(px, py, px, py + (int)(yoffset * 40));
    myView->Redraw();
}

} // namespace OrdoCAD
