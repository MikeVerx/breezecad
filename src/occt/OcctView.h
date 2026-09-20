#pragma once

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <AIS_Line.hxx>
#include <AIS_ColoredShape.hxx>
#include <AIS_Point.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_NeutralWindow.hxx>
#include <Aspect_RenderingContext.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <TDF_Label.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>

#include <GLFW/glfw3.h>

#include <vector>

namespace BreezeCAD {

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

    void DisplayShape(const TopoDS_Shape& shape,
                      const TDF_Label& label,
                      bool isSystem = false);
    void ClearAllShapes();
    void ShowGrid(bool show);
    void ClearGrid();
    void FitAll();

    void SetShapeVisible(const TDF_Label& label, bool visible);
    void SetDisplayModeAll(int mode, bool edges);

    // Сохранить текущий вид в файл PNG (для превью проектов).
    // Возвращает true при успехе.
    bool CaptureToPixmap(int width, int height, const std::string& outPath);

    TDF_Label GetSelectedLabel() const;
    bool SelectAtPixel(int px, int py);

    // === Рамка выделения ===
    bool IsSelectionRectActive() const { return myRectActive; }
    void GetSelectionRect(int& x1, int& y1, int& x2, int& y2) const {
        x1 = myRectX1; y1 = myRectY1; x2 = myRectX2; y2 = myRectY2;
    }

    // === ПКМ-контекстное меню в 3D-сцене ===
    // Возвращает true один раз, если был клик ПКМ (без движения).
    bool ConsumeContextMenuRequest(int& outX, int& outY);
    bool IsSketchMode() const { return !myInSketchMode ? false : true; }
    int GetShapeCount() const { return (int)myShapeEntries.size(); }

    // Все метки отображаемых объектов (для изоляции)
    std::vector<TDF_Label> GetAllLabels() const {
        std::vector<TDF_Label> out;
        out.reserve(myShapeEntries.size());
        for (const auto& e : myShapeEntries) out.push_back(e.Label);
        return out;
    }
    void ClearSelection();

    // === Режим эскиза ===
    void EnterSketchMode(const gp_Dir& planeNormal, const gp_Pnt& planeOrigin);
    void ExitSketchMode();
    bool IsInSketchMode() const { return myInSketchMode; }
    const gp_Pln& GetSketchPlane() const { return mySketchPlane; }

    // === Управление камерой (для ViewCube) ===
    gp_Dir GetCameraDirection() const;
    gp_Dir GetCameraUp() const;
    void   SetCameraDirectionAndUp(const gp_Dir& dir, const gp_Dir& up);
    void   RotateAroundUp(double angleDeg);
    void   SetHomeView();

    // Конвертация экранных координат в точку на плоскости эскиза
    bool ScreenToPlane(int px, int py, gp_Pnt& out) const;

    // Показать/обновить/убрать превью-линию
    void ShowPreviewLine(const gp_Pnt& from, const gp_Pnt& to);
    void ShowCursorMarker(const gp_Pnt& p);
    void ShowCursorMarkerHighlighted(const gp_Pnt& p, bool highlight);
    void ClearPreviewLine();

    // Показать постоянную линию эскиза
    void DisplaySketchLine(const TopoDS_Shape& line, const TDF_Label& label, int style = 0);

private:
    struct ShapeEntry {
        TDF_Label                     Label;
        Handle(AIS_InteractiveObject) Shape;
        TopoDS_Shape                  TopoShape;
        bool                          IsSystem;
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
    double myLeftPressX = 0.0;
    double myLeftPressY = 0.0;
    bool   myLeftDragging = false;
    int    myRectX1 = 0, myRectY1 = 0, myRectX2 = 0, myRectY2 = 0;
    bool   myRectActive = false;

    bool              myInSketchMode = false;
    gp_Pln            mySketchPlane;
    Handle(AIS_Line) myPreviewLine;
    Handle(AIS_Point) myStartMarker;
    Handle(AIS_Point) myEndMarker;

    // Состояние ПКМ
    bool   myContextMenuPending = false;
    int    myContextMenuX = 0;
    int    myContextMenuY = 0;
    double myRightPressX = 0;
    double myRightPressY = 0;
    bool   myRightPressed = false;

    std::vector<ShapeEntry> myShapeEntries;
    std::vector<Handle(AIS_InteractiveObject)> myGridObjects;
    bool myGridVisible = true;

    void BuildSketchGrid(const gp_Dir& normal, const gp_Pnt& origin);
};

} // namespace BreezeCAD
