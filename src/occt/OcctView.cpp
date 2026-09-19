#include "occt/OcctView.h"

#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <AIS_SelectionScheme.hxx>
#include <AIS_SelectionModesConcurrency.hxx>
#include <AIS_SelectionModesConcurrency.hxx>
#include <Quantity_Color.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <Geom_Plane.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pln.hxx>
#include <gp_Lin.hxx>
#include <Aspect_TypeOfLine.hxx>
#include <Prs3d_Presentation.hxx>

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

    // Двустороннее освещение — плоскости видны с обеих сторон
    myViewer->SetDefaultLights();
    myViewer->SetLightOn();
    myContext->SetDisplayMode(AIS_Shaded, true);

    Handle(Prs3d_Drawer) aDrawer = myContext->DefaultDrawer();
    aDrawer->SetFaceBoundaryDraw(true);
    aDrawer->SetFaceBoundaryAspect(
        new Prs3d_LineAspect(Quantity_NOC_BLACK, Aspect_TOL_SOLID, 1.5));

    // === Настройка подсветки выделения ===
    // 1) Динамическая подсветка (под курсором) — жёлтая заливка
    {
        Handle(Prs3d_Drawer) style = myContext->HighlightStyle(Prs3d_TypeOfHighlight_LocalDynamic);
        style->SetDisplayMode(AIS_Shaded);
        style->SetColor(Quantity_NOC_YELLOW);
        style->SetTransparency(0.3);
        style->SetFaceBoundaryDraw(false);
        style->SetShadingAspect(new Prs3d_ShadingAspect());
        style->ShadingAspect()->SetColor(Quantity_NOC_YELLOW);
        style->ShadingAspect()->SetTransparency(0.3);
    }
    // 2) Подсветка выбранного — зелёная заливка
    {
        Handle(Prs3d_Drawer) style = myContext->HighlightStyle(Prs3d_TypeOfHighlight_LocalSelected);
        style->SetDisplayMode(AIS_Shaded);
        style->SetColor(Quantity_NOC_GREEN);
        style->SetTransparency(0.3);
        style->SetFaceBoundaryDraw(false);
        style->SetShadingAspect(new Prs3d_ShadingAspect());
        style->ShadingAspect()->SetColor(Quantity_NOC_GREEN);
        style->ShadingAspect()->SetTransparency(0.3);
    }
    // 3) Подсветка всей группы (для родителей дерева) — тоже зелёная
    {
        Handle(Prs3d_Drawer) style = myContext->HighlightStyle(Prs3d_TypeOfHighlight_Selected);
        style->SetDisplayMode(AIS_Shaded);
        style->SetColor(Quantity_NOC_GREEN);
    }

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
        std::cerr << "OcctView: failed to get native GL context\n";
        return false;
    }

    myView->SetWindow(myWindow, nativeGlCtx);
    if (!myWindow->IsMapped()) myWindow->Map();
    myView->MustBeResized();
    myView->Redraw();
    return true;
}

void OcctView::DisplayShape(const TopoDS_Shape& shape,
                             const TDF_Label& label,
                             bool isSystem) {
    if (shape.IsNull()) return;

    Handle(AIS_Shape) ais = new AIS_Shape(shape);

    // Устанавливаем display mode ДО Display
    ais->SetDisplayMode(AIS_Shaded);

    if (isSystem) {
        Quantity_Color grey(0.55, 0.55, 0.55, Quantity_TOC_RGB);

        Handle(Prs3d_Drawer) drawer = new Prs3d_Drawer();
        drawer->SetFaceBoundaryDraw(true);
        drawer->SetFaceBoundaryAspect(
            new Prs3d_LineAspect(grey, Aspect_TOL_SOLID, 1.0));
        drawer->SetLineAspect(
            new Prs3d_LineAspect(grey, Aspect_TOL_SOLID, 1.0));
        drawer->SetShadingAspect(new Prs3d_ShadingAspect());
        drawer->ShadingAspect()->SetColor(grey);
        // Прозрачность и на drawer, и на shape
        drawer->ShadingAspect()->SetTransparency(0.95f);
        drawer->SetTransparency(0.95f);
        ais->SetAttributes(drawer);
        ais->SetTransparency(0.95f);
    }

    myContext->Display(ais, false);

    // Применяем transparency через контекст (более надёжно, чем через ais)
    if (isSystem) {
        myContext->SetTransparency(ais, 0.95f, false);
    }

    // Активируем режимы выделения
    if (isSystem && shape.ShapeType() == TopAbs_FACE) {
        // Для плоскостей — режим Face (4)
        myContext->Activate(ais, 4);
        // Явно включаем этот режим для selection
        myContext->SetSelectionModeActive(ais, 4, true,
            AIS_SelectionModesConcurrency_Single);
    } else {
        myContext->Activate(ais, 0);
    }

    myShapeEntries.push_back({ label, ais, isSystem });
}

void OcctView::ClearAllShapes() {
    if (myContext.IsNull()) return;
    myContext->RemoveAll(false);
    myShapeEntries.clear();
}

void OcctView::SetShapeVisible(const TDF_Label& label, bool visible) {
    if (myContext.IsNull()) return;
    for (auto& e : myShapeEntries) {
        if (e.Label.IsEqual(label)) {
            if (visible) {
                myContext->Display(e.Shape, false);
            } else {
                myContext->Erase(e.Shape, false);
            }
            myContext->UpdateCurrentViewer();
            if (!myView.IsNull()) myView->Redraw();
            return;
        }
    }
}

void OcctView::BuildSketchGrid(const gp_Dir& normal, const gp_Pnt& origin) {
    // Строим локальную систему координат на плоскости:
    // Z = нормаль, X и Y — произвольные перпендикулярные
    gp_Ax3 ax(origin, normal);
    gp_Dir uDir = ax.XDirection();
    gp_Dir vDir = ax.YDirection();

    const int    nLines = 20;       // -20..+20
    const double step   = 10.0;     // шаг 10 мм
    const double ext    = nLines * step;

    // Осевые линии (X и Y) — красная и зелёная
    TopoDS_Shape xAxis = BRepBuilderAPI_MakeEdge(
        gp_Lin(origin, uDir), -ext, ext).Shape();
    TopoDS_Shape yAxis = BRepBuilderAPI_MakeEdge(
        gp_Lin(origin, vDir), -ext, ext).Shape();

    Handle(AIS_Shape) xAis = new AIS_Shape(xAxis);
    Handle(AIS_Shape) yAis = new AIS_Shape(yAxis);

    Handle(Prs3d_Drawer) xDrawer = new Prs3d_Drawer();
    xDrawer->SetLineAspect(new Prs3d_LineAspect(Quantity_NOC_RED, Aspect_TOL_SOLID, 2.0));
    xAis->SetAttributes(xDrawer);

    Handle(Prs3d_Drawer) yDrawer = new Prs3d_Drawer();
    yDrawer->SetLineAspect(new Prs3d_LineAspect(Quantity_NOC_GREEN, Aspect_TOL_SOLID, 2.0));
    yAis->SetAttributes(yDrawer);

    // Добавляем в контекст как временные (не выбираемые)
    myContext->Display(xAis, false);
    myContext->Display(yAis, false);
    myContext->Deactivate(xAis);
    myContext->Deactivate(yAis);

    // Сетка — набор тонких серых линий, параллельных осям
    Quantity_Color grey(0.35, 0.35, 0.35, Quantity_TOC_RGB);

    // Линии параллельно X
    for (int i = -nLines; i <= nLines; i++) {
        if (i == 0) continue;
        gp_Pnt start = origin.Translated(gp_Vec(uDir) * -ext + gp_Vec(vDir) * (i * step));
        gp_Pnt end   = origin.Translated(gp_Vec(uDir) *  ext + gp_Vec(vDir) * (i * step));
        TopoDS_Shape line = BRepBuilderAPI_MakeEdge(start, end).Shape();
        Handle(AIS_Shape) ais = new AIS_Shape(line);
        Handle(Prs3d_Drawer) dr = new Prs3d_Drawer();
        dr->SetLineAspect(new Prs3d_LineAspect(grey, Aspect_TOL_SOLID, 1.0));
        ais->SetAttributes(dr);
        myContext->Display(ais, false);
        myContext->Deactivate(ais);
    }
    // Линии параллельно Y
    for (int i = -nLines; i <= nLines; i++) {
        if (i == 0) continue;
        gp_Pnt start = origin.Translated(gp_Vec(vDir) * -ext + gp_Vec(uDir) * (i * step));
        gp_Pnt end   = origin.Translated(gp_Vec(vDir) *  ext + gp_Vec(uDir) * (i * step));
        TopoDS_Shape line = BRepBuilderAPI_MakeEdge(start, end).Shape();
        Handle(AIS_Shape) ais = new AIS_Shape(line);
        Handle(Prs3d_Drawer) dr = new Prs3d_Drawer();
        dr->SetLineAspect(new Prs3d_LineAspect(grey, Aspect_TOL_SOLID, 1.0));
        ais->SetAttributes(dr);
        myContext->Display(ais, false);
        myContext->Deactivate(ais);
    }

    myContext->UpdateCurrentViewer();
}

void OcctView::EnterSketchMode(const gp_Dir& planeNormal, const gp_Pnt& planeOrigin) {
    if (myInSketchMode) return;
    myInSketchMode = true;

    // 1. Работаем с камерой напрямую
    Handle(Graphic3d_Camera) cam = myView->Camera();

    // Точка, куда смотрит камера — центр плоскости
    cam->SetCenter(gp_Pnt(planeOrigin.X(), planeOrigin.Y(), planeOrigin.Z()));

    // Направление взгляда — вдоль нормали (камера смотрит СТОРОНА -normal)
    cam->SetDirection(gp_Dir(-planeNormal.X(), -planeNormal.Y(), -planeNormal.Z()));

    // "Верх" камеры — X-ось плоскости (или Y, если нормаль параллельна X)
    gp_Ax3 ax(planeOrigin, planeNormal);
    cam->SetUp(ax.YDirection());

    // Расстояние до объекта
    cam->SetDistance(500.0);
    cam->SetScale(3.0);

    // 2. Ортогональная проекция
    cam->SetProjectionType(Graphic3d_Camera::Projection_Orthographic);

    // 3. Строим сетку
    BuildSketchGrid(planeNormal, planeOrigin);

    // 4. Вписываем вид
    myView->FitAll(0.01, false);
    myView->Redraw();

    std::cout << "OcctView: entered sketch mode\n";
}

void OcctView::ExitSketchMode() {
    if (!myInSketchMode) return;
    myInSketchMode = false;

    // 1. Возвращаем перспективу
    myView->Camera()->SetProjectionType(Graphic3d_Camera::Projection_Perspective);

    // 2. Убираем сетку и осевые линии.
    //    Просто очищаем весь контекст и заново показываем только те объекты,
    //    что зарегистрированы в myShapeEntries.
    myContext->RemoveAll(false);
    for (auto& e : myShapeEntries) {
        myContext->Display(e.Shape, false);
        if (e.IsSystem) {
            myContext->SetTransparency(e.Shape, 0.95f, false);
            if (e.Shape->Shape().ShapeType() == TopAbs_FACE) {
                myContext->Activate(e.Shape, 4);
            } else {
                myContext->Activate(e.Shape, 0);
            }
        } else {
            myContext->Activate(e.Shape, 0);
        }
    }
    myContext->UpdateCurrentViewer();

    // 3. Возвращаем камеру
    myView->FitAll(0.01, false);
    myView->ZFitAll();
    myView->Redraw();

    std::cout << "OcctView: exited sketch mode\n";
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
    if (!myView.IsNull()) myView->Redraw();
}

void OcctView::Resize(int width, int height) {
    myWidth  = width;
    myHeight = height;
    if (myWindow.IsNull() || myView.IsNull()) return;
    myWindow->SetSize(width, height);
    myView->MustBeResized();
}

static void CursorToFb(GLFWwindow* w, double x, double y, int& outX, int& outY) {
    int winW = 1, winH = 1, fbW = 1, fbH = 1;
    glfwGetWindowSize(w, &winW, &winH);
    glfwGetFramebufferSize(w, &fbW, &fbH);
    double sx = (winW > 0) ? (double)fbW / winW : 1.0;
    double sy = (winH > 0) ? (double)fbH / winH : 1.0;
    outX = (int)(x * sx);
    outY = (int)(y * sy);
}

void OcctView::OnMouseButton(int button, int action, int /*mods*/) {
    double xpos, ypos;
    glfwGetCursorPos(myGlfwWindow, &xpos, &ypos);
    int px, py;
    CursorToFb(myGlfwWindow, xpos, ypos, px, py);

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            myLeftPressed = true;
            myPressX = xpos; myPressY = ypos;
        } else if (action == GLFW_RELEASE) {
            if (myLeftPressed) {
                double dx = xpos - myPressX;
                double dy = ypos - myPressY;
                if (dx*dx + dy*dy < 25.0) {
                    // Атомарный Select — внутри OCCT делает MoveTo + Detected + Select
                    myContext->MoveTo(px, py, myView, false);
                    myContext->SelectDetected(AIS_SelectionScheme_Replace);
                    myView->Redraw();
                }
                myLeftPressed = false;
            }
        }
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (myInSketchMode) return;  // в режиме эскиза вращение отключено
        if (action == GLFW_PRESS) {
            myRotating = true;
            myLastX = xpos; myLastY = ypos;
            myView->StartRotation(px, py);
        } else if (action == GLFW_RELEASE) {
            myRotating = false;
        }
    } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (action == GLFW_PRESS) {
            myPanning = true;
            myLastX = xpos; myLastY = ypos;
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
        int lpx, lpy;
        CursorToFb(myGlfwWindow, myLastX, myLastY, lpx, lpy);
        myView->Pan(px - lpx, -(py - lpy));
        myView->Redraw();
    }
    myLastX = x; myLastY = y;
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
