#include "occt/OcctView.h"

#include <Prs3d_Drawer.hxx>
#include <Prs3d_PointAspect.hxx>
#include <Geom_CartesianPoint.hxx>
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

#include <cmath>
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
        Quantity_Color grey(1.0, 1.0, 1.0, Quantity_TOC_RGB);  // белая сетка

        Handle(Prs3d_Drawer) drawer = new Prs3d_Drawer();
        drawer->SetFaceBoundaryDraw(true);
        drawer->SetFaceBoundaryAspect(
            new Prs3d_LineAspect(grey, Aspect_TOL_SOLID, 1.2));
        drawer->SetLineAspect(
            new Prs3d_LineAspect(grey, Aspect_TOL_SOLID, 1.2));
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

    // Осевые линии (X и Y) — красная и зелёная штрих-пунктирные
    TopoDS_Shape xAxis = BRepBuilderAPI_MakeEdge(
        gp_Lin(origin, uDir), -ext, ext).Shape();
    TopoDS_Shape yAxis = BRepBuilderAPI_MakeEdge(
        gp_Lin(origin, vDir), -ext, ext).Shape();

    Handle(AIS_Shape) xAis = new AIS_Shape(xAxis);
    Handle(AIS_Shape) yAis = new AIS_Shape(yAxis);

    myContext->Display(xAis, false);
    myContext->SetColor(xAis, Quantity_NOC_RED, false);
    myContext->SetWidth(xAis, 2.0, false);
    myContext->Deactivate(xAis);

    myContext->Display(yAis, false);
    myContext->SetColor(yAis, Quantity_NOC_GREEN, false);
    myContext->SetWidth(yAis, 2.0, false);
    myContext->Deactivate(yAis);

    // Сетка — набор тонких серых линий, параллельных осям
    Quantity_Color grey(1.0, 1.0, 1.0, Quantity_TOC_RGB);  // белая сетка

    // Линии параллельно X
    for (int i = -nLines; i <= nLines; i++) {
        if (i == 0) continue;
        gp_Pnt start = origin.Translated(gp_Vec(uDir) * -ext + gp_Vec(vDir) * (i * step));
        gp_Pnt end   = origin.Translated(gp_Vec(uDir) *  ext + gp_Vec(vDir) * (i * step));
        TopoDS_Shape line = BRepBuilderAPI_MakeEdge(start, end).Shape();
        Handle(AIS_Shape) ais = new AIS_Shape(line);
        myContext->Display(ais, false);
        myContext->SetColor(ais, grey, false);
        myContext->SetWidth(ais, 1.5, false);
        myContext->Deactivate(ais);
    }
    // Линии параллельно Y
    for (int i = -nLines; i <= nLines; i++) {
        if (i == 0) continue;
        gp_Pnt start = origin.Translated(gp_Vec(vDir) * -ext + gp_Vec(uDir) * (i * step));
        gp_Pnt end   = origin.Translated(gp_Vec(vDir) *  ext + gp_Vec(uDir) * (i * step));
        TopoDS_Shape line = BRepBuilderAPI_MakeEdge(start, end).Shape();
        Handle(AIS_Shape) ais = new AIS_Shape(line);
        myContext->Display(ais, false);
        myContext->SetColor(ais, grey, false);
        myContext->SetWidth(ais, 1.5, false);
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

    // Сохраняем плоскость эскиза — пригодится для конвертации
    mySketchPlane = gp_Pln(planeOrigin, planeNormal);

    // 3. Строим сетку
    BuildSketchGrid(planeNormal, planeOrigin);

    // 4. Вписываем вид
    myView->FitAll(0.01, false);
    myView->Redraw();

    // 5. Отключаем динамическую подсветку — чтобы плоскости не мерцали под курсором
    myContext->Deactivate();
    myContext->UpdateCurrentViewer();

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

    // 4. Возвращаем селекцию объектов
    for (auto& e : myShapeEntries) {
        if (e.IsSystem && e.Shape->Shape().ShapeType() == TopAbs_FACE) {
            myContext->Activate(e.Shape, 4);
        } else {
            myContext->Activate(e.Shape, 0);
        }
    }
    myContext->UpdateCurrentViewer();

    std::cout << "OcctView: exited sketch mode\n";
}

bool OcctView::ScreenToPlane(int px, int py, gp_Pnt& out) const {
    if (myView.IsNull() || !myInSketchMode) return false;

    // 1. ConvertWithProj: точка на плоскости проекции И направление луча
    double X, Y, Z, Vx, Vy, Vz;
    myView->ConvertWithProj(px, py, X, Y, Z, Vx, Vy, Vz);

    gp_Pnt pointOnProj(X, Y, Z);
    gp_Dir rayDir(Vx, Vy, Vz);

    // 2. Пересечение луча с плоскостью эскиза (вручную)
    gp_Pnt plnLoc  = mySketchPlane.Location();
    gp_Dir plnNorm = mySketchPlane.Axis().Direction();

    double denom = rayDir.Dot(plnNorm);
    if (std::abs(denom) < 1e-9) return false;

    gp_Vec ptToPlane(pointOnProj, plnLoc);
    double t = ptToPlane.Dot(plnNorm) / denom;

    out = gp_Pnt(pointOnProj.X() + rayDir.X() * t,
                 pointOnProj.Y() + rayDir.Y() * t,
                 pointOnProj.Z() + rayDir.Z() * t);
    return true;
}

void OcctView::ShowPreviewLine(const gp_Pnt& from, const gp_Pnt& to) {
    if (myContext.IsNull()) return;

    // Проверка: точки должны различаться
    double dx = to.X() - from.X();
    double dy = to.Y() - from.Y();
    double dz = to.Z() - from.Z();
    if (dx*dx + dy*dy + dz*dz < 1e-6) return;

    try {
        // === Линия-превью ===
        ClearPreviewLine();

        TopoDS_Shape edge = BRepBuilderAPI_MakeEdge(from, to).Shape();
        if (edge.IsNull()) return;
        myPreviewLine = new AIS_Shape(edge);
        Handle(Prs3d_Drawer) dr = new Prs3d_Drawer();
        dr->SetLineAspect(new Prs3d_LineAspect(Quantity_NOC_YELLOW, Aspect_TOL_DASH, 1.5));
        myPreviewLine->SetAttributes(dr);

        myContext->Display(myPreviewLine, false);
        myContext->Deactivate(myPreviewLine);

        // === Маркер стартовой точки (зелёный) ===
        {
            Handle(Geom_Point) gStart = new Geom_CartesianPoint(from);
            myStartMarker = new AIS_Point(gStart);
            Handle(Prs3d_PointAspect) pa = new Prs3d_PointAspect(
                Aspect_TOM_O, Quantity_NOC_GREEN, 3.0);
            Handle(Prs3d_Drawer) dr1 = new Prs3d_Drawer();
            dr1->SetPointAspect(pa);
            myStartMarker->SetAttributes(dr1);
            myContext->Display(myStartMarker, false);
            myContext->Deactivate(myStartMarker);
        }

        // === Маркер конечной точки (жёлтый) ===
        {
            Handle(Geom_Point) gEnd = new Geom_CartesianPoint(to);
            myEndMarker = new AIS_Point(gEnd);
            Handle(Prs3d_PointAspect) pa = new Prs3d_PointAspect(
                Aspect_TOM_O, Quantity_NOC_YELLOW, 3.0);
            Handle(Prs3d_Drawer) dr2 = new Prs3d_Drawer();
            dr2->SetPointAspect(pa);
            myEndMarker->SetAttributes(dr2);
            myContext->Display(myEndMarker, false);
            myContext->Deactivate(myEndMarker);
        }

        myContext->UpdateCurrentViewer();
        if (!myView.IsNull()) myView->Redraw();
    } catch (const std::exception& e) {
        std::cerr << "ShowPreviewLine exception: " << e.what() << "\n";
        myPreviewLine.Nullify();
        myStartMarker.Nullify();
        myEndMarker.Nullify();
    } catch (...) {
        std::cerr << "ShowPreviewLine unknown exception\n";
        myPreviewLine.Nullify();
        myStartMarker.Nullify();
        myEndMarker.Nullify();
    }
}

void OcctView::ShowCursorMarker(const gp_Pnt& p) {
    if (myContext.IsNull()) return;

    try {
        // Чистим предыдущие маркеры
        if (!myStartMarker.IsNull()) {
            myContext->Remove(myStartMarker, false);
            myStartMarker.Nullify();
        }
        if (!myEndMarker.IsNull()) {
            myContext->Remove(myEndMarker, false);
            myEndMarker.Nullify();
        }
        if (!myPreviewLine.IsNull()) {
            myContext->Remove(myPreviewLine, false);
            myPreviewLine.Nullify();
        }

        // Маркер под курсором — жёлтая окружность
        Handle(Geom_Point) gP = new Geom_CartesianPoint(p);
        myEndMarker = new AIS_Point(gP);
        Handle(Prs3d_PointAspect) pa = new Prs3d_PointAspect(
            Aspect_TOM_O, Quantity_NOC_YELLOW, 3.0);
        Handle(Prs3d_Drawer) dr = new Prs3d_Drawer();
        dr->SetPointAspect(pa);
        myEndMarker->SetAttributes(dr);

        myContext->Display(myEndMarker, false);
        myContext->Deactivate(myEndMarker);
        myContext->UpdateCurrentViewer();
        if (!myView.IsNull()) myView->Redraw();
    } catch (...) {
        myEndMarker.Nullify();
    }
}

void OcctView::ClearPreviewLine() {
    if (myContext.IsNull()) return;
    if (!myPreviewLine.IsNull()) {
        myContext->Remove(myPreviewLine, false);
        myPreviewLine.Nullify();
    }
    if (!myStartMarker.IsNull()) {
        myContext->Remove(myStartMarker, false);
        myStartMarker.Nullify();
    }
    if (!myEndMarker.IsNull()) {
        myContext->Remove(myEndMarker, false);
        myEndMarker.Nullify();
    }
    myContext->UpdateCurrentViewer();
}

void OcctView::DisplaySketchLine(const TopoDS_Shape& line, const TDF_Label& label) {
    if (line.IsNull() || myContext.IsNull()) return;

    Handle(AIS_Shape) ais = new AIS_Shape(line);
    Handle(Prs3d_Drawer) dr = new Prs3d_Drawer();
    dr->SetLineAspect(new Prs3d_LineAspect(Quantity_Color(0.65, 0.3, 1.0, Quantity_TOC_RGB), Aspect_TOL_SOLID, 3.5));
    ais->SetAttributes(dr);

    myContext->Display(ais, false);
    myContext->Deactivate(ais);
    myShapeEntries.push_back({ label, ais, false });
    myContext->UpdateCurrentViewer();
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
