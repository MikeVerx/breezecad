#include "occt/OcctView.h"
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <BRep_Tool.hxx>
#include <Geom_Curve.hxx>
#include <Geom_CartesianPoint.hxx>
#include <AIS_Line.hxx>

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
#include <gp_Ax1.hxx>
#include <gp_Trsf.hxx>
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

namespace BreezeCAD {

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
    // Включаем Compatibility Profile для поддержки толстых линий
    myDriver->ChangeOptions().contextCompatible = true;

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
        Quantity_Color grey(0.35, 0.35, 0.4, Quantity_TOC_RGB);  // бледная сетка

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

    myShapeEntries.push_back({ label, ais, shape, isSystem });
}

void OcctView::ClearAllShapes() {
    if (myContext.IsNull()) return;
    for (auto& e : myShapeEntries) {
        if (!e.Shape.IsNull()) {
            myContext->Remove(e.Shape, false);
        }
    }
    myShapeEntries.clear();
    myContext->UpdateCurrentViewer();
}

void OcctView::ShowGrid(bool show) {
    myGridVisible = show;
    if (myContext.IsNull()) return;
    for (auto& obj : myGridObjects) {
        if (obj.IsNull()) continue;
        if (show) {
            myContext->Display(obj, false);
            myContext->Deactivate(obj);
        } else {
            myContext->Erase(obj, false);
        }
    }
    myContext->UpdateCurrentViewer();
    if (!myView.IsNull()) myView->Redraw();
}

void OcctView::ClearGrid() {
    if (myContext.IsNull()) return;
    for (auto& obj : myGridObjects) {
        if (!obj.IsNull()) {
            myContext->Remove(obj, false);
        }
    }
    myGridObjects.clear();
    myContext->UpdateCurrentViewer();
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
    gp_Ax3 ax(origin, normal);
    gp_Dir uDir = ax.XDirection();
    gp_Dir vDir = ax.YDirection();

    const int    nLines = 20;
    const double step   = 10.0;
    const double ext    = nLines * step;

    Quantity_Color gridColor(0.18, 0.18, 0.22, Quantity_TOC_RGB);

    // Сетка — тонкая
    for (int i = -nLines; i <= nLines; i++) {
        if (i == 0) continue;

        gp_Pnt s1 = origin.Translated(gp_Vec(uDir) * -ext + gp_Vec(vDir) * (i * step));
        gp_Pnt e1 = origin.Translated(gp_Vec(uDir) *  ext + gp_Vec(vDir) * (i * step));
        TopoDS_Shape l1 = BRepBuilderAPI_MakeEdge(s1, e1).Shape();
        Handle(AIS_Shape) a1 = new AIS_Shape(l1);
        a1->SetDisplayMode(AIS_WireFrame);
        Handle(Prs3d_LineAspect) as1 = new Prs3d_LineAspect(gridColor, Aspect_TOL_SOLID, 1.0);
        a1->Attributes()->SetWireAspect(as1);
        myContext->Display(a1, false);
        myContext->SetColor(a1, gridColor, false);
        myContext->SetWidth(a1, 1.0, false);
        myContext->Deactivate(a1);
        myGridObjects.push_back(a1);

        gp_Pnt s2 = origin.Translated(gp_Vec(vDir) * -ext + gp_Vec(uDir) * (i * step));
        gp_Pnt e2 = origin.Translated(gp_Vec(vDir) *  ext + gp_Vec(uDir) * (i * step));
        TopoDS_Shape l2 = BRepBuilderAPI_MakeEdge(s2, e2).Shape();
        Handle(AIS_Shape) a2 = new AIS_Shape(l2);
        a2->SetDisplayMode(AIS_WireFrame);
        Handle(Prs3d_LineAspect) as2 = new Prs3d_LineAspect(gridColor, Aspect_TOL_SOLID, 1.0);
        a2->Attributes()->SetWireAspect(as2);
        myContext->Display(a2, false);
        myContext->SetColor(a2, gridColor, false);
        myContext->SetWidth(a2, 1.0, false);
        myContext->Deactivate(a2);
        myGridObjects.push_back(a2);
    }

    // Осевые X (красная) и Y (зелёная) — штрих-пунктирные, толстые
    TopoDS_Shape xAxis = BRepBuilderAPI_MakeEdge(gp_Lin(origin, uDir), -ext, ext).Shape();
    TopoDS_Shape yAxis = BRepBuilderAPI_MakeEdge(gp_Lin(origin, vDir), -ext, ext).Shape();

    Handle(AIS_Shape) xAis = new AIS_Shape(xAxis);
    xAis->SetDisplayMode(AIS_WireFrame);
    Handle(Prs3d_LineAspect) xAsp = new Prs3d_LineAspect(Quantity_NOC_RED, Aspect_TOL_DOTDASH, 4.0);
    xAis->Attributes()->SetWireAspect(xAsp);
    myContext->Display(xAis, false);
    myContext->SetColor(xAis, Quantity_NOC_RED, false);
    myContext->SetWidth(xAis, 4.0, false);
    myContext->Deactivate(xAis);

    Handle(AIS_Shape) yAis = new AIS_Shape(yAxis);
    yAis->SetDisplayMode(AIS_WireFrame);
    Handle(Prs3d_LineAspect) yAsp = new Prs3d_LineAspect(Quantity_NOC_GREEN, Aspect_TOL_DOTDASH, 4.0);
    yAis->Attributes()->SetWireAspect(yAsp);
    myContext->Display(yAis, false);
    myContext->SetColor(yAis, Quantity_NOC_GREEN, false);
    myContext->SetWidth(yAis, 4.0, false);
    myContext->Deactivate(yAis);

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
    if (!myView.IsNull()) {
        myView->Camera()->SetProjectionType(Graphic3d_Camera::Projection_Perspective);
    }

    // 2. Убираем сетку
    ClearGrid();

    // 3. Восстанавливаем ВСЕ объекты из myShapeEntries
    //    (системные + пользовательские). Это гарантирует, что плоскости
    //    и оси снова появляются в сцене после выхода из эскиза.
    for (auto& e : myShapeEntries) {
        if (e.Shape.IsNull()) continue;

        myContext->Display(e.Shape, false);

        if (e.IsSystem) {
            // Серые полупрозрачные плоскости/оси/точка
            myContext->SetTransparency(e.Shape, 0.95f, false);
            myContext->SetColor(e.Shape, Quantity_Color(0.55, 0.55, 0.55, Quantity_TOC_RGB), false);

            if (e.TopoShape.ShapeType() == TopAbs_FACE) {
                myContext->Activate(e.Shape, 4);
            } else {
                myContext->Activate(e.Shape, 0);
            }
        } else {
            myContext->Activate(e.Shape, 0);
        }
    }
    myContext->UpdateCurrentViewer();

    // 4. Возвращаем камеру
    if (!myView.IsNull()) {
        myView->FitAll(0.01, false);
        myView->ZFitAll();
        myView->Redraw();
    }

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

    double dx = to.X() - from.X();
    double dy = to.Y() - from.Y();
    double dz = to.Z() - from.Z();
    if (dx*dx + dy*dy + dz*dz < 1e-6) return;

    try {
        ClearPreviewLine();

        Handle(Geom_CartesianPoint) gp1 = new Geom_CartesianPoint(from);
        Handle(Geom_CartesianPoint) gp2 = new Geom_CartesianPoint(to);
        myPreviewLine = new AIS_Line(gp1, gp2);

        Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(
            Quantity_NOC_YELLOW, Aspect_TOL_DASH, 10.0);
        myPreviewLine->Attributes()->SetLineAspect(aspect);
        myPreviewLine->SetWidth(10.0);
        myPreviewLine->SetColor(Quantity_NOC_YELLOW);

        myContext->Display(myPreviewLine, false);
        myContext->SetZLayer(myPreviewLine, Graphic3d_ZLayerId_Topmost);
        myContext->Deactivate(myPreviewLine);

        // Маркеры
        Handle(Geom_Point) gStart = new Geom_CartesianPoint(from);
        myStartMarker = new AIS_Point(gStart);
        Handle(Prs3d_PointAspect) pa1 = new Prs3d_PointAspect(
            Aspect_TOM_O, Quantity_NOC_GREEN, 3.0);
        Handle(Prs3d_Drawer) dr1 = new Prs3d_Drawer();
        dr1->SetPointAspect(pa1);
        myStartMarker->SetAttributes(dr1);
        myContext->Display(myStartMarker, false);
        myContext->Deactivate(myStartMarker);

        Handle(Geom_Point) gEnd = new Geom_CartesianPoint(to);
        myEndMarker = new AIS_Point(gEnd);
        Handle(Prs3d_PointAspect) pa2 = new Prs3d_PointAspect(
            Aspect_TOM_O, Quantity_NOC_YELLOW, 3.0);
        Handle(Prs3d_Drawer) dr2 = new Prs3d_Drawer();
        dr2->SetPointAspect(pa2);
        myEndMarker->SetAttributes(dr2);
        myContext->Display(myEndMarker, false);
        myContext->Deactivate(myEndMarker);

        myContext->UpdateCurrentViewer();
        if (!myView.IsNull()) myView->Redraw();
    } catch (...) {
        myPreviewLine.Nullify();
        myStartMarker.Nullify();
        myEndMarker.Nullify();
    }
}

void OcctView::ShowCursorMarkerHighlighted(const gp_Pnt& p, bool highlight) {
    if (myContext.IsNull()) return;

    try {
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

        Handle(Geom_Point) gP = new Geom_CartesianPoint(p);
        myEndMarker = new AIS_Point(gP);

        Quantity_Color col = highlight ? Quantity_NOC_GREEN : Quantity_NOC_YELLOW;
        Handle(Prs3d_PointAspect) pa = new Prs3d_PointAspect(
            Aspect_TOM_O, col, highlight ? 5.0 : 3.0);
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

void OcctView::DisplaySketchLine(const TopoDS_Shape& line, const TDF_Label& label, int style) {
    if (line.IsNull() || myContext.IsNull()) return;
    if (line.ShapeType() != TopAbs_EDGE) return;

    TopoDS_Edge edge = TopoDS::Edge(line);
    Standard_Real first, last;
    Handle(Geom_Curve) curve = BRep_Tool::Curve(edge, first, last);
    if (curve.IsNull()) return;

    gp_Pnt p1 = curve->Value(first);
    gp_Pnt p2 = curve->Value(last);

    Handle(Geom_CartesianPoint) gp1 = new Geom_CartesianPoint(p1);
    Handle(Geom_CartesianPoint) gp2 = new Geom_CartesianPoint(p2);
    Handle(AIS_Line) ais = new AIS_Line(gp1, gp2);

    Quantity_Color color;
    Aspect_TypeOfLine tol;
    double width;

    switch (style) {
        case 1:  // Construction
            color = Quantity_Color(0.55, 0.55, 0.75, Quantity_TOC_RGB);
            tol   = Aspect_TOL_DASH;
            width = 6.0;
            break;
        case 2:  // Centerline
            color = Quantity_Color(0.9, 0.45, 0.15, Quantity_TOC_RGB);
            tol   = Aspect_TOL_DOTDASH;
            width = 7.0;
            break;
        default: // Normal
            color = Quantity_Color(0.75, 0.2, 1.0, Quantity_TOC_RGB);
            tol   = Aspect_TOL_SOLID;
            width = 12.0;
            break;
    }

    Handle(Prs3d_LineAspect) aspect = new Prs3d_LineAspect(color, tol, width);
    ais->Attributes()->SetLineAspect(aspect);
    ais->SetWidth(width);
    ais->SetColor(color);

    myContext->Display(ais, false);
    myContext->SetZLayer(ais, Graphic3d_ZLayerId_Topmost);
    myContext->Deactivate(ais);

    myShapeEntries.push_back({ label, ais, line, false });
    myContext->UpdateCurrentViewer();
}

bool OcctView::ConsumeContextMenuRequest(int& outX, int& outY) {
    if (!myContextMenuPending) return false;
    outX = myContextMenuX;
    outY = myContextMenuY;
    myContextMenuPending = false;
    return true;
}

bool OcctView::SelectAtPixel(int px, int py) {
    if (myContext.IsNull() || myView.IsNull()) return false;
    myContext->ClearSelected(false);
    myContext->MoveTo(px, py, myView, false);
    myContext->SelectDetected(AIS_SelectionScheme_Replace);
    myContext->UpdateCurrentViewer();
    myView->Redraw();
    return myContext->NbSelected() > 0;
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

gp_Dir OcctView::GetCameraDirection() const {
    if (myView.IsNull()) return gp_Dir(0, 0, -1);
    Handle(Graphic3d_Camera) cam = myView->Camera();
    if (cam.IsNull()) return gp_Dir(0, 0, -1);
    return cam->Direction();
}

gp_Dir OcctView::GetCameraUp() const {
    if (myView.IsNull()) return gp_Dir(0, 1, 0);
    Handle(Graphic3d_Camera) cam = myView->Camera();
    if (cam.IsNull()) return gp_Dir(0, 1, 0);
    return cam->Up();
}

void OcctView::SetCameraDirectionAndUp(const gp_Dir& dir, const gp_Dir& up) {
    if (myView.IsNull()) return;
    Handle(Graphic3d_Camera) cam = myView->Camera();
    if (cam.IsNull()) return;

    // Защита: если dir и up параллельны — подставляем дефолтный up
    gp_Dir safeUp = up;
    if (std::abs(dir.Dot(up)) > 0.99) {
        gp_Dir fallback(0, 0, 1);
        if (std::abs(dir.Dot(fallback)) > 0.9) fallback = gp_Dir(0, 1, 0);
        safeUp = fallback;
    }

    cam->SetDirection(dir);
    cam->SetUp(safeUp);
    myView->Redraw();
}

void OcctView::RotateAroundUp(double angleDeg) {
    if (myView.IsNull()) return;
    Handle(Graphic3d_Camera) cam = myView->Camera();
    if (cam.IsNull()) return;

    gp_Dir dir = cam->Direction();
    gp_Dir up  = cam->Up();

    // Защита: если up и dir коллинеарны, поворот вокруг up бессмыслен
    if (std::abs(dir.Dot(up)) > 0.99) return;

    gp_Trsf rot;
    rot.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), up), angleDeg * M_PI / 180.0);
    gp_Dir newDir = dir.Transformed(rot);
    cam->SetDirection(newDir);
    myView->Redraw();
}

void OcctView::SetHomeView() {
    if (myView.IsNull()) return;
    Handle(Graphic3d_Camera) cam = myView->Camera();
    cam->SetDirection(gp_Dir(-1, -1, -1));
    cam->SetUp(gp_Dir(0, 0, 1));
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
        // В режиме эскиза ЛКМ обрабатывается Application'ом (рисование)
        if (myInSketchMode) return;

        if (action == GLFW_PRESS) {
            myLeftPressed = true;
            myLeftDragging = false;
            myRectActive = false;
            myLeftPressX = xpos;
            myLeftPressY = ypos;
            myRectX1 = px; myRectY1 = py;
            myRectX2 = px; myRectY2 = py;
        } else if (action == GLFW_RELEASE) {
            if (myLeftPressed && !myLeftDragging) {
                // Короткий клик — выделение объекта под курсором
                myContext->ClearSelected(false);
                myContext->MoveTo(px, py, myView, false);
                myContext->SelectDetected(AIS_SelectionScheme_Replace);
                myContext->UpdateCurrentViewer();
                myView->Redraw();
            } else if (myLeftDragging) {
                // Рамка — выделение по прямоугольнику
                int xMin = std::min(myRectX1, myRectX2);
                int yMin = std::min(myRectY1, myRectY2);
                int xMax = std::max(myRectX1, myRectX2);
                int yMax = std::max(myRectY1, myRectY2);

                if (xMax - xMin > 5 && yMax - yMin > 5) {
                    myContext->ClearSelected(false);
                    myContext->Select(xMin, yMin, xMax, yMax, myView,
                                       AIS_SelectionScheme_Add);
                    myContext->UpdateCurrentViewer();
                    myView->Redraw();
                }
            }
            myLeftPressed = false;
            myLeftDragging = false;
            myRectActive = false;
        }
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (myInSketchMode) return;
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

    // === ПКМ drag = вращение ===
    if (myRotating) {
        myView->Rotation(px, py);
        myView->Redraw();
        myLastX = x; myLastY = y;
        return;
    }

    // === ЛКМ drag = рамка выделения ===
    if (myLeftPressed && !myInSketchMode) {
        if (!myLeftDragging) {
            double dx = x - myLeftPressX;
            double dy = y - myLeftPressY;
            if (dx*dx + dy*dy > 25.0) {  // > 5 пикселей
                myLeftDragging = true;
                myRectActive = true;
            }
        }
        if (myLeftDragging) {
            myRectX2 = px;
            myRectY2 = py;
            if (!myView.IsNull()) myView->Redraw();
        }
        myLastX = x; myLastY = y;
        return;
    }

    // === СКМ = панорамирование ===
    if (myPanning) {
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

} // namespace BreezeCAD
