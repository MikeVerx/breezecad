#pragma once

#include "platform/glfw/GlfwWindow.h"
#include "occt/OcctView.h"
#include "doc/OrdoDocument.h"

#include <TDF_Label.hxx>

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
    TDF_Label myPendingEnterSketchLabel;   // двойной клик по эскизу → войти

    // Активный эскиз (если в режиме 2D)
    TDF_Label myActiveSketchLabel;

    void DrawDocumentTree();
    void DrawAddBoxDialog();
    void DrawMenuBar();
    void DrawSketchOverlay();
    void RefreshView();
    void OnSave();
    void OnOpen();
    void OnNewDocument();
    void EnterSketch(const TDF_Label& sketchLabel);
    void ExitSketch();

    bool DrawLabelTree(const TDF_Label& label,
                       const TDF_Label& selectedLabel);
};

} // namespace OrdoCAD
