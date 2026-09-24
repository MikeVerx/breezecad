#pragma once

#include "platform/glfw/GlfwWindow.h"
#include "occt/OcctView.h"
#include "doc/BreezeDocument.h"
#include "app/IconManager.h"
#include "ui/viewcube/ViewCubeOverlay.h"   // ViewCubeOverlay + Animator

#include <TDF_Label.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pln.hxx>
#include "imgui.h"

#include <map>
#include <memory>
#include <vector>
#include <functional>
#include <string>

namespace BreezeCAD {

struct ThemeTokens {
    // ==== Home Screen / общий UI ====
    ImU32 bgTitle;
    ImU32 bgRibbon;
    ImU32 bgContent;
    ImU32 bgCardBg;
    ImU32 border;
    ImU32 borderStrong;
    ImU32 textMain;
    ImU32 textDim;
    ImU32 homeBtnLabel;
    ImU32 homeBtnBg;
    ImU32 homeBtnBgHover;
    ImU32 homeBtnBgDisabled;
    ImU32 homeBtnTextDisabled;
    ImU32 thumbBg;
    ImU32 accent;
    ImU32 accentHover;

    // ==== ViewCube ====
    ImU32 cubeBgCircle;
    ImU32 cubeFaceTop;
    ImU32 cubeFaceBottom;
    ImU32 cubeFaceFront;
    ImU32 cubeFaceBack;
    ImU32 cubeFaceRight;
    ImU32 cubeFaceLeft;
    ImU32 cubeOutline;
    ImU32 cubeText;
    ImU32 cubeHover;
    ImU32 cubeFaceHover;
};



class Application {
public:
    Application();
    ~Application();

    void Run();

private:
    std::unique_ptr<GlfwWindow>   myWindow;
    std::unique_ptr<OcctView>     myOcctView;
    UI::ViewCube::ViewCubeOverlay myViewCube;
    IconManager                   myIcons;
    // === Диалог зависимостей ===
    bool myDependencyDialogOpen = false;
    int  myCurrentDependencyType = 0;  // 0=Coincident, 1=Parallel, ...
    std::vector<TDF_Label> myDependencyBase;       // базовые объекты
    std::vector<TDF_Label> myDependencyDependent;  // зависимые объекты
    ImVec2 myDependencyDialogPos = ImVec2(0, 0);   // позиция (запоминается)
    bool   myDependencyDialogPosSet = false;

    // === QAT (Quick Access Toolbar) ===
    char mySearchBuffer[128] = "";
    // === Недавние файлы ===
    std::vector<std::string> myRecentFiles;
    void AddRecentFile(const std::string& path);

    // === Превью проектов ===
    // Для каждого документа — путь к файлу-превью (PNG)
    std::vector<std::string> myThumbnailPaths;

    // Сохранить превью текущего активного документа в PNG
    void CaptureCurrentThumbnail();
    // Возвращает путь к превью для документа (пустой, если нет)
    std::string GetThumbnailPath(int docIndex) const;

    // === Буфер обмена (для копирования объектов) ===
    TDF_Label myClipboardLabel;

    // === Изоляция ===
    bool myIsolationActive = false;
    std::map<std::string, bool> myIsolationBackup;   // что было скрыто до изоляции

    // === Главная (Home) ===
    bool myShowHomeScreen = true;   // по умолчанию — стартовая страница
    int  myHomeViewMode = 0;   // 0 = плитки, 1 = список
    char myHomeSearch[128] = {0};

    // === Правая панель навигации ===
    bool myRightPanelExpanded = false;
    // ViewCube-состояние переехало в myViewCube (ViewCubeOverlay)

    // === ПКМ-меню в 3D-сцене ===
    bool   myPendingContextMenu = false;
    ImVec2 myContextMenuPos = ImVec2(0, 0);

    // === Диалог переименования ===
    TDF_Label myPendingRenameLabel;
    char      myRenameBuffer[128] = "";
    bool      myShowRenameDialog = false;

    // === Диалог подтверждения удаления ===
    bool myShowDeleteConfirm = false;

    // === Диалог несохранённых изменений ===
    bool myShowUnsavedDialog = false;
    int  myPendingCloseDocIndex = -1;   // какой документ закрываем (-1 = не закрывать)
    bool myPendingWindowClose = false;  // закрытие всего окна
    char myUnsavedDialogInfo[256] = "";
    char myDeleteConfirmInfo[256] = "";
    bool mySkipDeleteConfirm = false;   // «не спрашивать снова» (на сеанс)

    // === Диалог свойств ===
    TDF_Label myPendingPropertiesLabel;
    bool      myShowPropertiesDialog = false;

    // === Tooltips (3 уровня задержки) ===
    std::string myHoveredItemId;
    double      myHoverStartTime = 0.0;
    int         myLastTooltipLevel = 0;  // 0=нет, 1=название, 2=краткое, 3=подробное+картинка

    // === Splitter (регулировка ширины дерева) ===
    float myTreeWidth = 340.0f;      // текущая ширина
    bool  myIsResizingTree = false;  // идёт перетаскивание

    bool myStartExpandedManually = false;  // пользователь развернул Start вручную
    bool myShowSettingsDialog = false;
    bool myShowNotImplementedDialog = false;
    std::string myNotImplementedFeature;
    int  myActiveSettingsTab  = 0;
    int  myCurrentTheme       = 1;
    ThemeTokens myTheme;
    int  myDisplayStyleIndex = 0;   // Стиль отображения (7 вариантов)
    int  myBackgroundIndex   = 0;   // Фон вьюпорта (5 вариантов)   // 0=light, 1=dark, 2=system
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
    int  myActiveRibbonTab = 0;

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
    void DrawTreeSplitter();
    void DrawContextMenu3D();
    void DrawContextMenuItems(const TDF_Label& label, bool hasOps);

    // Логика видимости по умолчанию (учитывает hasOps и myVisibility)
    bool IsLabelVisibleByDefault(const TDF_Label& label, bool hasOps) const;

    // Показать все объекты, уважая правила видимости (для снятия изоляции)
    void ShowAllRespectingDefaults();
    void DrawSelectionRect();
    void DrawRightPanel();
    void DrawHomeScreen();
    void DrawRenameDialog();
    void DrawDeleteConfirmDialog();
    void DrawUnsavedChangesDialog();
    void DrawPropertiesDialog();
    void RequestDelete(const TDF_Label& label);
    void DrawAdvancedTooltip(const std::string& id,
                             const std::string& name,
                             const std::string& shortDesc,
                             const std::string& longDesc,
                             const std::string& imagePath = "");
    void DrawAddBoxDialog();
    void DrawMenuBar();
    void DrawSketchOverlay();
    void DrawStatusBar();
    void DrawViewCube();
    void DrawDependencyDialog();
    void OpenDependencyDialog(int type);
    std::string GetLabelName(const TDF_Label& label) const;
    void DrawDependenciesPanel();
    void DrawPreviewInfo();
    void RefreshView();
    void RefreshSketchLines();
    void OnSave();
    void OnExportSTEP();
    void OnExportSTL();
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
    void ApplyTheme(int theme);
    void InitCommands();
    void UpdateTelemetry();
    void DrawRibbonTabs();
    // Ribbon кнопки (используют SVG через myIcons)
    int RibbonButton(const char* id, const char* const* opts, int optCount,
                     int& idx, const char* icon, bool enabled, bool big = true,
                     const char* tooltip = nullptr, float iconScale = 1.0f);
    int RibbonSingleButton(const char* id, const char* key, const char* icon,
                           bool enabled, bool big = true,
                           const char* tooltip = nullptr, float iconScale = 1.0f);

    void DrawRibbonSketchTab();
    void DrawRibbonModelTab();
    void DrawRibbonAnnotateTab();
    void DrawRibbonToolsTab();
    void DrawRibbonManageTab();
    void DrawRibbonViewTab();
    void DrawRibbonAssemblyTab();  // заглушка

    // Группы ribbon — блок с рамкой и заголовком снизу по центру
    void RibbonGroupStart();
    float myRibbonGroupStartY = 0.0f;
    void RibbonGroupEnd(const char* name);
    void EnterSketch(const TDF_Label& sketchLabel);
    void ExitSketch();
    void OnSketchClick(int px, int py);
    void FinishCurrentLine();  // для автопродолжения
    void ApplyExactLine();     // создать линию по введённым Length/Angle

    bool DrawLabelTree(const TDF_Label& label,
                       const TDF_Label& selectedLabel,
                       bool hasOps);
};

} // namespace BreezeCAD
