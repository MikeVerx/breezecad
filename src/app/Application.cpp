#include "app/Application.h"
#include "app/Localization.h"
#include "app/IconsFontAwesome.h"
#include "doc/BreezeApplication.h"
#include "platform/FileDialog.h"
#include "ui/theme/ThemePresets.h"
#include "ui/theme/ThemeManager.h"
#include "ui/widgets/IconTint.h"
#include "ui/widgets/SvgButton.h"
#include "ui/widgets/Tooltip.h"
#include "ui/widgets/SelectionRect.h"
#include "ui/status/StatusBar.h"            // UI::Status::DrawStatusBar
#include "ui/status/ProjectTabs.h"
#include "ui/qat/QuickAccessToolbar.h"
#include "ui/ribbon/Ribbon.h"
#include "ui/ribbon/RibbonButton.h"
#include "ui/ribbon/RibbonGroup.h"
#include "ui/ribbon/RibbonTabs.h"
#include "ui/ribbon/tabs/ModelTab.h"
#include "ui/ribbon/tabs/ViewTab.h"
#include "ui/ribbon/tabs/SketchTab.h"
#include "util/Utf8.h"
#include "util/Geometry2D.h"
#include "util/LabelName.h"
#include "ui/home/HomeScreen.h"
#include "ui/dialogs/RenameDialog.h"        // UI::Dialogs::DrawRenameDialog
#include "ui/dialogs/DeleteConfirmDialog.h" // UI::Dialogs::DrawDeleteConfirmDialog
#include "ui/dialogs/AddBoxDialog.h"
#include "ui/dialogs/PropertiesDialog.h"    // UI::Dialogs::DrawPropertiesDialog
#include "ui/dialogs/NotImplementedDialog.h"// UI::Dialogs::DrawNotImplementedDialog
#include "ui/dialogs/UnsavedChangesDialog.h"
#include "ui/dialogs/DependencyDialog.h"
#include "ui/dialogs/SettingsDialog.h"
#include "ui/panels/TreeSplitter.h"          // UI::Panels::DrawTreeSplitter
#include "ui/panels/RightPanel.h"            // UI::Panels::DrawRightPanel
#include "ui/panels/DependenciesPanel.h"
#include "ui/panels/DocumentTree.h"
#include "ui/panels/Visibility.h"          // UI::Panels::ShowAllRespectingDefaults          // UI::Panels::DrawDocumentTree
#include "ui/panels/ContextMenu.h"           // UI::Panels::DrawContextMenu3D
#include "ui/home/Thumbnails.h"            // UI::Home::CaptureCurrentThumbnail и др.
#include "ui/theme/Fonts.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <TDF_ChildIterator.hxx>
#include <STEPControl_Writer.hxx>
#include <Interface_Static.hxx>
#include <StlAPI_Writer.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#ifdef _WIN32
  #include <windows.h>
  #include <psapi.h>
#endif
#include <TDataStd_Name.hxx>
#include <TDataStd_Integer.hxx>
#include <TCollection_AsciiString.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopExp_Explorer.hxx>
#include <BRep_Tool.hxx>
#include <TNaming_NamedShape.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <gp_Dir.hxx>
#include <gp_Trsf.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pln.hxx>

#define _USE_MATH_DEFINES
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

// stb_image extern declarations (глобальные, до namespace)
extern "C" unsigned char* stbi_load(const char* filename, int* x, int* y,
                                    int* channels_in_file, int desired_channels);
extern "C" void stbi_image_free(void* retval_from_stbi_load);

namespace BreezeCAD {

using Util::Utf8ToExt;
using Util::ExtToUtf8;
using Util::ComputeLengthAngle;
using Util::ApplyOrtho;
using Util::SnapToGridPoint;
using Util::PointFromLengthAngle;
using Util::LabelKey;
using Util::GetLabelName;


// === TRACE (enabled only when env BREEZECAD_TRACE is set) ===
static void TRACE(const char* msg) {
    static FILE* g_traceFile = nullptr;
    static bool g_enabled = (getenv("BREEZECAD_TRACE") != nullptr);
    if (!g_enabled) return;
    if (!g_traceFile) g_traceFile = fopen("breezecad_trace.log", "w");
    if (g_traceFile) {
        fprintf(g_traceFile, "%s\n", msg);
        fflush(g_traceFile);
    }
}

// Вспомогательная: получить tint-цвет иконки по текущей теме
// Хелпер: маленькая кнопка с SVG-иконкой
// Обёртка для QAT
// Чистый C++ конвертеры (см. BreezeDocument.cpp — там идентичные, но локальные)

// Диапазоны глифов Font Awesome (Private Use Area)

// Тёмная тема
// Светлая тема — фон с оттенком RAL 5024, кнопки светло-серые
// Светлая тема — белый фон + серые кнопки, максимальный контраст
// DeepSColor — фирменная тема с бирюзовым акцентом
void Application::ApplyTheme(int theme) {
    UI::Theme::ApplyTheme(theme, myTheme, myOcctView.get());
    myCurrentTheme = (theme == 3) ? 1 : theme;
}
Application::Application() {
    myWindow = std::make_unique<GlfwWindow>(1280, 720, "BreezeCAD - Prototype");
    if (!myWindow->GetNativeWindow()) return;

    // Загружаем и ПРЕДЗАГРУЖАЕМ все SVG-иконки Lucide
    // (делается до создания окна, чтобы первый кадр не тормозил)
    myIcons.LoadAll("assets/icons");
    myIcons.PreloadDefaults();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    {
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigDebugHighlightIdConflicts = false;
    }

    // Отключаем сохранение позиций окон в imgui.ini —
    // это устраняет баг «белого экрана» при уехавших окнах
    ImGui::GetIO().IniFilename = nullptr;

    ImGui::StyleColorsDark();
    ApplyTheme(myCurrentTheme);
    UI::Theme::LoadFonts();
    ImGui_ImplGlfw_InitForOpenGL(myWindow->GetNativeWindow(), true);
    ImGui_ImplOpenGL3_Init("#version 330");

    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(myWindow->GetNativeWindow(), &fbW, &fbH);

    myOcctView = std::make_unique<OcctView>();
    try {
        if (!myOcctView->Init(myWindow->GetNativeWindow(), fbW, fbH)) {
            myOcctView.reset(); return;
        }
    } catch (...) { myOcctView.reset(); return; }

    ApplyTheme(myCurrentTheme);

    CreateNewDoc();
    InitCommands();

    RefreshView();
    if (myOcctView) myOcctView->FitAll();

    myWindow->SetMouseButtonCallback([this](int b, int a, int m) {
        if (ImGui::GetIO().WantCaptureMouse) return;
        if (myOcctView) myOcctView->OnMouseButton(b, a, m);
    });
    myWindow->SetCursorPosCallback([this](double x, double y) {
        if (ImGui::GetIO().WantCaptureMouse) return;
        if (myOcctView) myOcctView->OnMouseMove(x, y);
    });
    myWindow->SetScrollCallback([this](double x, double y) {
        if (ImGui::GetIO().WantCaptureMouse) return;
        if (myOcctView) myOcctView->OnMouseScroll(x, y);
    });
    myWindow->SetResizeCallback([this](int w, int h) {
        if (myOcctView) myOcctView->Resize(w, h);
    });
}

Application::~Application() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Application::RefreshView() {
    if (!myOcctView || !myDocument) return;
    myOcctView->ClearAllShapes();

    bool hasOps = myDocument->Has3DOperations();

    // Рекурсивный обход всего дерева
    std::function<void(const TDF_Label&)> walk = [&](const TDF_Label& l) {
        int type = BreezeDocument::GetType(l);

        TopoDS_Shape s = myDocument->GetShape(l);
        if (!s.IsNull()) {
            myOcctView->DisplayShape(s, l, BreezeDocument::IsSystem(l));

            // Определяем видимость
            std::string key = LabelKey(l);
            auto it = myVisibility.find(key);

            bool visible;
            if (it != myVisibility.end()) {
                // Пользователь явно указал
                visible = it->second;
            } else {
                // По умолчанию: оси и плоскости скрыты, если есть 3D-операции
                bool isSystemHideable = (type == FT_Axis || type == FT_Plane);
                visible = !(hasOps && isSystemHideable);
            }

            if (!visible) {
                myOcctView->SetShapeVisible(l, false);
            }
        }

        TDF_ChildIterator it(l, false);
        for (; it.More(); it.Next()) walk(it.Value());
    };
    walk(myDocument->MainLabel());

    myOcctView->Redraw();
}

void Application::OnNewDocument() {
    CreateNewDoc();
    RefreshView();
    if (myOcctView) myOcctView->FitAll();
}

// ============================================================
//  Multi-Document Interface
// ============================================================

void Application::CreateNewDoc() {
    auto doc = std::make_unique<BreezeDocument>();
    doc->CreateBaseGeometry();

    std::string name = "Project" + std::to_string(myNextProjectNumber++);

    myDocuments.push_back(std::move(doc));
    myProjectNames.push_back(name);
    myProjectPaths.push_back("");
    myActiveDocIndex = (int)myDocuments.size() - 1;
    myDocument = myDocuments.back().get();

    std::cout << "CreateNewDoc: '" << name << "' (index " << myActiveDocIndex << ")";
}

int Application::AddDocFromFile(const std::string& path,
                                 const Handle(TDocStd_Document)& loaded) {
    auto doc = std::make_unique<BreezeDocument>();
    doc->SetDocument(loaded);

    // Извлекаем имя файла из пути
    std::string name = path;
    size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);

    myDocuments.push_back(std::move(doc));
    myProjectNames.push_back(name);
    myProjectPaths.push_back(path);
    myActiveDocIndex = (int)myDocuments.size() - 1;
    myDocument = myDocuments.back().get();

    std::cout << "AddDocFromFile: '" << name << "' from " << path << "\n";
    return myActiveDocIndex;
}

void Application::SwitchToDoc(int index) {
    if (index < 0 || index >= (int)myDocuments.size()) return;
    if (index == myActiveDocIndex) return;

    // Сохраняем превью текущего документа перед уходом
    UI::Home::CaptureCurrentThumbnail(*myOcctView, myActiveDocIndex, myThumbnailPaths);

    myActiveDocIndex = index;
    myDocument = myDocuments[index].get();

    // Сброс состояния эскиза — эскиз принадлежал другому документу
    myActiveSketchLabel = TDF_Label();
    myHasSketchStart = false;
    if (myOcctView) {
        myOcctView->ClearPreviewLine();
        myOcctView->ClearAllShapes();
        myOcctView->ClearGrid();
        myOcctView->ExitSketchMode();
    }

    myVisibility.clear();
    RefreshView();
    if (myOcctView) myOcctView->FitAll();

    std::cout << "SwitchToDoc: index " << index << "\n";
}

void Application::CloseDoc(int index) {
    if (index < 0 || index >= (int)myDocuments.size()) return;
    if (myDocuments.size() <= 1) return;  // не закрываем последний

    // Проверка на несохранённые изменения
    BreezeDocument* checkDoc = myDocuments[index].get();
    if (checkDoc && checkDoc->IsModified()) {
        myUnsavedState.pendingCloseDocIndex = index;
        myUnsavedState.pendingWindowClose = false;
        std::snprintf(myUnsavedState.info, sizeof(myUnsavedState.info),
            "Проект «%s» содержит "
            "несохранённые изменения.\n"
            "Сохранить перед закрытием?",
            myProjectNames[index].c_str());
        myUnsavedState.show = true;
        return;
    }

    // Освободить OCAF-документ
    BreezeApplication::Instance().CloseDocument(myDocuments[index]->GetDoc());

    myDocuments.erase(myDocuments.begin() + index);
    myProjectNames.erase(myProjectNames.begin() + index);
    myProjectPaths.erase(myProjectPaths.begin() + index);

    if (myActiveDocIndex >= (int)myDocuments.size()) {
        myActiveDocIndex = (int)myDocuments.size() - 1;
    }
    myDocument = (myActiveDocIndex >= 0) ? myDocuments[myActiveDocIndex].get() : nullptr;

    myActiveSketchLabel = TDF_Label();
    myHasSketchStart = false;
    if (myOcctView) {
        myOcctView->ClearPreviewLine();
        myOcctView->ClearAllShapes();
        myOcctView->ClearGrid();
        myOcctView->ExitSketchMode();
    }
    myVisibility.clear();
    RefreshView();
    if (myOcctView) myOcctView->FitAll();

    std::cout << "CloseDoc: index " << index << "\n";
}

void Application::OnSave() {
    if (!myDocument) return;
    std::string path = myCurrentPath;
    if (path.empty()) {
        if (!ShowFileDialog(true, path, "Сохранить как")) return;
    }
    if (BreezeApplication::Instance().SaveDocument(myDocument->GetDoc(), path)) {
        myCurrentPath = path;
        std::strncpy(myFilePathBuffer, path.c_str(), sizeof(myFilePathBuffer) - 1);
        myFilePathBuffer[sizeof(myFilePathBuffer) - 1] = '\0';
        UI::Home::AddRecentFile(myRecentFiles, path);
        myDocument->SetModified(false);
        std::cout << "Saved to: " << myCurrentPath << "\n";
    }
}

void Application::OnExportSTEP() {
    if (!myDocument || myDocument->GetDoc().IsNull()) return;

    std::string path;
    if (!ShowFileDialog(true, path, "Экспорт в STEP",
                         "STEP (*.step)\0*.step\0STEP (*.stp)\0*.stp\0All (*.*)\0*.*\0\0",
                         "step")) return;

    try {
        // Собираем все TopoDS_Shape из документа
        std::vector<TopoDS_Shape> shapes = myDocument->GetAllShapes();
        if (shapes.empty()) {
            std::cerr << "Export STEP: no shapes\n";
            return;
        }

        STEPControl_Writer writer;
        Interface_Static::SetCVal("write.step.unit", "MM");
        Interface_Static::SetCVal("write.step.schema", "AP214");

        for (const auto& s : shapes) {
            if (writer.Transfer(s, STEPControl_AsIs) != IFSelect_RetDone) {
                std::cerr << "Export STEP: transfer failed\n";
                return;
            }
        }

        if (writer.Write(path.c_str()) != IFSelect_RetDone) {
            std::cerr << "Export STEP: write failed\n";
            return;
        }
        std::cout << "Exported to STEP: " << path << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Export STEP exception: " << e.what() << "\n";
    }
}

void Application::OnExportSTL() {
    if (!myDocument || myDocument->GetDoc().IsNull()) return;

    std::string path;
    if (!ShowFileDialog(true, path, "Экспорт в STL",
                         "STL (*.stl)\0*.stl\0All (*.*)\0*.*\0\0",
                         "stl")) return;

    try {
        std::vector<TopoDS_Shape> shapes = myDocument->GetAllShapes();
        if (shapes.empty()) {
            std::cerr << "Export STL: no shapes\n";
            return;
        }

        // Объединяем всё в один compound и пишем
        BRep_Builder bb;
        TopoDS_Compound comp;
        bb.MakeCompound(comp);
        for (const auto& s : shapes) bb.Add(comp, s);

        // Тесселяция
        BRepMesh_IncrementalMesh mesh(comp, 0.1);

        StlAPI_Writer writer;
        writer.ASCIIMode() = Standard_False;
        if (!writer.Write(comp, path.c_str())) {
            std::cerr << "Export STL: write failed\n";
            return;
        }
        std::cout << "Exported to STL: " << path << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Export STL exception: " << e.what() << "\n";
    }
}

void Application::OnOpen() {
    std::string path;
    if (!ShowFileDialog(false, path, "Открыть файл")) return;

    Handle(TDocStd_Document) loaded =
        BreezeApplication::Instance().OpenDocument(path);
    if (loaded.IsNull()) { std::cerr << "Open failed\n"; return; }

    AddDocFromFile(path, loaded);
    UI::Home::AddRecentFile(myRecentFiles, path);
    myVisibility.clear();
    RefreshView();
    if (myOcctView) myOcctView->FitAll();
}

// === Дерево ===

void Application::OnUndo() {
    if (!myDocument) return;
    if (myDocument->Undo()) {
        std::cout << "Undo OK\n";
        RefreshView();
        if (!myActiveSketchLabel.IsNull()) {
            // Если в эскизе — перерисовать линии
            if (myOcctView) myOcctView->ClearAllShapes();
            RefreshSketchLines();
            myOcctView->ShowGrid(myShowGrid);
        }
    } else {
        std::cout << "Undo: nothing to undo\n";
    }
}

void Application::OnRedo() {
    if (!myDocument) return;
    if (myDocument->Redo()) {
        std::cout << "Redo OK\n";
        RefreshView();
        if (!myActiveSketchLabel.IsNull()) {
            if (myOcctView) myOcctView->ClearAllShapes();
            RefreshSketchLines();
            myOcctView->ShowGrid(myShowGrid);
        }
    } else {
        std::cout << "Redo: nothing to redo\n";
    }
}

// Рисует кнопку Ribbon. active=false → красная рамка, "disabled".
static bool RibbonButton(const char* label, bool active, const char* tooltip = nullptr,
                          float width = 90, float height = 0) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(50, 90, 140, 255));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(120, 30, 30, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(160, 40, 40, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(180, 50, 50, 255));
    }
    bool clicked = active && ImGui::Button(label, ImVec2(width, height));
    if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    if (active) ImGui::PopStyleColor();
    else { ImGui::PopStyleColor(3); }
    return clicked;
}

// Большая кнопка Ribbon: верх — название, низ — полоска с ▼ для меню
// opts — массив указателей на ключи локализации (термины).
// idx — индекс выбранного в данный момент.
// Возвращает: 0 = не нажато, 1 = нажата основная, 2 = нажата стрелка ▼


// ============================================================
//  Вкладка «Эскиз»
// ============================================================


// Универсальная кнопка Ribbon с SVG-иконкой Lucide.
void Application::DrawRibbonAnnotateTab() {
    static int dummy = 0;
    static const char* oneOpt[] = { "tool.dummy" };

    // --- Размеры ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.dimensions").c_str());
    UI::Ribbon::SingleButton(myIcons, "dim_linear",   "tool.dim_linear",   ICON_DIM_LINEAR,   true, true);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "dim_angle",    "tool.dim_angle",    ICON_DIM_ANGLE,    true, true);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "dim_radius",   "tool.dim_radius",   ICON_DIM_RADIUS,   true, true);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "dim_diameter", "tool.dim_diameter", ICON_DIM_DIAMETER, true, true);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "dim_ordinate", "tool.dim_ordinate", ICON_DIM_ORDINATE, true, true);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Текст и символы ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.text").c_str());
    UI::Ribbon::SingleButton(myIcons, "leader",    "tool.leader",     ICON_LEADER,     true, false);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "text_ann",  "tool.text",       ICON_TEXT,       true, false);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "datum",     "tool.datum",      ICON_DATUM,      true, false);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "tolerance", "tool.tolerance",  ICON_TOLERANCE,  true, false);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "roughness", "tool.roughness",  ICON_ROUGHNESS,  true, false);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "weld",      "tool.weld",       ICON_WELD,       true, false);
    ImGui::EndGroup();
}

// ============================================================
//  Вкладка «Инструменты»
// ============================================================
void Application::DrawRibbonToolsTab() {
    static int dummy = 0;
    static const char* oneOpt[] = { "tool.dummy" };

    // --- Измерить ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.measure").c_str());
    UI::Ribbon::SingleButton(myIcons, "measure",  "tool.measure",  ICON_MEASURE,  true, false);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "distance", "tool.distance", ICON_DISTANCE, true, false);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "angle",    "tool.angle",    ICON_ANGLE,    true, false);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "area",     "tool.area",     ICON_AREA,     true, false);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "volume",   "tool.volume",   ICON_VOLUME,   true, false);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Анализ ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.analysis").c_str());
    UI::Ribbon::SingleButton(myIcons, "section",       "tool.section",       ICON_SECTION,       true, false);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "draft_analysis","tool.draft_analysis",ICON_DRAFT_ANALYSIS,true, false);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "thickness",     "tool.thickness",     ICON_THICKNESS,     true, false);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "curvature",     "tool.curvature",     ICON_CURVATURE,     true, false);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "zebra",         "tool.zebra",         ICON_ZEBRA,         true, false);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "interference",  "tool.interference",  ICON_INTERFERENCE,  true, false);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Свойства ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.properties").c_str());
    UI::Ribbon::SingleButton(myIcons, "model_props", "tool.model_props", ICON_MODEL_PROPS, true, false);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "mass",        "tool.mass",        ICON_MASS,        true, false);
    ImGui::EndGroup();
}

// ============================================================
//  Вкладка «Управление»
// ============================================================
void Application::DrawRibbonManageTab() {
    static int dummy = 0;
    static const char* oneOpt[] = { "tool.dummy" };

    // --- Параметры ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.parameters").c_str());
    UI::Ribbon::SingleButton(myIcons, "parameters", "tool.parameters", ICON_PARAMETERS, true, true);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "functions",  "tool.functions",  ICON_FUNCTION,   true, true);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "texts",      "tool.texts",      ICON_TEXT,       true, true);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Материалы (будущее) ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.materials").c_str());
    UI::Ribbon::SingleButton(myIcons, "material",   "tool.material",   ICON_MATERIAL,   true, false);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "texture",    "tool.texture",    ICON_TEXTURE,    true, false);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "appearance", "tool.appearance", ICON_APPEARANCE, true, false);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Обновление ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.update").c_str());
    UI::Ribbon::SingleButton(myIcons, "update",    "tool.update",    ICON_UPDATE,    true, false);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "recompute", "tool.recompute", ICON_RECOMPUTE, true, false);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "report",    "tool.report",    ICON_REPORT,    true, false);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Вставка ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.insert").c_str());
    UI::Ribbon::SingleButton(myIcons, "insert_obj", "tool.insert_obj", ICON_INSERT_OBJ, true, false);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "insert_img", "tool.insert_img", ICON_INSERT_IMG, true, false);
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "import",     "tool.import",     ICON_IMPORT,     true, false);
    ImGui::EndGroup();
}

// ============================================================
//  Вкладка «Вид»
// ============================================================
void Application::DrawRibbonAssemblyTab() {
    // === Компонент ===
    UI::Ribbon::GroupStart(myRibbonState);
    UI::Ribbon::SingleButton(myIcons, "asm_place",   "tool.asm.place",   "package-plus", true, true, "Вставить компонент");
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "asm_create",  "tool.asm.create",  "package",      true, true, "Создать компонент");
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "asm_replace", "tool.asm.replace", "refresh-cw",   true, true, "Заменить компонент");
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "asm_copy",    "tool.asm.copy",    ICON_COPY,      true, true, "Копировать компонент");
    UI::Ribbon::GroupEnd(Tr("grp.component").c_str(), myRibbonState);

    // === Позиционирование ===
    UI::Ribbon::GroupStart(myRibbonState);
    UI::Ribbon::SingleButton(myIcons, "asm_joint",      "tool.asm.joint",      "link",    true, true, "Соединение (подвижное)");
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "asm_constraint", "tool.asm.constraint", "pin",     true, true, "Зависимость (статичная)");
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "asm_dof",        "tool.asm.dof",        "move-3d", true, true, "Степени свободы");
    UI::Ribbon::GroupEnd(Tr("grp.position").c_str(), myRibbonState);

    // === Массив ===
    UI::Ribbon::GroupStart(myRibbonState);
    UI::Ribbon::SingleButton(myIcons, "asm_pat_rect", "tool.pattern_rect", ICON_PATTERN_RECT, true, false, "Прямоуг.");
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "asm_pat_circ", "tool.pattern_circ", ICON_PATTERN_CIRC, true, false, "Круговой");
    UI::Ribbon::SingleButton(myIcons, "asm_mirror",   "tool.mirror",       ICON_MIRROR,       true, false, "Зеркало");
    UI::Ribbon::GroupEnd(Tr("grp.pattern").c_str(), myRibbonState);

    // === Спецификация ===
    UI::Ribbon::GroupStart(myRibbonState);
    UI::Ribbon::SingleButton(myIcons, "asm_bom",  "tool.bom",           "list",      true, true, "Bill of Materials");
    ImGui::SameLine();
    UI::Ribbon::SingleButton(myIcons, "asm_spec", "tool.specification", ICON_FILE,   true, true, "Спецификация");
    UI::Ribbon::GroupEnd(Tr("grp.specification").c_str(), myRibbonState);

    // === Стандартные изделия ===
    UI::Ribbon::GroupStart(myRibbonState);
    UI::Ribbon::SingleButton(myIcons, "asm_library", "tool.library", "package", true, true, "Библиотека");
    UI::Ribbon::GroupEnd(Tr("grp.standards").c_str(), myRibbonState);
}

void Application::DrawRibbon() {
    // Тонкая обёртка: собираем RibbonContext и вызываем модуль.
    // Логика самого ribbon — в src/ui/ribbon/Ribbon.cpp (подшаг 10.2).
    UI::Ribbon::RibbonContext ctx;
    ctx.state = &myRibbonState;

    ctx.drawQat = [this]() {
        UI::Qat::QatContext qat;
        qat.icons               = &myIcons;
        qat.tooltip             = &myTooltipState;
        qat.searchBuffer        = mySearchBuffer;
        qat.searchBufferSize    = sizeof(mySearchBuffer);
        qat.commands            = &myCommands;
        qat.ramMB               = (float)myLastRAMMB;
        qat.shapeCount          = myLastShapeCount;
        qat.rightPanelExpanded  = myRightPanelExpanded;
        qat.onNewDocument    = [this]() { OnNewDocument(); };
        qat.onSave           = [this]() { OnSave(); };
        qat.onUndo           = [this]() { OnUndo(); };
        qat.onRedo           = [this]() { OnRedo(); };
        qat.onGoHome         = [this]() { UI::Home::CaptureCurrentThumbnail(*myOcctView, myActiveDocIndex, myThumbnailPaths); myShowHomeScreen = true; };
        qat.onOpenSettings   = [this]() { mySettingsState.show = true; };
        qat.onNotImplemented = [this](const char* feat) {
            myNotImplementedState.feature = feat;
            myNotImplementedState.show = true;
        };
        qat.onMinimize       = [this]() { glfwIconifyWindow(myWindow->GetNativeWindow()); };
        qat.onMaximize       = [this]() { myWindow->Maximize(); };
        qat.onClose          = [this]() { glfwSetWindowShouldClose(myWindow->GetNativeWindow(), GLFW_TRUE); };
        UI::Qat::DrawQuickAccessToolbar(qat);
    };

    ctx.icons    = &myIcons;

    ctx.drawSketchTab = [this]() {
        UI::Ribbon::SketchTabContext sctx;
        sctx.icons            = &myIcons;
        sctx.state            = &myRibbonState;
        sctx.inSketch         = !myActiveSketchLabel.IsNull();
        sctx.currentLineStyle = &myCurrentLineStyle;
        sctx.hasSketchStart   = &myHasSketchStart;
        sctx.clearPreviewLine = [this]() { if (myOcctView) myOcctView->ClearPreviewLine(); };
        sctx.exitSketch       = [this]() { ExitSketch(); };
        UI::Ribbon::DrawSketchTab(sctx);
    };
    ctx.drawAssemblyTab = [this]() { DrawRibbonAssemblyTab(); };

    UI::Ribbon::Draw(ctx);
}

void Application::DrawMenuBar() {
    // Оставлен для совместимости — но UI теперь полностью в DrawRibbon()
    // Вызывается из Run()
}

void Application::RefreshSketchLines() {
    if (!myDocument || !myOcctView || myActiveSketchLabel.IsNull()) return;

    // Обходим дочерние метки эскиза и показываем их линии
    TDF_ChildIterator it(myActiveSketchLabel, false);
    for (; it.More(); it.Next()) {
        TDF_Label lineLabel = it.Value();
        TopoDS_Shape s = myDocument->GetShape(lineLabel);
        if (!s.IsNull()) {
            LineStyle style = BreezeDocument::GetLineStyle(lineLabel);
            myOcctView->DisplaySketchLine(s, lineLabel, static_cast<int>(style));
        }
    }
}

// Вычисление длины и угла в плоскости эскиза
// Принудительное выравнивание угла к ближайшему кратному 90° (Компас-стиль)
// Проверка: попадает ли точка p в существующую вершину эскиза
// (Origin или конец уже нарисованной линии).
// skip — точка, которую нужно игнорировать (старт новой линии,
//        чтобы случайно не «замкнуться» на только что начатой линии).
static bool IsPointOnExistingVertex(const gp_Pnt& p, const TDF_Label& sketchLabel,
                                     const gp_Pnt& skip,
                                     double threshold) {
    auto dist2 = [](const gp_Pnt& a, const gp_Pnt& b) {
        double dx = a.X()-b.X(), dy = a.Y()-b.Y(), dz = a.Z()-b.Z();
        return dx*dx + dy*dy + dz*dz;
    };

    double th2 = threshold * threshold;

    // Origin
    gp_Pnt origin(0, 0, 0);
    if (dist2(p, origin) < th2 && dist2(skip, origin) > th2)
        return true;

    // Концы уже нарисованных линий эскиза
    TDF_ChildIterator it(sketchLabel, false);
    for (; it.More(); it.Next()) {
        TDF_Label lineLabel = it.Value();
        Handle(TNaming_NamedShape) ns;
        if (lineLabel.FindAttribute(TNaming_NamedShape::GetID(), ns) && !ns.IsNull()) {
            TopoDS_Shape s = ns->Get();
            if (!s.IsNull() && s.ShapeType() == TopAbs_EDGE) {
                TopoDS_Edge e = TopoDS::Edge(s);
                TopExp_Explorer exp(e, TopAbs_VERTEX);
                for (; exp.More(); exp.Next()) {
                    gp_Pnt vp = BRep_Tool::Pnt(TopoDS::Vertex(exp.Current()));
                    if (dist2(skip, vp) > th2 && dist2(p, vp) < th2) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

// Округление точки до сетки (шаг 10 мм) в плоскости эскиза.
// Привязка к известным точкам: Origin (0,0,0) + концы существующих линий эскиза
static bool SnapToKnownPoint(const gp_Pnt& p, const TDF_Label& sketchLabel,
                              const Handle(TDocStd_Document)& doc,
                              double threshold, gp_Pnt& out) {
    if (doc.IsNull() || sketchLabel.IsNull()) return false;

    std::vector<gp_Pnt> candidates;
    candidates.push_back(gp_Pnt(0, 0, 0));  // Origin

    // Концы линий эскиза
    TDF_ChildIterator it(sketchLabel, false);
    for (; it.More(); it.Next()) {
        TDF_Label lineLabel = it.Value();
        Handle(TNaming_NamedShape) ns;
        if (lineLabel.FindAttribute(TNaming_NamedShape::GetID(), ns) && !ns.IsNull()) {
            TopoDS_Shape s = ns->Get();
            if (!s.IsNull() && s.ShapeType() == TopAbs_EDGE) {
                TopoDS_Edge e = TopoDS::Edge(s);
                TopExp_Explorer exp(e, TopAbs_VERTEX);
                for (; exp.More(); exp.Next()) {
                    TopoDS_Vertex v = TopoDS::Vertex(exp.Current());
                    gp_Pnt vp = BRep_Tool::Pnt(v);
                    candidates.push_back(vp);
                }
            }
        }
    }

    double bestDist = threshold * threshold;
    bool found = false;
    for (const auto& c : candidates) {
        double dx = c.X() - p.X();
        double dy = c.Y() - p.Y();
        double dz = c.Z() - p.Z();
        double d2 = dx*dx + dy*dy + dz*dz;
        if (d2 < bestDist) {
            bestDist = d2;
            out = c;
            found = true;
        }
    }
    return found;
}

void Application::OnSketchClick(int px, int py) {
    if (!myOcctView || !myDocument || myActiveSketchLabel.IsNull()) return;

    // Если открыт диалог зависимостей — клик добавляет объекты в списки
    if (myDependencyState.open) {
        if (!myOcctView->SelectAtPixel(px, py)) return;

        TDF_Label sel = myOcctView->GetSelectedLabel();
        if (sel.IsNull()) return;

        // Проверяем, что это линия эскиза
        if (BreezeDocument::GetType(sel) != FT_SketchLine) {
            myOcctView->ClearSelection();
            return;
        }

        // Проверяем, нет ли уже этой метки в списках
        auto inList = [&](const std::vector<TDF_Label>& lst) {
            for (const auto& l : lst) if (l.IsEqual(sel)) return true;
            return false;
        };

        if (!inList(myDependencyState.base) && !inList(myDependencyState.dependent)) {
            // Первый клик — базовый, остальные — зависимые
            if (myDependencyState.base.empty()) {
                myDependencyState.base.push_back(sel);
                std::cout << "Dependency: base = " << Util::GetLabelName(sel) << "\n";
            } else {
                myDependencyState.dependent.push_back(sel);
                std::cout << "Dependency: dependent = " << Util::GetLabelName(sel) << "\n";
            }
        }

        myOcctView->ClearSelection();
        return;
    }

    gp_Pnt p;
    if (!myOcctView->ScreenToPlane(px, py, p)) return;

    // 1. Shift → ortho mode (45°)
    bool shiftHeld = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
                  || glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

    if (myHasSketchStart && shiftHeld) {
        p = ApplyOrtho(myOcctView->GetSketchPlane(), mySketchStartPoint, p);
    } else {
        // 2. Привязка к известным точкам
        bool snapped = false;
        if (mySnapToPoints) {
            gp_Pnt snapP;
            if (SnapToKnownPoint(p, myActiveSketchLabel, myDocument->GetDoc(),
                                  3.0, snapP)) {
                p = snapP;
                snapped = true;
            }
        }
        // 3. Привязка к сетке
        if (!snapped && mySnapToGrid) {
            p = SnapToGridPoint(p, myOcctView->GetSketchPlane());
        }
    }

    if (!myHasSketchStart) {
        mySketchStartPoint = p;
        myHasSketchStart = true;
        std::strcpy(myLengthBuffer, "0");
        std::strcpy(myAngleBuffer, "0");
        std::cout << "Sketch: start point set\n";
    } else {
        // Проверяем, что точки не совпадают
        double dx = p.X() - mySketchStartPoint.X();
        double dy = p.Y() - mySketchStartPoint.Y();
        double dz = p.Z() - mySketchStartPoint.Z();
        double dist2 = dx*dx + dy*dy + dz*dz;

        if (dist2 < 1e-4) {
            std::cout << "Sketch: points coincide, ignoring\n";
            return;
        }

        // 1. Проверка замыкания — ДО создания линии.
        //    Если конечная точка p совпадает с существующей вершиной эскиза
        //    (Origin или концом другой линии, кроме старта этой линии) — замыкание.
        bool loopClosed = IsPointOnExistingVertex(p, myActiveSketchLabel,
                                                   mySketchStartPoint,
                                                   0.5);  // порог 0.5 мм

        std::cout << "Sketch: loop check = " << (loopClosed ? "CLOSED" : "open")
                  << " (start was at " << mySketchStartPoint.X() << ","
                  << mySketchStartPoint.Y() << "," << mySketchStartPoint.Z()
                  << " end at " << p.X() << "," << p.Y() << "," << p.Z() << ")\n";

        std::cout << "Sketch: creating line with style="
                  << static_cast<int>(myCurrentLineStyle) << "\n";

        try {
            myDocument->AddSketchLine(myActiveSketchLabel, mySketchStartPoint, p, myCurrentLineStyle);
            myOcctView->ClearPreviewLine();

            TopoDS_Shape line = BRepBuilderAPI_MakeEdge(mySketchStartPoint, p).Shape();

            int maxTag = 0;
            TDF_ChildIterator it(myActiveSketchLabel, false);
            for (; it.More(); it.Next()) {
                if (it.Value().Tag() > maxTag) maxTag = it.Value().Tag();
            }
            TDF_Label lineLabel = myActiveSketchLabel.FindChild(maxTag, false);
            myOcctView->DisplaySketchLine(line, lineLabel, static_cast<int>(myCurrentLineStyle));

            if (myAutoContinue && !loopClosed) {
                mySketchStartPoint = p;
                myHasSketchStart = true;
                myOcctView->ShowCursorMarker(p);
                std::cout << "Sketch: line added, continuing\n";
            } else {
                myHasSketchStart = false;
                myOcctView->ClearPreviewLine();
                if (loopClosed) {
                    std::cout << "Sketch: line added, loop closed\n";
                } else {
                    std::cout << "Sketch: line added, stopped\n";
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Sketch: exception creating line: " << e.what() << "\n";
        } catch (...) {
            std::cerr << "Sketch: unknown exception creating line\n";
        }
    }
}

void Application::EnterSketch(const TDF_Label& sketchLabel) {
    if (!myDocument || !myOcctView || sketchLabel.IsNull()) return;

    // Родитель эскиза — плоскость. Берём её геометрию и вычисляем нормаль.
    TDF_Label parentPlane = sketchLabel.Father();
    if (parentPlane.IsNull()) {
        std::cerr << "EnterSketch: no parent plane\n";
        return;
    }

    TopoDS_Shape planeShape = myDocument->GetShape(parentPlane);
    if (planeShape.IsNull() || planeShape.ShapeType() != TopAbs_FACE) {
        std::cerr << "EnterSketch: parent is not a face\n";
        return;
    }

    TopoDS_Face face = TopoDS::Face(planeShape);
    BRepAdaptor_Surface surf(face);
    Standard_Real u = (surf.FirstUParameter() + surf.LastUParameter()) * 0.5;
    Standard_Real v = (surf.FirstVParameter() + surf.LastVParameter()) * 0.5;
    gp_Pnt p;
    gp_Vec du, dv;
    surf.D1(u, v, p, du, dv);
    gp_Dir normal = du.Crossed(dv);

    myOcctView->EnterSketchMode(normal, p);
    myOcctView->ShowGrid(myShowGrid);
    myActiveSketchLabel = sketchLabel;
    myHasSketchStart = false;
    myRibbonState.activeTab = 1;  // Sketch tab

    // Показать уже существующие линии эскиза
    RefreshSketchLines();

    std::cout << "Application: entered sketch mode\n";
}

void Application::ExitSketch() {
    myHasSketchStart = false;
    if (myOcctView) {
        myOcctView->ClearPreviewLine();
        myOcctView->ExitSketchMode();
    }
    myActiveSketchLabel = TDF_Label();
    myRibbonState.activeTab = 0;  // 3D Model tab

    // Принудительно перерисовываем всё из документа —
    // это гарантирует, что плоскости и другие системные объекты вернутся
    RefreshView();
    if (myOcctView) myOcctView->FitAll();

    std::cout << "Application: exited sketch mode\n";
}

void Application::ApplyExactLine() {
    if (!myOcctView || !myDocument || myActiveSketchLabel.IsNull()) return;
    if (!myHasSketchStart) return;

    double length = std::atof(myLengthBuffer);
    double angle  = std::atof(myAngleBuffer);

    if (length < 1e-6) {
        std::cout << "ApplyExactLine: length too small\n";
        return;
    }

    gp_Pnt p = PointFromLengthAngle(myOcctView->GetSketchPlane(),
                                     mySketchStartPoint,
                                     length, angle);

    try {
        myDocument->AddSketchLine(myActiveSketchLabel, mySketchStartPoint, p, myCurrentLineStyle);
        myOcctView->ClearPreviewLine();

        TopoDS_Shape line = BRepBuilderAPI_MakeEdge(mySketchStartPoint, p).Shape();
        int maxTag = 0;
        TDF_ChildIterator it(myActiveSketchLabel, false);
        for (; it.More(); it.Next()) {
            if (it.Value().Tag() > maxTag) maxTag = it.Value().Tag();
        }
        TDF_Label lineLabel = myActiveSketchLabel.FindChild(maxTag, false);
        myOcctView->DisplaySketchLine(line, lineLabel, static_cast<int>(myCurrentLineStyle));

        bool loopClosed = IsPointOnExistingVertex(p, myActiveSketchLabel,
                                                   mySketchStartPoint, 0.5);

        if (myAutoContinue && !loopClosed) {
            mySketchStartPoint = p;
            myHasSketchStart = true;
            myOcctView->ShowCursorMarker(p);
            std::cout << "ApplyExactLine: line added, continuing\n";
        } else {
            myHasSketchStart = false;
            if (loopClosed) std::cout << "ApplyExactLine: loop closed\n";
        }

        std::strcpy(myLengthBuffer, "0");
        std::strcpy(myAngleBuffer, "0");
        myInputActive = false;
    } catch (...) {
        std::cerr << "ApplyExactLine exception\n";
    }
}

void Application::DrawPreviewInfo() {
    if (myActiveSketchLabel.IsNull()) return;
    if (!myHasSketchStart) return;

    ImGuiIO& io = ImGui::GetIO();

    // Фиксированное положение — правый верхний угол, отступ 20 пикселей
    ImVec2 pos = ImVec2(io.DisplaySize.x - 280, 60);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(260, 0), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.9f);

    ImGui::Begin("##preview_info", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);

    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Length: %.2f mm", myPreviewLength);
    ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Angle:  %.1f deg", myPreviewAngle);

    ImGui::Separator();
    ImGui::Text("Exact input:");

    // Tab — фокус на Length
    static bool focusLength = false;
    if (ImGui::IsKeyPressed(ImGuiKey_Tab) && !ImGui::GetIO().WantTextInput) {
        focusLength = true;
    }

    ImGui::SetNextItemWidth(120);
    if (focusLength) {
        ImGui::SetKeyboardFocusHere();
        focusLength = false;
    }
    bool enterLength = ImGui::InputText("Length", myLengthBuffer, sizeof(myLengthBuffer),
                                         ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::SetNextItemWidth(120);
    bool enterAngle = ImGui::InputText("Angle", myAngleBuffer, sizeof(myAngleBuffer),
                                        ImGuiInputTextFlags_EnterReturnsTrue);

    if (enterLength || enterAngle) {
        ApplyExactLine();
    }

    ImGui::Separator();
    if (ImGui::Button("Apply", ImVec2(120, 0))) {
        ApplyExactLine();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        // Отмена текущей цепочки
        myHasSketchStart = false;
        myOcctView->ClearPreviewLine();
    }

    ImGui::TextDisabled("Tab - focus Length");
    ImGui::TextDisabled("Enter - apply");

    ImGui::End();
}

void Application::DrawSketchOverlay() {
    // Пустая — весь UI перенесён в Ribbon и Status Bar
}

void Application::DrawViewCube() {
    if (!myOcctView) return;
    UI::ViewCube::ViewCubeOverlay::Context ctx{
        *myOcctView,
        myTheme,
        ImGui::GetIO(),
        myRightPanelExpanded ? 110.0f : 55.0f
    };
    myViewCube.Draw(ctx);
}

void Application::RequestDelete(const TDF_Label& label) {
    if (label.IsNull()) return;

    if (myDeleteConfirmState.skipConfirm) {
        // Удаляем сразу
        if (myDocument) myDocument->RemoveFeature(label);
        RefreshView();
        if (myOcctView) myOcctView->FitAll();
        return;
    }

    myDeleteConfirmState.pendingLabel = label;

    // Собираем информацию: имя + сколько детей удалится
    std::string name = "<unnamed>";
    Handle(TDataStd_Name) nameAttr;
    if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
        name = ExtToUtf8(nameAttr->Get());
    }

    int childCount = 0;
    TDF_ChildIterator it(label, false);
    for (; it.More(); it.Next()) childCount++;

    if (childCount > 0) {
        std::snprintf(myDeleteConfirmState.info, sizeof(myDeleteConfirmState.info),
                      "Удалить «%s»?\n\nВместе с ним будут удалены %d вложенных элементов.",
                      name.c_str(), childCount);
    } else {
        std::snprintf(myDeleteConfirmState.info, sizeof(myDeleteConfirmState.info),
                      "Удалить «%s»?", name.c_str());
    }

    myDeleteConfirmState.show = true;
}

void Application::InitCommands() {
    myCommands.clear();

    auto add = [&](const std::string& id, const std::string& name,
                    const std::string& shortcut, std::function<void()> action) {
        myCommands.push_back({ id, name, shortcut, std::move(action) });
    };

    // Файл
    add("file.new",   "Создать",   "Ctrl+N",  [this]{ OnNewDocument(); });
    add("file.open",  "Открыть",   "Ctrl+O",  [this]{ OnOpen(); });
    add("file.save",  "Сохранить", "Ctrl+S",  [this]{ OnSave(); });

    // Правка
    add("edit.undo",  "Отменить",  "Ctrl+Z",  [this]{ OnUndo(); });
    add("edit.redo",  "Повторить", "Ctrl+Y",  [this]{ OnRedo(); });

    // Эскиз — инструменты
    add("sketch.line",   "Отрезок",         "L", [this]{
        myCurrentLineStyle = LS_Normal;
        myHasSketchStart = false;
        if (myOcctView) myOcctView->ClearPreviewLine();
    });
    add("sketch.rect",   "Прямоугольник",   "R", []{});
    add("sketch.circle", "Окружность",      "C", []{});
    add("sketch.arc",    "Дуга",            "A", []{});
    add("sketch.poly",   "Многоугольник",   "",  []{});
    add("sketch.finish", "Завершить эскиз", "Esc", [this]{ ExitSketch(); });

    // 3D
    add("model.extrude", "Выдавливание",   "E", []{});
    add("model.revolve", "Вращение",       "",  []{});
    add("model.hole",    "Отверстие",      "",  []{});

    // Настройки
    add("view.settings", "Настройки", "", [this]{ mySettingsState.show = true; });

    std::cout << "InitCommands: " << myCommands.size() << " commands loaded\n";
}

void Application::UpdateTelemetry() {
    // RAM через WinAPI (OCCT OSD_MemInfo под MinGW вызывает wmic,
    // которого нет в Windows 11 начиная с 24H2 — exit code 127).
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        myLastRAMMB = (double)pmc.WorkingSetSize / (1024.0 * 1024.0);
    } else {
        myLastRAMMB = 0.0;
    }
#else
    myLastRAMMB = 0.0;
#endif

    // Количество отображаемых объектов
    if (myOcctView) {
        myLastShapeCount = myOcctView->GetShapeCount();
    } else {
        myLastShapeCount = 0;
    }
}

void Application::Run() {
    bool prevF11 = false;
    bool prevDelete = false;
    bool prevCtrlS = false;
    bool prevCtrlO = false;

    while (!myWindow->ShouldClose()) {
      try {
        TRACE("iter: start");
        myWindow->PollEvents();

        // Перехват X / Alt+F4: если есть несохранённые — показать диалог
        if (glfwWindowShouldClose(myWindow->GetNativeWindow()) && !myUnsavedState.show) {
            int modIdx = -1;
            for (int i = 0; i < (int)myDocuments.size(); i++) {
                if (myDocuments[i]->IsModified()) { modIdx = i; break; }
            }
            if (modIdx >= 0) {
                glfwSetWindowShouldClose(myWindow->GetNativeWindow(), GLFW_FALSE);
                myUnsavedState.pendingWindowClose = true;
                myUnsavedState.pendingCloseDocIndex = modIdx;
                std::snprintf(myUnsavedState.info, sizeof(myUnsavedState.info),
                    "Проект «%s» содержит "
                    "несохранённые изменения.\n"
                    "Сохранить перед выходом?",
                    myProjectNames[modIdx].c_str());
                myUnsavedState.show = true;
            }
        }

        bool curF11 = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_F11) == GLFW_PRESS;
        if (curF11 && !prevF11) myWindow->ToggleFullscreen();
        prevF11 = curF11;

        // === Polling ПКМ через GLFW напрямую (ImGui колбэки перезаписаны нашими) ===
        if (!myDependencyState.open && !myRenameState.show &&
            !myDeleteConfirmState.show && !myPropertiesState.show) {
            ImGuiIO& io = ImGui::GetIO();

            // Мышь в области 3D-сцены?
            bool inScene = io.MousePos.x > myTreeWidth + 10
                        && io.MousePos.y > 165
                        && io.MousePos.x < io.DisplaySize.x - 320
                        && io.MousePos.y < io.DisplaySize.y - 70;

            static bool  prevRMB = false;
            static ImVec2 rmbStartPos(0, 0);

            bool curRMB = glfwGetMouseButton(myWindow->GetNativeWindow(),
                                              GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

            if (curRMB && !prevRMB && inScene) {
                rmbStartPos = io.MousePos;
            }
            if (!curRMB && prevRMB && inScene) {
                float dx = io.MousePos.x - rmbStartPos.x;
                float dy = io.MousePos.y - rmbStartPos.y;
                if (dx*dx + dy*dy < 25.0f) {
                    // ПКМ-клик без движения → меню
                    myPendingContextMenu = true;
                    myContextMenuPos = rmbStartPos;

                    // Выделяем объект под курсором
                    if (myOcctView) {
                        int winW = 1, winH = 1, fbW = 1, fbH = 1;
                        glfwGetWindowSize(myWindow->GetNativeWindow(), &winW, &winH);
                        glfwGetFramebufferSize(myWindow->GetNativeWindow(), &fbW, &fbH);
                        int px = (int)(rmbStartPos.x * fbW / winW);
                        int py = (int)(rmbStartPos.y * fbH / winH);
                        myOcctView->SelectAtPixel(px, py);
                    }
                }
                // Если было движение — вращение уже отработало в OcctView, ничего не делаем
            }
            prevRMB = curRMB;
        }

        // В режиме эскиза: всегда показываем маркер под курсором,
        // а если есть старт — ещё и линию-превью
        if (!myActiveSketchLabel.IsNull() && myOcctView) {
            if (!ImGui::GetIO().WantCaptureMouse) {
                static double lastPreviewX = -1e9, lastPreviewY = -1e9;

                double mx, my;
                glfwGetCursorPos(myWindow->GetNativeWindow(), &mx, &my);

                double dxp = mx - lastPreviewX;
                double dyp = my - lastPreviewY;
                if (dxp*dxp + dyp*dyp > 9.0) {  // сдвиг > 3 пикселей
                    lastPreviewX = mx;
                    lastPreviewY = my;

                    int winW = 1, winH = 1, fbW = 1, fbH = 1;
                    glfwGetWindowSize(myWindow->GetNativeWindow(), &winW, &winH);
                    glfwGetFramebufferSize(myWindow->GetNativeWindow(), &fbW, &fbH);
                    int px = (int)(mx * fbW / winW);
                    int py = (int)(my * fbH / winH);

                    gp_Pnt cur;
                    if (myOcctView->ScreenToPlane(px, py, cur)) {
                        bool shiftHeld = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
                                      || glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

                        if (myHasSketchStart && shiftHeld) {
                            cur = ApplyOrtho(myOcctView->GetSketchPlane(), mySketchStartPoint, cur);
                        } else {
                            bool curSnapped = false;
                            if (mySnapToPoints) {
                                gp_Pnt snapP;
                                if (SnapToKnownPoint(cur, myActiveSketchLabel,
                                                      myDocument->GetDoc(),
                                                      3.0, snapP)) {
                                    cur = snapP;
                                    curSnapped = true;
                                }
                            }
                            if (!curSnapped && mySnapToGrid) {
                                cur = SnapToGridPoint(cur, myOcctView->GetSketchPlane());
                            }
                        }

                        if (myHasSketchStart) {
                            myOcctView->ShowPreviewLine(mySketchStartPoint, cur);

                            // Считаем длину и угол для отображения
                            ComputeLengthAngle(myOcctView->GetSketchPlane(),
                                                mySketchStartPoint, cur,
                                                myPreviewLength, myPreviewAngle);
                            myHasPreview = true;
                        } else {
                            // Проверяем: подсветить зелёным, если точка попадает
                            // в существующую вершину (снап сработает при клике)
                            bool willSnap = IsPointOnExistingVertex(cur,
                                              myActiveSketchLabel,
                                              gp_Pnt(1e9, 1e9, 1e9),  // skip невозможно
                                              3.0);
                            myOcctView->ShowCursorMarkerHighlighted(cur, willSnap);
                            myHasPreview = false;
                        }
                    }
                }
            }

            static bool prevLMB = false;
            bool curLMB = glfwGetMouseButton(myWindow->GetNativeWindow(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            if (curLMB && !prevLMB) {
                // Получаем позицию курсора в координатах framebuffer
                double mx, my;
                glfwGetCursorPos(myWindow->GetNativeWindow(), &mx, &my);
                int winW = 1, winH = 1, fbW = 1, fbH = 1;
                glfwGetWindowSize(myWindow->GetNativeWindow(), &winW, &winH);
                glfwGetFramebufferSize(myWindow->GetNativeWindow(), &fbW, &fbH);
                int px = (int)(mx * fbW / winW);
                int py = (int)(my * fbH / winH);
                // Не обрабатывать клики по панелям ImGui
                if (!ImGui::GetIO().WantCaptureMouse) {
                    OnSketchClick(px, py);
                }
            }
            prevLMB = curLMB;
        }

        bool curDelete = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_DELETE) == GLFW_PRESS;
        if (curDelete && !prevDelete && myOcctView && myDocument) {
            TDF_Label sel = myOcctView->GetSelectedLabel();
            if (!sel.IsNull() && !BreezeDocument::IsSystem(sel)) {
                // Через диалог подтверждения
                RequestDelete(sel);
                myOcctView->ClearSelection();
            }
        }
        prevDelete = curDelete;

        bool ctrl = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS
                 || glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
        bool curCtrlS = ctrl && glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_S) == GLFW_PRESS;
        if (curCtrlS && !prevCtrlS) OnSave();
        prevCtrlS = curCtrlS;

        bool curCtrlO = ctrl && glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_O) == GLFW_PRESS;
        if (curCtrlO && !prevCtrlO) OnOpen();
        prevCtrlO = curCtrlO;

        // Ctrl+C — копировать выделенное
        {
            bool ctrl = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS
                     || glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
            static bool prevCtrlC = false;
            bool curCtrlC = ctrl && glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_C) == GLFW_PRESS;
            if (curCtrlC && !prevCtrlC && myOcctView) {
                TDF_Label sel = myOcctView->GetSelectedLabel();
                if (!sel.IsNull() && !BreezeDocument::IsSystem(sel)) {
                    myClipboardLabel = sel;
                    std::cout << "Copied: tag=" << sel.Tag() << "\n";
                }
            }
            prevCtrlC = curCtrlC;

            // Ctrl+V — вставить
            static bool prevCtrlV = false;
            bool curCtrlV = ctrl && glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_V) == GLFW_PRESS;
            if (curCtrlV && !prevCtrlV && !myClipboardLabel.IsNull()) {
                // TODO: клонирование
                std::cout << "Paste — TODO\n";
            }
            prevCtrlV = curCtrlV;
        }

        // Ctrl+C — копировать выделенное
        {
            bool ctrl = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS
                     || glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
            static bool prevCtrlC = false;
            bool curCtrlC = ctrl && glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_C) == GLFW_PRESS;
            if (curCtrlC && !prevCtrlC && myOcctView) {
                TDF_Label sel = myOcctView->GetSelectedLabel();
                if (!sel.IsNull() && !BreezeDocument::IsSystem(sel)) {
                    myClipboardLabel = sel;
                    std::cout << "Copied: tag=" << sel.Tag() << "\n";
                }
            }
            prevCtrlC = curCtrlC;

            static bool prevCtrlV = false;
            bool curCtrlV = ctrl && glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_V) == GLFW_PRESS;
            if (curCtrlV && !prevCtrlV && !myClipboardLabel.IsNull()) {
                std::cout << "Paste — TODO\n";
            }
            prevCtrlV = curCtrlV;
        }

        // Alt+V — переключить видимость выделенного объекта
        {
            bool altHeld = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_LEFT_ALT) == GLFW_PRESS
                        || glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
            static bool prevAltV = false;
            bool curAltV = altHeld && glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_V) == GLFW_PRESS;
            if (curAltV && !prevAltV && myOcctView && myDocument) {
                TDF_Label sel = myOcctView->GetSelectedLabel();
                if (!sel.IsNull()) {
                    std::string key = LabelKey(sel);
                    auto it = myVisibility.find(key);
                    bool curVis = (it != myVisibility.end()) ? it->second : true;
                    myVisibility[key] = !curVis;
                    myOcctView->SetShapeVisible(sel, !curVis);
                    std::cout << "Alt+V: visibility toggled for tag=" << sel.Tag()
                              << " -> " << (!curVis ? "visible" : "hidden") << "\n";
                }
            }
            prevAltV = curAltV;
        }

        // F6 — Home view + сброс изоляции
        static bool prevF6 = false;
        bool curF6 = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_F6) == GLFW_PRESS;
        if (curF6 && !prevF6 && myOcctView) {
            myOcctView->SetHomeView();
            if (myIsolationActive) {
                UI::Panels::ShowAllRespectingDefaults(myOcctView.get(), myDocument, myVisibility, myIsolationBackup, myIsolationActive);
            }
        }
        prevF6 = curF6;

        // Ctrl+Z — Undo
        static bool prevCtrlZ = false;
        bool curCtrlZ = ctrl && glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_Z) == GLFW_PRESS;
        if (curCtrlZ && !prevCtrlZ) OnUndo();
        prevCtrlZ = curCtrlZ;

        // Ctrl+Y — Redo
        static bool prevCtrlY = false;
        bool curCtrlY = ctrl && glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_Y) == GLFW_PRESS;
        if (curCtrlY && !prevCtrlY) OnRedo();
        prevCtrlY = curCtrlY;

        // Esc — сначала прерывает текущую линию, потом выходит из эскиза
        static bool prevEsc = false;
        bool curEsc = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_ESCAPE) == GLFW_PRESS;
        if (curEsc && !prevEsc && !myActiveSketchLabel.IsNull()) {
            if (myHasSketchStart) {
                // Прервать текущую цепочку линий
                myHasSketchStart = false;
                if (myOcctView) myOcctView->ClearPreviewLine();
                myHasPreview = false;
                std::cout << "Sketch: line chain cancelled\n";
            } else {
                // Нечего прерывать — выходим из эскиза
                ExitSketch();
            }
        }
        prevEsc = curEsc;

        glfwMakeContextCurrent(myWindow->GetNativeWindow());
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Запоминаем позицию мыши для превью
        {
            double mx, my;
            glfwGetCursorPos(myWindow->GetNativeWindow(), &mx, &my);
            myMouseX = mx;
            myMouseY = my;
        }

        if (!myShowHomeScreen) {
            DrawRibbon();
            UI::Panels::DrawDocumentTree({
                myDocument, myOcctView.get(), &myIcons,
                &myVisibility, &myIsolationBackup,
                &myAddBoxState, &myRenameState, &myDeleteConfirmState, &myPropertiesState,
                &myStartExpandedManually, &myIsolationActive,
                &myPendingCreateSketchLabel, &myPendingEnterSketchLabel, &myClipboardLabel,
                &myPendingContextMenu, &myContextMenuPos,
                [this]() { OnSave(); },
                [this]() { OnOpen(); },
                [this](const TDF_Label& l) { EnterSketch(l); },
                [this]() { UI::Panels::ShowAllRespectingDefaults(myOcctView.get(), myDocument, myVisibility, myIsolationBackup, myIsolationActive); },
                [this](const TDF_Label& l) { RequestDelete(l); },
                [this](const TDF_Label& l) {
                    myRenameState.pendingLabel = l;
                    std::string nm = Util::GetLabelName(l);
                    std::strncpy(myRenameState.buffer, nm.c_str(), sizeof(myRenameState.buffer) - 1);
                    myRenameState.buffer[sizeof(myRenameState.buffer) - 1] = '\0';
                    myRenameState.show = true;
                },
                [this](const TDF_Label& l) {
                    myPropertiesState.pendingLabel = l;
                    myPropertiesState.show = true;
                }
            });
            UI::Panels::DrawTreeSplitter(myTreeWidth);
            UI::Dialogs::DrawAddBoxDialog({ &myAddBoxState, myDocument,
                [this]() { RefreshView(); } });
            DrawPreviewInfo();
            DrawViewCube();
            UI::Dialogs::DrawDependencyDialog({ &myDependencyState, myOcctView.get(),
                [](const char* t) { std::cout << "Dependency '" << t << "' applied\n"; } });
            UI::Dialogs::DrawRenameDialog({ &myRenameState, myDocument });
            UI::Dialogs::DrawDeleteConfirmDialog({ &myDeleteConfirmState, myDocument,
                [this]() { RefreshView(); if (myOcctView) myOcctView->FitAll(); } });
            UI::Dialogs::DrawUnsavedChangesDialog({ &myUnsavedState, (int)myDocuments.size(),
                [this](int idx) -> bool {
                    if (idx < 0 || idx >= (int)myDocuments.size()) return true;
                    BreezeDocument* prevDoc = myDocument;
                    int prevIdx = myActiveDocIndex;
                    myDocument = myDocuments[idx].get();
                    myActiveDocIndex = idx;
                    OnSave();
                    bool saved = !myDocument->IsModified();
                    if (!saved) { myDocument = prevDoc; myActiveDocIndex = prevIdx; }
                    return saved;
                },
                [this](int idx) { if (idx >= 0 && idx < (int)myDocuments.size()) myDocuments[idx]->SetModified(false); },
                [this](int idx) { CloseDoc(idx); },
                [this]() { glfwSetWindowShouldClose(myWindow->GetNativeWindow(), GLFW_TRUE); } });
            UI::Dialogs::DrawPropertiesDialog({ &myPropertiesState, myDocument });
            UI::Panels::DrawContextMenu3D({
                myDocument, myOcctView.get(), &myIcons,
                &myVisibility, &myIsolationBackup,
                &myAddBoxState, &myRenameState, &myDeleteConfirmState, &myPropertiesState,
                &myStartExpandedManually, &myIsolationActive,
                &myPendingCreateSketchLabel, &myPendingEnterSketchLabel, &myClipboardLabel,
                &myPendingContextMenu, &myContextMenuPos,
                [this]() { OnSave(); },
                [this]() { OnOpen(); },
                [this](const TDF_Label& l) { EnterSketch(l); },
                [this]() { UI::Panels::ShowAllRespectingDefaults(myOcctView.get(), myDocument, myVisibility, myIsolationBackup, myIsolationActive); },
                [this](const TDF_Label& l) { RequestDelete(l); },
                [this](const TDF_Label& l) {
                    myRenameState.pendingLabel = l;
                    myRenameState.show = true;
                },
                [this](const TDF_Label& l) {
                    myPropertiesState.pendingLabel = l;
                    myPropertiesState.show = true;
                }
            });
            UI::Widgets::DrawSelectionRect(*myOcctView, myWindow->GetNativeWindow());
            UI::Panels::DrawDependenciesPanel(!myActiveSketchLabel.IsNull());
            UI::Panels::DrawRightPanel({
                &myRightPanelExpanded, myIsolationActive,
                &myIcons, myOcctView.get(), &myTooltipState,
                [this]() {
                    if (myIsolationActive) UI::Panels::ShowAllRespectingDefaults(myOcctView.get(), myDocument, myVisibility, myIsolationBackup, myIsolationActive);
                    if (myOcctView) myOcctView->FitAll();
                } });
        }
        {
            UI::Home::HomeContext hs;
            hs.projectNames     = &myProjectNames;
            hs.projectPaths     = &myProjectPaths;
            hs.thumbnails       = &myThumbnailPaths;
            hs.documentCount    = (int)myDocuments.size();
            hs.showHomeScreen   = &myShowHomeScreen;
            hs.viewMode         = &myHomeViewMode;
            hs.searchBuffer     = myHomeSearch;
            hs.searchBufferSize = sizeof(myHomeSearch);
            hs.icons            = &myIcons;
            hs.theme            = &myTheme;
            hs.ramMB            = myLastRAMMB;
            hs.onSwitchDoc      = [this](int i) { SwitchToDoc(i); };
            hs.onCloseDoc       = [this](int i) { CloseDoc(i); };
            hs.onCreateDoc      = [this]() { CreateNewDoc(); RefreshView(); if (myOcctView) myOcctView->FitAll(); };
            hs.onOpenFile       = [this]() { OnOpen(); };
            hs.onShowSettings   = [this]() { mySettingsState.show = true; };
            hs.onNotImplemented = [this](const char* feat) {
                myNotImplementedState.feature = feat;
                myNotImplementedState.show = true;
            };
            hs.onQuit           = [this]() { glfwSetWindowShouldClose(myWindow->GetNativeWindow(), GLFW_TRUE); };
            UI::Home::DrawHomeScreen(hs);
        }
        {
            UI::Status::StatusInfo info;
            info.sketchActive       = !myActiveSketchLabel.IsNull();
            info.snapToGrid         = mySnapToGrid;
            info.snapToPoints       = mySnapToPoints;
            info.mouseX             = myMouseX;
            info.mouseY             = myMouseY;
            info.rightPanelExpanded = myRightPanelExpanded;
            UI::Status::DrawStatusBar(info);
        }
        {
            UI::Status::ProjectTabsContext ctx;
            ctx.names         = &myProjectNames;
            ctx.paths         = &myProjectPaths;
            ctx.activeIndex   = myActiveDocIndex;
            ctx.documentCount = (int)myDocuments.size();
            ctx.onHome        = myShowHomeScreen;
            ctx.icons         = &myIcons;
            ctx.onGoHome      = [this]() { myShowHomeScreen = true; };
            ctx.onCreate      = [this]() {
                CreateNewDoc();
                RefreshView();
                if (myOcctView) myOcctView->FitAll();
            };
            ctx.onSwitch      = [this](int i) { SwitchToDoc(i); };
            ctx.onClose       = [this](int i) { CloseDoc(i); };
            UI::Status::DrawProjectTabs(ctx);
        }
        UI::Dialogs::DrawSettingsDialog({ &mySettingsState, &myCurrentTheme,
            [this](int t) { ApplyTheme(t); },
            [](Language l) { Localization::Instance().SetLanguage(l); } });

        UI::Dialogs::DrawNotImplementedDialog(myNotImplementedState);
        TRACE("iter: before UpdateTelemetry");
        UpdateTelemetry();
        TRACE("iter: after UpdateTelemetry");

        ImGui::Render();

        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(myWindow->GetNativeWindow(), &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (myOcctView) myOcctView->Redraw();

        glViewport(0, 0, fbW, fbH);
        glfwMakeContextCurrent(myWindow->GetNativeWindow());
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        TRACE("iter: before SwapBuffers");
        myWindow->SwapBuffers();
        TRACE("iter: after SwapBuffers");
      } catch (const std::exception& e) {
        std::cerr << "Runtime exception in main loop: " << e.what() << "\n";
      } catch (...) {
        std::cerr << "Unknown exception in main loop (ignored)\n";
      }
    }
}

} // namespace BreezeCAD
