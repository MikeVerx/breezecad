#pragma once

#include "platform/glfw/GlfwWindow.h"
#include "occt/OcctView.h"
#include "doc/BreezeDocument.h"

#include <TDF_Label.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pln.hxx>

#include <map>
#include <memory>
#include <vector>
#include <functional>
#include <string>

namespace BreezeCAD {

class Application {
public:
    Application();
    ~Application();

    void Run();

private:
    std::unique_ptr<GlfwWindow>   myWindow;
    std::unique_ptr<OcctView>     myOcctView;
    // === QAT (Quick Access Toolbar) ===
    char mySearchBuffer[128] = "";
    bool myShowSettingsDialog = false;
    int  myActiveSettingsTab  = 0;
    std::vector<std::string> myPinnedCommands;

    // === Телеметрия ===
    double myLastRAMMB      = 0.0;
    int    myLastShapeCount = 0;

    // === Команды (для поиска) ===
    struct CommandEntry {
        std::string id;
        std::string name;
        std::string shortcut;
        std::function<void()> action;
    };
    std::vector<CommandEntry> myCommands;

    // Активный документ (сырой указатель на один из myDocuments)
    BreezeDocument* myDocument = nullptr;

    // Все открытые документы (владеют)
    std::vector<std::unique_ptr<BreezeDocument>> myDocuments;
    int myActiveDocIndex = -1;

    // Имя для новой вкладки (Project1, Project2, ...)
    int myNextProjectNumber = 1;
    std::vector<std::string> myProjectNames;   // параллельно с myDocuments
    std::vector<std::string> myProjectPaths;   // путь файла (пусто, если не сохранён)

    bool  myShowAddBoxDialog = false;
    char  myNewBoxName[128]  = "Box";
    float myNewBoxPos[3]     = { 0.0f, 0.0f, 0.0f };
    float myNewBoxSize[3]    = { 100.0f, 100.0f, 100.0f };

    std::string myCurrentPath;
    char        myFilePathBuffer[512] = "D:/test_breezecad/part.cbf";

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
    bool myShowGrid     = true;   // показывать ли сетку в эскизе
    int  myActiveRibbonTab = 1;

    // Индекс выбранного инструмента в каждой группе (для Ribbon-split)
    int myLineToolIndex   = 0;
    int myRectToolIndex   = 0;
    int myCircleToolIndex = 0;
    int myArcToolIndex    = 0;
    int myPolyToolIndex   = 0;
    int myTrimToolIndex   = 0;
    int myOffsetToolIndex = 0;
    int myMoveToolIndex   = 0;
    int myConstrainToolIndex = 0;  // 3D Model по умолчанию   // 0=Sketch, 1=3D Model, 2=Annotate, 3=Tools, 4=Manage, 5=View
    LineStyle myCurrentLineStyle = LS_Normal;

    // === Состояние рисования ===
    bool   myHasSketchStart = false;
    gp_Pnt mySketchStartPoint;

    // Точный ввод длины и угла
    char myLengthBuffer[32] = "0";
    char myAngleBuffer[32]  = "0";
    bool myInputActive      = false;

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
    void DrawStatusBar();
    void DrawDependenciesPanel();
    void DrawPreviewInfo();
    void RefreshView();
    void RefreshSketchLines();
    void OnSave();
    void OnOpen();
    void OnNewDocument();

    // === Multi-Document ===
    void CreateNewDoc();                                          // создать пустой + сделать активным
    int  AddDocFromFile(const std::string& path,
                        const Handle(TDocStd_Document)& loaded);  // открыть из файла
    void SwitchToDoc(int index);                                  // переключиться
    void CloseDoc(int index);                                     // закрыть (с подтверждением)
    void DrawProjectTabs();                                       // нижний таб-бар
    void OnUndo();
    void OnRedo();
    void DrawRibbon();
    void DrawQuickAccessToolbar();
    void DrawFileMenu();
    void DrawSettingsDialog();
    void InitCommands();
    void UpdateTelemetry();
    void DrawRibbonTabs();
    void DrawRibbonSketchTab();
    void DrawRibbonModelTab();
    void DrawRibbonPlaceholderTab(const char* name);
    void EnterSketch(const TDF_Label& sketchLabel);
    void ExitSketch();
    void OnSketchClick(int px, int py);
    void FinishCurrentLine();  // для автопродолжения
    void ApplyExactLine();     // создать линию по введённым Length/Angle

    bool DrawLabelTree(const TDF_Label& label,
                       const TDF_Label& selectedLabel);
};

} // namespace BreezeCAD
