#pragma once

#include "platform/glfw/GlfwWindow.h"
#include "occt/OcctView.h"
#include "doc/OrdoDocument.h"

#include <TDF_Label.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pln.hxx>

#include <map>
#include <memory>
#include <string>

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

    bool  myShowAddBoxDialog = false;
    char  myNewBoxName[128]  = "Box";
    float myNewBoxPos[3]     = { 0.0f, 0.0f, 0.0f };
    float myNewBoxSize[3]    = { 100.0f, 100.0f, 100.0f };

    std::string myCurrentPath;
    char        myFilePathBuffer[512] = "D:/test_ordocad/part.cbf";

    std::map<std::string, bool> myVisibility;

    TDF_Label myPendingDeleteLabel;
    TDF_Label myPendingCreateSketchLabel;
    TDF_Label myPendingEnterSketchLabel;

    TDF_Label myActiveSketchLabel;

    // === Настройки привязок ===
    bool mySnapToGrid   = true;
    bool mySnapToPoints = true;
    bool myAutoContinue = true;
    bool myOrthoMode    = false;  // Shift — угол кратен 45°
    LineStyle myCurrentLineStyle = LS_Normal;

    // === Состояние рисования ===
    bool   myHasSketchStart = false;
    gp_Pnt mySketchStartPoint;

    // Последнее превью (для отображения длины и угла)
    bool   myHasPreview = false;
    double myPreviewLength = 0.0;
    double myPreviewAngle  = 0.0;
    double myMouseX = 0.0;
    double myMouseY = 0.0;

    void DrawDocumentTree();
    void DrawAddBoxDialog();
    void DrawMenuBar();
    void DrawSketchOverlay();
    void DrawPreviewInfo();
    void RefreshView();
    void RefreshSketchLines();
    void OnSave();
    void OnOpen();
    void OnNewDocument();
    void EnterSketch(const TDF_Label& sketchLabel);
    void ExitSketch();
    void OnSketchClick(int px, int py);
    void FinishCurrentLine();  // для автопродолжения

    bool DrawLabelTree(const TDF_Label& label,
                       const TDF_Label& selectedLabel);
};

} // namespace OrdoCAD
