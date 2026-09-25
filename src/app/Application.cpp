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
#include "util/Utf8.h"
#include "util/Geometry2D.h"              // Util::ComputeLengthAngle и др.                    // Util::Utf8ToExt / ExtToUtf8       // UI::Qat::DrawQuickAccessToolbar
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

static std::string LabelKey(const TDF_Label& label) {
    std::ostringstream oss;
    label.EntryDump(oss);
    return oss.str();
}

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

void Application::CaptureCurrentThumbnail() {
    if (!myOcctView || myActiveDocIndex < 0) return;

    // Папка для превью
    std::string dir = "D:/test_breezecad/thumbs/";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    char filename[64];
    std::snprintf(filename, sizeof(filename), "thumb_%d.bmp", myActiveDocIndex);
    std::string path = dir + filename;

    if (myOcctView->CaptureToPixmap(320, 240, path)) {
        // Расширяем вектор до нужного размера
        if ((int)myThumbnailPaths.size() <= myActiveDocIndex) {
            myThumbnailPaths.resize(myActiveDocIndex + 1);
        }
        myThumbnailPaths[myActiveDocIndex] = path;
        std::cout << "Thumbnail saved: " << path << "\n";
    }
}

std::string Application::GetThumbnailPath(int docIndex) const {
    if (docIndex < 0 || docIndex >= (int)myThumbnailPaths.size()) return "";
    return myThumbnailPaths[docIndex];
}

void Application::AddRecentFile(const std::string& path) {
    if (path.empty()) return;

    // Убираем дубликат, если был
    myRecentFiles.erase(
        std::remove(myRecentFiles.begin(), myRecentFiles.end(), path),
        myRecentFiles.end());

    // Вставляем в начало
    myRecentFiles.insert(myRecentFiles.begin(), path);

    // Ограничиваем 10 записями
    if (myRecentFiles.size() > 10) myRecentFiles.resize(10);
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
    CaptureCurrentThumbnail();

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
        myPendingCloseDocIndex = index;
        myPendingWindowClose = false;
        std::snprintf(myUnsavedDialogInfo, sizeof(myUnsavedDialogInfo),
            "Проект «%s» содержит "
            "несохранённые изменения.\n"
            "Сохранить перед закрытием?",
            myProjectNames[index].c_str());
        myShowUnsavedDialog = true;
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
        AddRecentFile(path);
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
    AddRecentFile(path);
    myVisibility.clear();
    RefreshView();
    if (myOcctView) myOcctView->FitAll();
}

// === Дерево ===

bool Application::DrawLabelTree(const TDF_Label& label,
                                 const TDF_Label& selectedLabel,
                                 bool hasOps) {
    // Линии эскиза не показываем в дереве
    int _earlyType = BreezeDocument::GetType(label);
    if (_earlyType == FT_SketchLine) return false;

    std::string labelName = "<unnamed>";
    Handle(TDataStd_Name) nameAttr;
    if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
        labelName = ExtToUtf8(nameAttr->Get());
    }

    int type = BreezeDocument::GetType(label);
    bool isSystem = (type < 0);

    TDF_ChildIterator childIt(label, false);
    bool hasChildren = childIt.More();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                ImGuiTreeNodeFlags_SpanAvailWidth |
                                ImGuiTreeNodeFlags_DefaultOpen;
    if (!hasChildren)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (!selectedLabel.IsNull() && selectedLabel.IsEqual(label))
        flags |= ImGuiTreeNodeFlags_Selected;

    ImGui::PushID(LabelKey(label).c_str());

    // Чекбокс видимости для системных элементов с геометрией
    if (isSystem && (type == FT_Plane || type == FT_Axis)) {
        std::string key = LabelKey(label);
        auto it = myVisibility.find(key);
        bool visible;
        if (it != myVisibility.end()) {
            visible = it->second;
        } else {
            // По умолчанию: скрыто, если есть 3D-операции
            visible = !hasOps;
        }

                const char* eyeName = visible ? "eye" : "eye-off";
        std::string eyeId = "eye_" + LabelKey(label);
        if (UI::Widgets::SvgIconBtn(myIcons, eyeId.c_str(), eyeName, 14, 22, 18,
                        visible ? "Скрыть" : "Показать", true)) {
            myVisibility[key] = !visible;
            if (myOcctView) myOcctView->SetShapeVisible(label, !visible);
        }
        ImGui::SameLine();
    }

    if (isSystem) {
        // Системные элементы — цвет текста темы без изменений
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
    }
    bool opened = ImGui::TreeNodeEx(labelName.c_str(), flags);
    if (isSystem) ImGui::PopStyleColor();

    // Двойной клик по эскизу → войти в режим 2D
    if (type == FT_Sketch && ImGui::IsItemHovered()
        && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        myPendingEnterSketchLabel = label;
    }

    // === Контекстное меню (ПКМ) ===
    if (ImGui::BeginPopupContextItem("##ctx")) {
        ImGui::TextDisabled("%s", labelName.c_str());
        ImGui::Separator();
        DrawContextMenuItems(label, hasOps);
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("[type=%d, tag=%d]", type, label.Tag());

    bool requested = false;
    if (opened && hasChildren) {
        TDF_ChildIterator it(label, false);
        for (; it.More(); it.Next()) {
            if (DrawLabelTree(it.Value(), selectedLabel, hasOps)) requested = true;
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
    return requested || !myPendingDeleteLabel.IsNull()
                     || !myPendingCreateSketchLabel.IsNull();
}

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

void Application::DrawRibbonTabs() {
    static const char* tabs[] = {
        "tab.model", "tab.sketch", "tab.assembly", "tab.view"
    };
    for (int i = 0; i < 4; i++) {
        if (i > 0) ImGui::SameLine();
        bool active = (myActiveRibbonTab == i);
        bool isLight = (ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).x > 0.5f);
        if (active) {
            if (isLight) {
                // Светлая — серебристый C3
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.698f, 0.714f, 0.737f, 1.0f));
            } else {
                // Тёмная — синий accent
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.95f, 1.0f));
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Button));
        }
        if (ImGui::Button(Tr(tabs[i]).c_str(), ImVec2(120, 26))) {
            myActiveRibbonTab = i;
        }
        ImGui::PopStyleColor();
    }
    // Кнопка "+" для подключения модулей
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Button));
    if (UI::Widgets::SvgIconBtn(myIcons, "ribbon_plus", "plus", 16, 32, 26, "Подключить модуль")) {
        // TODO: диалог подключения модуля
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Подключить модуль");
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
void Application::RibbonGroupStart() {
    myRibbonGroupStartY = ImGui::GetCursorScreenPos().y;
    ImGui::BeginGroup();
}

void Application::RibbonGroupEnd(const char* name) {
    // Подтягиваем группу до общей высоты TARGET_H
    float curY = ImGui::GetCursorScreenPos().y;
    float groupH = curY - myRibbonGroupStartY;
    const float TARGET_H = 76.0f;
    if (groupH < TARGET_H) {
        ImGui::Dummy(ImVec2(1.0f, TARGET_H - groupH));
    }
    ImGui::EndGroup();

    ImVec2 pMin = ImGui::GetItemRectMin();
    ImVec2 pMax = ImGui::GetItemRectMax();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool isLight = (ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).x > 0.5f);

    // Фон группы — включает кнопки И подпись
    ImU32 bg = isLight ? IM_COL32(255, 255, 255, 60)
                       : IM_COL32(255, 255, 255, 12);
    dl->AddRectFilled(
        ImVec2(pMin.x - 6, pMin.y - 4),
        ImVec2(pMax.x + 6, pMax.y + 18),
        bg, 4.0f);

    // Вертикальный разделитель справа от группы
    dl->AddLine(
        ImVec2(pMax.x + 10, pMin.y - 2),
        ImVec2(pMax.x + 10, pMax.y + 16),
        isLight ? IM_COL32(160, 160, 160, 140) : IM_COL32(90, 100, 115, 180),
        1.0f);

    // Заголовок группы — снизу по центру (без изменения layout)
    ImVec2 tsz = ImGui::CalcTextSize(name);
    float cx = (pMin.x + pMax.x) * 0.5f - tsz.x * 0.5f;
    ImU32 tcol = isLight ? IM_COL32(90, 90, 90, 230)
                         : IM_COL32(180, 185, 195, 220);
    dl->AddText(ImVec2(cx, pMax.y + 3), tcol, name);

    // Продолжаем на той же строке
    ImGui::SameLine(0, 22);
}

int Application::RibbonButton(const char* id, const char* const* opts,
                              int optCount, int& idx, const char* icon,
                              bool enabled, bool big, const char* tooltip,
                              float iconScale) {
    int result = 0;
    const float bigW = 76.0f, bigH = 76.0f;
    const float smallW = 32.0f, smallH = 32.0f;
    const float arrowW = 14.0f;
    float w = big ? bigW : smallW;
    float h = big ? bigH : smallH;
    const int iconSize = (int)((big ? 32 : 24) * iconScale);
    const float o = 4.0f;
    bool hasArrow = (optCount > 1);
    float totalW = w + (hasArrow ? arrowW : 0.0f);

    bool isLight = (ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).x > 0.5f);
    ImVec4 bgHover, bgActive;
    if (isLight) {
        bgHover  = ImVec4(0.760f, 0.800f, 0.860f, 0.6f);
        bgActive = ImVec4(0.640f, 0.720f, 0.840f, 0.8f);
    } else {
        bgHover  = ImVec4(0.35f, 0.55f, 0.85f, 0.35f);
        bgActive = ImVec4(0.35f, 0.55f, 0.85f, 0.55f);
    }
    if (!enabled) {
        bgHover.w *= 0.4f; bgActive.w *= 0.4f;
    }

    ImVec4 tint = UI::Widgets::IconTintColor(enabled);
    GLuint tex = myIcons.Get(icon, iconSize);
    ImTextureID tid = (ImTextureID)(intptr_t)tex;

    ImGui::PushID(id);
    ImGui::BeginGroup();

    ImVec2 startPos = ImGui::GetCursorScreenPos();

    // Кликабельная область на всю ширину (кнопка + стрелка)
    ImGui::InvisibleButton("##click", ImVec2(totalW, h));
    bool clicked = ImGui::IsItemClicked();
    bool hovered = ImGui::IsItemHovered();
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();

    // Определяем: попали в стрелку?
    ImVec2 mp = ImGui::GetIO().MousePos;
    bool inArrow = hasArrow && (mp.x >= min.x + w);

    if (tooltip && hovered && !inArrow) ImGui::SetTooltip("%s", tooltip);
    if (inArrow && hovered) ImGui::SetTooltip("Открыть список вариантов");

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Основная кнопка — hover только если мышь НЕ на стрелке
    ImVec2 mainMax(min.x + w, max.y);
    if (hovered && enabled && !inArrow) {
        ImU32 bgCol = ImGui::GetColorU32(bgHover);
        dl->AddRectFilled(min, mainMax, bgCol, 4.0f);
    }

    // Полоска стрелки справа — светло-подкрашена
    if (hasArrow) {
        ImU32 arrowBg;
        if (isLight) {
            arrowBg = enabled ? IM_COL32(220, 225, 232, 220)
                              : IM_COL32(210, 215, 220, 140);
        } else {
            arrowBg = enabled ? IM_COL32(60, 70, 86, 200)
                              : IM_COL32(50, 58, 72, 130);
        }
        if (hovered && enabled && inArrow) {
            arrowBg = ImGui::GetColorU32(bgActive);
        }
        dl->AddRectFilled(ImVec2(min.x + w, min.y), max, arrowBg, 4.0f);
    }

    // Иконка
    if (tid != 0) {
        ImVec2 iconPos(min.x + (w - iconSize) * 0.5f, min.y + o);
        dl->AddImage(tid, iconPos,
                     ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
                     ImVec2(0, 0), ImVec2(1, 1),
                     ImGui::GetColorU32(tint));
    } else {
        ImVec2 p1(min.x + (w - iconSize) * 0.5f, min.y + o);
        dl->AddRectFilled(p1, ImVec2(p1.x + iconSize, p1.y + iconSize),
                          IM_COL32(140, 140, 140, 160), 4.0f);
    }

    // Текст — под иконкой. Формула:
    //   a = h (высота кнопки), b = iconSize, o = 4
    //   Полоса текста: [min.y + o + b + o, min.y + a - o]
    if (big && optCount > 0) {
        std::string label = Tr(opts[idx]);
        float maxTextW = w - 4.0f;
        ImFont* font = ImGui::GetFont();
        float baseSize = ImGui::GetFontSize();
        ImU32 txtCol = ImGui::GetColorU32(tint);  // тот же цвет, что иконка

        float textTop    = min.y + o + iconSize + o;
        float textBottom = min.y + h - o;

        auto fits = [&](const std::string& s, float fs) {
            return font->CalcTextSizeA(fs, FLT_MAX, 0.0f, s.c_str()).x <= maxTextW;
        };
        auto splitMiddle = [&](const std::string& s) {
            std::vector<size_t> spaces;
            for (size_t i = 0; i < s.size(); ++i) if (s[i] == ' ') spaces.push_back(i);
            if (spaces.empty()) return std::pair<std::string,std::string>{s, ""};
            size_t pos = spaces[spaces.size() / 2];
            return std::pair<std::string,std::string>{s.substr(0, pos), s.substr(pos + 1)};
        };

        bool done = false;
        auto drawBottom = [&](const std::vector<std::string>& ls, float fs) {
            float lineH = fs + 1.0f;
            float y = textBottom - lineH * (float)ls.size();
            for (auto& s : ls) {
                ImVec2 sz = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, s.c_str());
                dl->AddText(font, fs,
                            ImVec2(min.x + (w - sz.x) * 0.5f, y),
                            txtCol, s.c_str());
                y += lineH;
            }
        };

        float sizes[] = { baseSize, baseSize * 0.82f, baseSize * 0.62f, baseSize * 0.52f };
        for (float fs : sizes) {
            if (done) break;
            if (fits(label, fs)) { drawBottom({label}, fs); done = true; break; }
            auto [l1, l2] = splitMiddle(label);
            if (!l2.empty() && fits(l1, fs) && fits(l2, fs)) {
                if (textTop + 2.0f * (fs + 1.0f) <= textBottom) {
                    drawBottom({l1, l2}, fs); done = true; break;
                }
            }
        }
        if (!done) drawBottom({label}, baseSize * 0.45f);
    }

    // Стрелка ▼ — вертикально по центру правой полоски
    if (hasArrow) {
        GLuint arrowTex = myIcons.Get("chevron-down", 10);
        if (arrowTex != 0) {
            float ax = min.x + w + (arrowW - 10) * 0.5f;
            float ay = min.y + (h - 10) * 0.5f;
            dl->AddImage((ImTextureID)(intptr_t)arrowTex,
                         ImVec2(ax, ay), ImVec2(ax + 10, ay + 10),
                         ImVec2(0, 0), ImVec2(1, 1),
                         ImGui::GetColorU32(tint));
        }
    }

    // Клик: по стрелке — popup, по кнопке — action
    if (clicked && enabled) {
        if (inArrow) result = 2;
        else result = 1;
    }

    ImGui::EndGroup();

    if (result == 2) ImGui::OpenPopup("##popup");
    if (ImGui::BeginPopup("##popup")) {
        for (int i = 0; i < optCount; i++) {
            bool selected = (i == idx);
            if (ImGui::MenuItem(Tr(opts[i]).c_str(), nullptr, selected)) {
                idx = i;
                result = 3;
            }
        }
        ImGui::EndPopup();
    }

    ImGui::PopID();
    return result;
}

int Application::RibbonSingleButton(const char* id, const char* key,
                                    const char* icon, bool enabled, bool big,
                                    const char* tooltip, float iconScale) {
    static int dummy = 0;
    const char* opts[] = { key };
    return RibbonButton(id, opts, 1, dummy, icon, enabled, big, tooltip, iconScale);
}

void Application::DrawRibbonSketchTab() {
    bool inSketch = !myActiveSketchLabel.IsNull();
    static int dummy = 0;

    // Отрезок: Отрезок / Сплайн(упр.верш.) / Сплайн(интерп.) / Кривая по формуле / Замкнуть
    static const char* lineOpts[]   = {
        "tool.line", "tool.spline.ctrl", "tool.spline.interp",
        "tool.curve.fx", "tool.close"
    };
    // Прямоугольник: 4 прямоугольника + 5 пазов + Многоугольник
    static const char* rectOpts[]   = {
        "tool.rect.2pt", "tool.rect.3pt",
        "tool.rect.2pt.center", "tool.rect.3pt.center",
        "tool.slot.2center", "tool.slot.common", "tool.slot.center",
        "tool.slot.arc3pt", "tool.slot.arc.center",
        "tool.polygon"
    };
    // Окружность: Окружность (центр) / Окружность (касательная) / Эллипс
    static const char* circleOpts[] = { "tool.circle.center", "tool.circle.tan", "tool.ellipse" };
    // Дуга: по трём точкам / касательная / центр
    static const char* arcOpts[]    = { "tool.arc.3pt", "tool.arc.tan", "tool.arc.center" };
    static const char* pointOpt[]   = { "tool.point" };
    static const char* textOpt[]    = { "tool.text", "tool.text.line" };
    // Сопряжение: Сопряжение / Фаска
    static const char* fillet2dOpts[] = { "tool.fillet.2d", "tool.chamfer.2d" };
    static const char* projOpt[] = {
        "tool.proj.geom", "tool.project.loop", "tool.proj.unfold",
        "tool.proj.3d", "tool.proj.dwg"
    };
    static int projIdx = 0;
    static const char* trimOpt[]    = { "tool.trim", "tool.extend" };
    static const char* offsetOpt[]  = { "tool.offset" };
    static const char* moveOpt[]    = { "tool.move", "tool.copy", "tool.rotate" };
    static const char* scaleOpt[]   = { "tool.scale", "tool.stretch" };
    static const char* splitOpt[]   = { "tool.split" };
    static const char* finishOpt[]  = { "tool.finish" };
    static const char* clearOpt[]   = { "tool.clear" };
    static const char* showFormatOpt[] = { "tool.show_format" };

    // ============================================================
    // СОЗДАТЬ — 6 больших в один ряд
    // ============================================================
    RibbonGroupStart();
    {
        int r = RibbonButton("line", lineOpts, 5, myLineToolIndex, ICON_LINE, inSketch, true,
                             "Отрезок");
        if (r == 1 || r == 3) {
            myCurrentLineStyle = LS_Normal;
            myHasSketchStart = false;
            if (myOcctView) myOcctView->ClearPreviewLine();
        }
    }
    ImGui::SameLine();
    RibbonButton("rect",   rectOpts,   10, myRectToolIndex,   ICON_RECT,    inSketch, true, "Прямоугольник");
    ImGui::SameLine();
    RibbonButton("circle", circleOpts, 3,  myCircleToolIndex, ICON_CIRCLE,  inSketch, true, "Окружность");
    ImGui::SameLine();
    RibbonButton("arc",    arcOpts,    3,  myArcToolIndex,    ICON_ARC,     inSketch, true, "Дуга");
    ImGui::SameLine();
    RibbonButton("point",  pointOpt,   1,  dummy,             ICON_POINT,   inSketch, true, "Точка");
    ImGui::SameLine();
    RibbonButton("text",   textOpt,    2,  dummy,             ICON_TEXT,    inSketch, true, "Текст");
    ImGui::SameLine();
    RibbonButton("fillet_2d", fillet2dOpts, 2, dummy, ICON_FILLET, inSketch, true, "Сопряжение");
    RibbonGroupEnd(Tr("grp.create").c_str());

    // ============================================================
    // ПРОЕЦИРОВАТЬ — 2 малые иконки в ряд
    // ============================================================
    RibbonGroupStart();
    RibbonButton("proj_geom", projOpt, 5, projIdx, ICON_PROJECT, inSketch, true, "Проецирование");
    RibbonGroupEnd(Tr("grp.project").c_str());

    // ============================================================
    // МАССИВ — 3 малые иконки в столбик
    // ============================================================
    RibbonGroupStart();
    RibbonButton("pat_rect", pointOpt, 1, dummy, ICON_PATTERN_RECT, inSketch, false, "Прямоугольный массив");
    ImGui::SameLine();
    RibbonButton("pat_circ", pointOpt, 1, dummy, ICON_PATTERN_CIRC, inSketch, false, "Круговой массив");
    RibbonButton("pat_mirr", pointOpt, 1, dummy, ICON_MIRROR,       inSketch, false, "Зеркало");
    RibbonGroupEnd(Tr("grp.pattern").c_str());

    // ============================================================
    // ИЗМЕНИТЬ — 5 кнопок-меню
    // ============================================================
    RibbonGroupStart();
    RibbonButton("move",   moveOpt,   3, myMoveToolIndex,   ICON_MOVE,   inSketch, false, "Перенести");
    ImGui::SameLine();
    RibbonButton("trim",   trimOpt,   2, myTrimToolIndex,   ICON_TRIM,   inSketch, false, "Обрезать");
    ImGui::SameLine();
    RibbonButton("offset", offsetOpt, 1, myOffsetToolIndex, ICON_OFFSET, inSketch, false, "Сместить");
    RibbonButton("scale",  scaleOpt,  2, dummy,             ICON_SCALE,  inSketch, false, "Масштаб");
    ImGui::SameLine();
    RibbonButton("split",  splitOpt,  1, dummy,             ICON_SPLIT,  inSketch, false, "Разделить");
    ImGui::SameLine();
    RibbonButton("copy2d", splitOpt,  1, dummy,             ICON_COPY,   inSketch, false, "Копировать");
    RibbonGroupEnd(Tr("grp.modify").c_str());

    // ============================================================
    // ОГРАНИЧЕНИЯ — 12 малых, 2 ряда по 6
    // ============================================================
    RibbonGroupStart();
    // Уплотняем вертикальный отступ между двумя рядами
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 0.0f));
    // Ряд 1
    RibbonButton("con_coincident",  pointOpt, 1, dummy, ICON_COINCIDENT,    inSketch, false, "Совпадение");
    ImGui::SameLine();
    RibbonButton("con_parallel",    pointOpt, 1, dummy, ICON_PARALLEL,      inSketch, false, "Параллельность");
    ImGui::SameLine();
    RibbonButton("con_perp",        pointOpt, 1, dummy, ICON_PERPENDICULAR, inSketch, false, "Перпендикулярность");
    ImGui::SameLine();
    RibbonButton("con_horiz",       pointOpt, 1, dummy, ICON_HORIZONTAL,    inSketch, false, "Горизонталь");
    ImGui::SameLine();
    RibbonButton("con_vert",        pointOpt, 1, dummy, ICON_VERTICAL,      inSketch, false, "Вертикаль");
    ImGui::SameLine();
    RibbonButton("con_tangent",     pointOpt, 1, dummy, ICON_TANGENT,       inSketch, false, "Касание");
    // Ряд 2 (начинается с новой строки автоматически)
    RibbonButton("con_concentric",  pointOpt, 1, dummy, ICON_CONCENTRIC,    inSketch, false, "Концентричность");
    ImGui::SameLine();
    RibbonButton("con_equal",       pointOpt, 1, dummy, ICON_EQUAL,         inSketch, false, "Равенство");
    ImGui::SameLine();
    RibbonButton("con_collinear",   pointOpt, 1, dummy, ICON_COLLINEAR,     inSketch, false, "Коллинеарность");
    ImGui::SameLine();
    RibbonButton("con_fix",         pointOpt, 1, dummy, ICON_FIX,           inSketch, false, "Фиксация");
    ImGui::SameLine();
    RibbonButton("con_symmetric",   pointOpt, 1, dummy, ICON_SYMMETRIC,     inSketch, false, "Симметрия");
    ImGui::SameLine();
    RibbonButton("con_smooth",      pointOpt, 1, dummy, ICON_SMOOTH_G2,     inSketch, false, "Сглаживание G2");
    ImGui::PopStyleVar();
    RibbonGroupEnd(Tr("grp.constrain").c_str());

    // ============================================================
    // РАЗМЕРЫ — 5 малых
    // ============================================================
    RibbonGroupStart();
    RibbonButton("dim_linear",   pointOpt, 1, dummy, ICON_DIM_LINEAR,   inSketch, false, "Линейный");
    ImGui::SameLine();
    RibbonButton("dim_angle",    pointOpt, 1, dummy, ICON_DIM_ANGLE,    inSketch, false, "Угловой");
    ImGui::SameLine();
    RibbonButton("dim_radius",   pointOpt, 1, dummy, ICON_DIM_RADIUS,   inSketch, false, "Радиус");
    RibbonButton("dim_diameter", pointOpt, 1, dummy, ICON_DIM_DIAMETER, inSketch, false, "Диаметр");
    ImGui::SameLine();
    RibbonButton("dim_ordinate", pointOpt, 1, dummy, ICON_DIM_ORDINATE, inSketch, false, "Ординатный");
    RibbonGroupEnd(Tr("grp.dimensions").c_str());

    // ============================================================
    // ВСТАВКА — 3 малые
    // ============================================================
    RibbonGroupStart();
    RibbonButton("insert_img",    pointOpt, 1, dummy, ICON_INSERT_IMG, inSketch, false, "Изображение");
    ImGui::SameLine();
    RibbonButton("insert_points", pointOpt, 1, dummy, ICON_POINT_3D,   inSketch, false, "Точки");
    RibbonButton("insert_acad",   pointOpt, 1, dummy, ICON_FILE,       inSketch, false, "ACAD");
    RibbonGroupEnd(Tr("grp.insert").c_str());


    // ============================================================
    // ФОРМАТ — 3 тонкие кнопки в столбик
    // ============================================================
    RibbonGroupStart();
    {
        struct LSItem { const char* label; const char* tip; LineStyle style; ImU32 bg; };
        LSItem items[] = {
            { "Основная",   "Основная линия",        LS_Normal,       IM_COL32(140, 51, 191, 255) },
            { "Вспомог.",   "Вспомогательная линия", LS_Construction, IM_COL32(115, 128, 166, 255) },
            { "Осевая",     "Осевая линия",          LS_Centerline,   IM_COL32(179, 102, 38, 255) },
        };
        for (int i = 0; i < 3; ++i) {
            if (i > 0) ImGui::SameLine();
            bool active = (myCurrentLineStyle == items[i].style);
            ImVec4 bg = active ? ImColor(items[i].bg).Value
                               : ImVec4(0.22f, 0.24f, 0.30f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, bg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  ImVec4(bg.x*1.2f, bg.y*1.2f, bg.z*1.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  ImVec4(bg.x*0.8f, bg.y*0.8f, bg.z*0.8f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            char buf[64];
            snprintf(buf, sizeof(buf), "%s##ls_%d", items[i].label, i);
            if (ImGui::Button(buf, ImVec2(85, 62))) myCurrentLineStyle = items[i].style;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", items[i].tip);
            ImGui::PopStyleColor(4);
        }
    }
    RibbonGroupEnd(Tr("grp.format").c_str());


    // ============================================================
    // ВЫХОД — 2 большие в ряд
    // ============================================================
    RibbonGroupStart();
    if (RibbonButton("finish", finishOpt, 1, dummy, ICON_FINISH, inSketch, true, "Завершить эскиз"))
        ExitSketch();
    ImGui::SameLine();
    if (RibbonButton("show_format", showFormatOpt, 1, dummy, "layout-grid", inSketch, true, "Показать формат")) {
        // TODO: чекбоксы линий / размеров / ограничений
    }
    RibbonGroupEnd(Tr("grp.exit").c_str());
}

void Application::DrawRibbonModelTab() {
    static int dummy = 0;
    static const char* oneOpt[] = { "tool.dummy" };

    // === Группа: Эскиз ===
    RibbonGroupStart();
    RibbonSingleButton("newsketch", "tool.newSketch", ICON_SKETCH, true, true);
    RibbonGroupEnd(Tr("grp.sketch").c_str());

    // === Группа: Создать ===
    RibbonGroupStart();
    RibbonSingleButton("extrude", "tool.extrude", ICON_EXTRUDE, true, true);
    ImGui::SameLine();
    RibbonSingleButton("revolve", "tool.revolve", ICON_REVOLVE, true, true);
    ImGui::SameLine();
    RibbonSingleButton("sweep",   "tool.sweep",   ICON_SWEEP,   true, true);
    ImGui::SameLine();
    RibbonSingleButton("loft",    "tool.loft",    ICON_LOFT,    true, true);
    ImGui::SameLine();
    RibbonSingleButton("spring",  "tool.spring",  "cad/spring", true, true);
    RibbonGroupEnd(Tr("grp.create").c_str());

    // === Группа: Изменить ===
    RibbonGroupStart();
    RibbonSingleButton("hole",     "tool.hole",    ICON_HOLE,    true, true);
    ImGui::SameLine();
    RibbonSingleButton("fillet",   "tool.fillet",  ICON_FILLET,  true, true);
    ImGui::SameLine();
    RibbonSingleButton("thread",   "tool.thread",  ICON_THREAD,  true, true);
    ImGui::SameLine();
    // 2x2 малых СПРАВА от больших
    ImGui::BeginGroup();
        RibbonSingleButton("chamfer",  "tool.chamfer", ICON_CHAMFER, true, false);
        ImGui::SameLine();
        RibbonSingleButton("shell",    "tool.shell",   ICON_SHELL,   true, false);
        RibbonSingleButton("split",    "tool.split",   "scissors",   true, false);
        ImGui::SameLine();
        RibbonSingleButton("rib",      "tool.rib",     "plus",       true, false);
    ImGui::EndGroup();
    RibbonGroupEnd(Tr("grp.modify").c_str());

    // === Группа: Массив ===
    RibbonGroupStart();
    RibbonSingleButton("pattern_rect", "tool.pattern_rect", ICON_PATTERN_RECT, true, false);
    ImGui::SameLine();
    RibbonSingleButton("pattern_circ", "tool.pattern_circ", ICON_PATTERN_CIRC, true, false);
    RibbonSingleButton("mirror3d",     "tool.mirror",       ICON_MIRROR,       true, false);
    RibbonGroupEnd(Tr("grp.pattern").c_str());

    // === Группа: Рабочие элементы ===
    RibbonGroupStart();
    RibbonSingleButton("wplane", "tool.plane",    ICON_PLANE,    true, false);
    ImGui::SameLine();
    RibbonSingleButton("waxis",  "tool.axis",     ICON_AXIS,     true, false);
    RibbonSingleButton("wpoint", "tool.point",    ICON_POINT_3D, true, false);
    RibbonGroupEnd(Tr("grp.work").c_str());

    // === Группа: Стандартные изделия ===
    RibbonGroupStart();
    RibbonSingleButton("library", "tool.library", "package", true, true);
    RibbonGroupEnd(Tr("grp.standards").c_str());

    // === Группа: Диагностика ===
    RibbonGroupStart();
    RibbonSingleButton("info",       "tool.info",       ICON_INFO,    true, false);
    ImGui::SameLine();
    RibbonSingleButton("measure",    "tool.measure",    ICON_MEASURE, true, false);
    RibbonSingleButton("mass",       "tool.mass",       ICON_MASS,    true, false);
    ImGui::SameLine();
    RibbonSingleButton("import_stl", "tool.import_stl", ICON_IMPORT,  true, false);
    RibbonGroupEnd(Tr("grp.diagnostics").c_str());
}

void Application::DrawRibbonAnnotateTab() {
    static int dummy = 0;
    static const char* oneOpt[] = { "tool.dummy" };

    // --- Размеры ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.dimensions").c_str());
    RibbonSingleButton("dim_linear",   "tool.dim_linear",   ICON_DIM_LINEAR,   true, true);
    ImGui::SameLine();
    RibbonSingleButton("dim_angle",    "tool.dim_angle",    ICON_DIM_ANGLE,    true, true);
    ImGui::SameLine();
    RibbonSingleButton("dim_radius",   "tool.dim_radius",   ICON_DIM_RADIUS,   true, true);
    ImGui::SameLine();
    RibbonSingleButton("dim_diameter", "tool.dim_diameter", ICON_DIM_DIAMETER, true, true);
    ImGui::SameLine();
    RibbonSingleButton("dim_ordinate", "tool.dim_ordinate", ICON_DIM_ORDINATE, true, true);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Текст и символы ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.text").c_str());
    RibbonSingleButton("leader",    "tool.leader",     ICON_LEADER,     true, false);
    ImGui::SameLine();
    RibbonSingleButton("text_ann",  "tool.text",       ICON_TEXT,       true, false);
    ImGui::SameLine();
    RibbonSingleButton("datum",     "tool.datum",      ICON_DATUM,      true, false);
    ImGui::SameLine();
    RibbonSingleButton("tolerance", "tool.tolerance",  ICON_TOLERANCE,  true, false);
    ImGui::SameLine();
    RibbonSingleButton("roughness", "tool.roughness",  ICON_ROUGHNESS,  true, false);
    ImGui::SameLine();
    RibbonSingleButton("weld",      "tool.weld",       ICON_WELD,       true, false);
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
    RibbonSingleButton("measure",  "tool.measure",  ICON_MEASURE,  true, false);
    ImGui::SameLine();
    RibbonSingleButton("distance", "tool.distance", ICON_DISTANCE, true, false);
    ImGui::SameLine();
    RibbonSingleButton("angle",    "tool.angle",    ICON_ANGLE,    true, false);
    ImGui::SameLine();
    RibbonSingleButton("area",     "tool.area",     ICON_AREA,     true, false);
    ImGui::SameLine();
    RibbonSingleButton("volume",   "tool.volume",   ICON_VOLUME,   true, false);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Анализ ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.analysis").c_str());
    RibbonSingleButton("section",       "tool.section",       ICON_SECTION,       true, false);
    ImGui::SameLine();
    RibbonSingleButton("draft_analysis","tool.draft_analysis",ICON_DRAFT_ANALYSIS,true, false);
    ImGui::SameLine();
    RibbonSingleButton("thickness",     "tool.thickness",     ICON_THICKNESS,     true, false);
    ImGui::SameLine();
    RibbonSingleButton("curvature",     "tool.curvature",     ICON_CURVATURE,     true, false);
    ImGui::SameLine();
    RibbonSingleButton("zebra",         "tool.zebra",         ICON_ZEBRA,         true, false);
    ImGui::SameLine();
    RibbonSingleButton("interference",  "tool.interference",  ICON_INTERFERENCE,  true, false);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Свойства ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.properties").c_str());
    RibbonSingleButton("model_props", "tool.model_props", ICON_MODEL_PROPS, true, false);
    ImGui::SameLine();
    RibbonSingleButton("mass",        "tool.mass",        ICON_MASS,        true, false);
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
    RibbonSingleButton("parameters", "tool.parameters", ICON_PARAMETERS, true, true);
    ImGui::SameLine();
    RibbonSingleButton("functions",  "tool.functions",  ICON_FUNCTION,   true, true);
    ImGui::SameLine();
    RibbonSingleButton("texts",      "tool.texts",      ICON_TEXT,       true, true);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Материалы (будущее) ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.materials").c_str());
    RibbonSingleButton("material",   "tool.material",   ICON_MATERIAL,   true, false);
    ImGui::SameLine();
    RibbonSingleButton("texture",    "tool.texture",    ICON_TEXTURE,    true, false);
    ImGui::SameLine();
    RibbonSingleButton("appearance", "tool.appearance", ICON_APPEARANCE, true, false);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Обновление ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.update").c_str());
    RibbonSingleButton("update",    "tool.update",    ICON_UPDATE,    true, false);
    ImGui::SameLine();
    RibbonSingleButton("recompute", "tool.recompute", ICON_RECOMPUTE, true, false);
    ImGui::SameLine();
    RibbonSingleButton("report",    "tool.report",    ICON_REPORT,    true, false);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Вставка ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.insert").c_str());
    RibbonSingleButton("insert_obj", "tool.insert_obj", ICON_INSERT_OBJ, true, false);
    ImGui::SameLine();
    RibbonSingleButton("insert_img", "tool.insert_img", ICON_INSERT_IMG, true, false);
    ImGui::SameLine();
    RibbonSingleButton("import",     "tool.import",     ICON_IMPORT,     true, false);
    ImGui::EndGroup();
}

// ============================================================
//  Вкладка «Вид»
// ============================================================
void Application::DrawRibbonAssemblyTab() {
    // === Компонент ===
    RibbonGroupStart();
    RibbonSingleButton("asm_place",   "tool.asm.place",   "package-plus", true, true, "Вставить компонент");
    ImGui::SameLine();
    RibbonSingleButton("asm_create",  "tool.asm.create",  "package",      true, true, "Создать компонент");
    ImGui::SameLine();
    RibbonSingleButton("asm_replace", "tool.asm.replace", "refresh-cw",   true, true, "Заменить компонент");
    ImGui::SameLine();
    RibbonSingleButton("asm_copy",    "tool.asm.copy",    ICON_COPY,      true, true, "Копировать компонент");
    RibbonGroupEnd(Tr("grp.component").c_str());

    // === Позиционирование ===
    RibbonGroupStart();
    RibbonSingleButton("asm_joint",      "tool.asm.joint",      "link",    true, true, "Соединение (подвижное)");
    ImGui::SameLine();
    RibbonSingleButton("asm_constraint", "tool.asm.constraint", "pin",     true, true, "Зависимость (статичная)");
    ImGui::SameLine();
    RibbonSingleButton("asm_dof",        "tool.asm.dof",        "move-3d", true, true, "Степени свободы");
    RibbonGroupEnd(Tr("grp.position").c_str());

    // === Массив ===
    RibbonGroupStart();
    RibbonSingleButton("asm_pat_rect", "tool.pattern_rect", ICON_PATTERN_RECT, true, false, "Прямоуг.");
    ImGui::SameLine();
    RibbonSingleButton("asm_pat_circ", "tool.pattern_circ", ICON_PATTERN_CIRC, true, false, "Круговой");
    RibbonSingleButton("asm_mirror",   "tool.mirror",       ICON_MIRROR,       true, false, "Зеркало");
    RibbonGroupEnd(Tr("grp.pattern").c_str());

    // === Спецификация ===
    RibbonGroupStart();
    RibbonSingleButton("asm_bom",  "tool.bom",           "list",      true, true, "Bill of Materials");
    ImGui::SameLine();
    RibbonSingleButton("asm_spec", "tool.specification", ICON_FILE,   true, true, "Спецификация");
    RibbonGroupEnd(Tr("grp.specification").c_str());

    // === Стандартные изделия ===
    RibbonGroupStart();
    RibbonSingleButton("asm_library", "tool.library", "package", true, true, "Библиотека");
    RibbonGroupEnd(Tr("grp.standards").c_str());
}

void Application::DrawRibbonViewTab() {
    static int dummy = 0;

    // === Навигация (3 кнопки-меню) ===
    RibbonGroupStart();
    {
        static const char* panOpts[]  = { "tool.pan", "tool.pan", "tool.pan" };
        RibbonButton("nav_pan",   panOpts,  1, dummy, ICON_PAN,   true, true, "Панорамирование");
    }
    ImGui::SameLine();
    {
        static const char* zoomOpts[] = { "tool.zoom_all", "tool.zoom_frame", "tool.zoom_sel", "tool.zoom" };
        int zoomIdx = 0;
        RibbonButton("nav_zoom",  zoomOpts, 4, zoomIdx, ICON_ZOOM,  true, true, "Зум");
    }
    ImGui::SameLine();
    {
        static const char* orbOpts[] = { "tool.orbit", "tool.orbit_dep", "tool.orbit_angle" };
        int orbIdx = 0;
        RibbonButton("nav_orbit", orbOpts, 3, orbIdx, ICON_ORBIT, true, true, "Орбита");
    }
    RibbonGroupEnd(Tr("grp.navigation").c_str());


    // === Стиль отображения (7, кнопка-меню) ===
    RibbonGroupStart();
    {
        static const char* styleOpts[] = {
            "tool.style_realistic", "tool.style_shaded", "tool.style_shaded_edges",
            "tool.style_shaded_hidden", "tool.style_wireframe",
            "tool.style_mono", "tool.style_tech"
        };
        RibbonButton("view_style", styleOpts, 7, myDisplayStyleIndex, ICON_SHADOWS, true, true,
                     "Стиль отображения");
    }
    ImGui::SameLine();
    {
        static const char* bgOpts[] = {
            "tool.bg_white", "tool.bg_gray", "tool.bg_grad_light",
            "tool.bg_grad_dark", "tool.bg_custom"
        };
        RibbonButton("view_bg", bgOpts, 5, myBackgroundIndex, ICON_BACKGROUND, true, true,
                     "Фон вьюпорта");
    }
    RibbonGroupEnd(Tr("grp.style").c_str());

    // === Представление модели (6 малых) ===
    RibbonGroupStart();
    RibbonSingleButton("mdl_shadows",     "tool.shadows",       ICON_SHADOWS,     true, false, "Тени");
    ImGui::SameLine();
    RibbonSingleButton("mdl_reflections", "tool.reflections",   ICON_REFLECTIONS, true, false, "Отражения");
    ImGui::SameLine();
    RibbonSingleButton("mdl_textures",    "tool.textures",      ICON_GRID,        true, false, "Текстуры");
    RibbonSingleButton("mdl_zero_plane",  "tool.zero_plane",    "plus",           true, false, "Нулевая");
    ImGui::SameLine();
    RibbonSingleButton("mdl_refine",      "tool.refine",        ICON_SHOW_ALL,    true, false, "Уточнить");
    RibbonGroupEnd(Tr("grp.model_view").c_str());

    // === Видимость (4) ===
    RibbonGroupStart();
    RibbonSingleButton("hide",        "tool.hide",        ICON_HIDE,        true, false, "Скрыть");
    ImGui::SameLine();
    RibbonSingleButton("show_all",    "tool.show_all",    ICON_SHOW_ALL,    true, false, "Показать всё");
    RibbonSingleButton("isolate",     "tool.isolate",     ICON_ISOLATE,     true, false, "Изолировать");
    ImGui::SameLine();
    RibbonSingleButton("show_hidden", "tool.show_hidden", ICON_SHOW_HIDDEN, true, false, "Показать скрытое");
    RibbonGroupEnd(Tr("grp.visibility").c_str());

    // === Окна (5 малых) ===
    RibbonGroupStart();
    RibbonSingleButton("win_ui",    "tool.ui",           "layout-grid", true, false, "Интерфейс");
    ImGui::SameLine();
    RibbonSingleButton("win_clear", "tool.clear_screen", ICON_BACKGROUND,  true, false, "Очистить экран");
    ImGui::SameLine();
    RibbonSingleButton("win_switch","tool.new_window",   ICON_NEW_WINDOW,  true, false, "Переключение");
    RibbonSingleButton("win_new",   "tool.new_window",   "plus",           true, false, "Создать");
    ImGui::SameLine();
    RibbonSingleButton("win_tile",  "tool.tile",         ICON_TILE,        true, false, "Упорядочить");
    RibbonGroupEnd(Tr("grp.windows").c_str());
}

void Application::DrawRibbon() {
    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, 206), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 2));
    ImGui::Begin("##ribbon", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

    {
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
        qat.onGoHome         = [this]() { CaptureCurrentThumbnail(); myShowHomeScreen = true; };
        qat.onOpenSettings   = [this]() { myShowSettingsDialog = true; };
        qat.onNotImplemented = [this](const char* feat) {
            myNotImplementedFeature = feat;
            myShowNotImplementedDialog = true;
        };
        qat.onMinimize       = [this]() { glfwIconifyWindow(myWindow->GetNativeWindow()); };
        qat.onMaximize       = [this]() { myWindow->Maximize(); };
        qat.onClose          = [this]() { glfwSetWindowShouldClose(myWindow->GetNativeWindow(), GLFW_TRUE); };
        UI::Qat::DrawQuickAccessToolbar(qat);
    }
    ImGui::Separator();
    DrawRibbonTabs();
    ImGui::Separator();

    // Child с отступом сверху 6 px
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
    if (ImGui::BeginChild("##ribbon_content", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        // Колесо мыши прокручивает ribbon горизонтально
        ImGuiIO& ioChild = ImGui::GetIO();
        if (ioChild.MouseWheel != 0.0f && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
            float scrollX = ImGui::GetScrollX() - ioChild.MouseWheel * 60.0f;
            if (scrollX < 0.0f) scrollX = 0.0f;
            ImGui::SetScrollX(scrollX);
        }
        switch (myActiveRibbonTab) {
            case 0: DrawRibbonModelTab();      break;
            case 1: DrawRibbonSketchTab();     break;
            case 2: DrawRibbonAssemblyTab();   break;
            case 3: DrawRibbonViewTab();       break;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::End();
    ImGui::PopStyleVar();
}

void Application::DrawMenuBar() {
    // Оставлен для совместимости — но UI теперь полностью в DrawRibbon()
    // Вызывается из Run()
}

void Application::DrawDocumentTree() {
    ImGuiIO& io = ImGui::GetIO();
    const float panelWidth = myTreeWidth;

    ImGui::SetNextWindowPos(ImVec2(0, 211), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelWidth, io.DisplaySize.y - 211 - 76), ImGuiCond_Always);

    ImGui::Begin("##tree_panel", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoTitleBar);

    ImGui::Text("%s", Tr("panel.title").c_str());
    ImGui::Separator();

    if (ImGui::Button(Tr("panel.addBox").c_str())) {
        myShowAddBoxDialog = true;
        std::strncpy(myNewBoxName, Tr("default.boxName").c_str(), sizeof(myNewBoxName) - 1);
        myNewBoxName[sizeof(myNewBoxName) - 1] = '\0';
        myNewBoxPos[0] = myNewBoxPos[1] = myNewBoxPos[2] = 0.0f;
        myNewBoxSize[0] = myNewBoxSize[1] = myNewBoxSize[2] = 100.0f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) OnSave();
    ImGui::SameLine();
    if (ImGui::Button("Open")) OnOpen();
    ImGui::Separator();

    if (!myDocument || myDocument->GetDoc().IsNull()) {
        ImGui::Text("%s", Tr("panel.document.null").c_str());
        ImGui::End();
        return;
    }

    bool hasOps = myDocument->Has3DOperations();

    TDF_Label selectedLabel;
    if (myOcctView) selectedLabel = myOcctView->GetSelectedLabel();

    TDF_Label root = myDocument->MainLabel();

    if (ImGui::TreeNodeEx(Tr("panel.document").c_str(),
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {

        // === Папка Start ===
        TDF_Label baseLabel = myDocument->GetBaseGeometryLabel();
        if (!baseLabel.IsNull()) {
            // Start открыт, если:
            //  - нет 3D-операций, ИЛИ
            //  - пользователь развернул его вручную
            bool startOpen = !hasOps || myStartExpandedManually;

            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::SetNextItemOpen(startOpen, ImGuiCond_Always);
            bool opened = ImGui::TreeNodeEx("Start",
                ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth);
            ImGui::PopStyleColor();

            // Пользователь кликнул — запоминаем его выбор
            if (ImGui::IsItemToggledOpen()) {
                myStartExpandedManually = opened;
            }

            if (opened) {
                // Внутри Start: только плоскости.
                // Origin Point — скрыт. Оси — скрыты, если есть 3D-операции.
                TDF_ChildIterator it(baseLabel, false);
                for (; it.More(); it.Next()) {
                    TDF_Label child = it.Value();
                    int type = BreezeDocument::GetType(child);

                    // Origin Point — никогда не показываем
                    if (type == FT_OriginPoint) continue;

                    // Оси не показываем вообще
                    // FT_Axis теперь тоже показывается

                    // Плоскости и (при отсутствии операций) оси — показываем
                    DrawLabelTree(child, selectedLabel, hasOps);
                }
                ImGui::TreePop();
            }
        }

        // === Пользовательские фичи (прямые дети Main, кроме Start) ===
        TDF_ChildIterator it(root, false);
        for (; it.More(); it.Next()) {
            TDF_Label child = it.Value();
            if (child.IsEqual(baseLabel)) continue;
            DrawLabelTree(child, selectedLabel, hasOps);
        }

        ImGui::TreePop();
    }

    ImGui::End();

    // Обработка отложенных операций
    if (!myPendingCreateSketchLabel.IsNull() && myDocument) {
        // Создаём эскиз и сразу запоминаем, куда войти
        TDF_Label parentPlane = myPendingCreateSketchLabel;
        TDF_Label newSketch = myDocument->AddSketchFeature(parentPlane);
        myPendingCreateSketchLabel = TDF_Label();

        // Автоматически входим в созданный эскиз
        if (!newSketch.IsNull()) {
            myPendingEnterSketchLabel = newSketch;
            std::cout << "Auto-enter sketch: tag=" << newSketch.Tag() << "\n";
        }
    }
    if (!myPendingEnterSketchLabel.IsNull() && myDocument) {
        EnterSketch(myPendingEnterSketchLabel);
        myPendingEnterSketchLabel = TDF_Label();
    }
    // myPendingDeleteLabel теперь обрабатывается через диалог подтверждения
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
    if (myDependencyDialogOpen) {
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

        if (!inList(myDependencyBase) && !inList(myDependencyDependent)) {
            // Первый клик — базовый, остальные — зависимые
            if (myDependencyBase.empty()) {
                myDependencyBase.push_back(sel);
                std::cout << "Dependency: base = " << GetLabelName(sel) << "\n";
            } else {
                myDependencyDependent.push_back(sel);
                std::cout << "Dependency: dependent = " << GetLabelName(sel) << "\n";
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
    myActiveRibbonTab = 1;  // Sketch tab

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
    myActiveRibbonTab = 0;  // 3D Model tab

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

    if (mySkipDeleteConfirm) {
        // Удаляем сразу
        if (myDocument) myDocument->RemoveFeature(label);
        RefreshView();
        if (myOcctView) myOcctView->FitAll();
        return;
    }

    myPendingDeleteLabel = label;

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
        std::snprintf(myDeleteConfirmInfo, sizeof(myDeleteConfirmInfo),
                      "Удалить «%s»?\n\nВместе с ним будут удалены %d вложенных элементов.",
                      name.c_str(), childCount);
    } else {
        std::snprintf(myDeleteConfirmInfo, sizeof(myDeleteConfirmInfo),
                      "Удалить «%s»?", name.c_str());
    }

    myShowDeleteConfirm = true;
}

void Application::DrawRenameDialog() {
    if (!myShowRenameDialog) return;

    ImGui::OpenPopup("Переименовать");

    if (ImGui::BeginPopupModal("Переименовать", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Новое имя:");
        ImGui::SetNextItemWidth(280);
        ImGui::InputText("##rename_buf", myRenameBuffer, sizeof(myRenameBuffer),
                         ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::Separator();

        if (ImGui::Button("OK", ImVec2(120, 0))) {
            if (myDocument && !myPendingRenameLabel.IsNull() && myRenameBuffer[0] != '\0') {
                myDocument->RenameFeature(myPendingRenameLabel, myRenameBuffer);
                myPendingRenameLabel = TDF_Label();
            }
            myShowRenameDialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Отмена", ImVec2(120, 0))) {
            myPendingRenameLabel = TDF_Label();
            myShowRenameDialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Application::DrawDeleteConfirmDialog() {
    if (!myShowDeleteConfirm) return;

    ImGui::OpenPopup("Подтверждение удаления");

    if (ImGui::BeginPopupModal("Подтверждение удаления", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", myDeleteConfirmInfo);
        ImGui::Separator();
        ImGui::Checkbox("Не спрашивать снова (до перезапуска)", &mySkipDeleteConfirm);
        ImGui::Separator();

        if (ImGui::Button("Удалить", ImVec2(120, 0))) {
            if (myDocument && !myPendingDeleteLabel.IsNull()) {
                myDocument->RemoveFeature(myPendingDeleteLabel);
                myPendingDeleteLabel = TDF_Label();
                RefreshView();
                if (myOcctView) myOcctView->FitAll();
            }
            myShowDeleteConfirm = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Отмена", ImVec2(120, 0))) {
            myPendingDeleteLabel = TDF_Label();
            myShowDeleteConfirm = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Application::DrawUnsavedChangesDialog() {
    if (!myShowUnsavedDialog) return;

    ImGui::OpenPopup("Несохранённые изменения");

    if (ImGui::BeginPopupModal("Несохранённые изменения", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", myUnsavedDialogInfo);
        ImGui::Separator();

        if (ImGui::Button("Сохранить", ImVec2(120, 0))) {
            int idx = myPendingCloseDocIndex;
            bool wasWindow = myPendingWindowClose;

            if (idx >= 0 && idx < (int)myDocuments.size()) {
                BreezeDocument* prevDoc = myDocument;
                int prevIdx = myActiveDocIndex;
                myDocument = myDocuments[idx].get();
                myActiveDocIndex = idx;
                OnSave();
                bool saved = !myDocument->IsModified();
                if (!saved) {
                    myDocument = prevDoc;
                    myActiveDocIndex = prevIdx;
                    return;
                }
            }

            myPendingCloseDocIndex = -1;
            myPendingWindowClose = false;
            myShowUnsavedDialog = false;
            ImGui::CloseCurrentPopup();

            if (wasWindow) {
                glfwSetWindowShouldClose(myWindow->GetNativeWindow(), GLFW_TRUE);
            } else if (idx >= 0) {
                CloseDoc(idx);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Не сохранять", ImVec2(120, 0))) {
            int idx = myPendingCloseDocIndex;
            bool wasWindow = myPendingWindowClose;
            myPendingCloseDocIndex = -1;
            myPendingWindowClose = false;
            myShowUnsavedDialog = false;
            ImGui::CloseCurrentPopup();

            if (idx >= 0 && idx < (int)myDocuments.size()) {
                myDocuments[idx]->SetModified(false);
            }
            if (wasWindow) {
                glfwSetWindowShouldClose(myWindow->GetNativeWindow(), GLFW_TRUE);
            } else if (idx >= 0 && idx < (int)myDocuments.size()) {
                CloseDoc(idx);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Отмена", ImVec2(120, 0))) {
            myPendingCloseDocIndex = -1;
            myPendingWindowClose = false;
            myShowUnsavedDialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Application::DrawPropertiesDialog() {
    if (!myShowPropertiesDialog) return;

    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Свойства", &myShowPropertiesDialog)) {
        if (myPendingPropertiesLabel.IsNull() || !myDocument) {
            ImGui::TextDisabled("Объект не выбран");
        } else {
            std::string name = "<unnamed>";
            Handle(TDataStd_Name) nameAttr;
            if (myPendingPropertiesLabel.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
                name = ExtToUtf8(nameAttr->Get());
            }

            int type = BreezeDocument::GetType(myPendingPropertiesLabel);

            ImGui::Text("Имя: %s", name.c_str());
            ImGui::Text("Тип: %d", type);
            ImGui::Text("Тег: %d", myPendingPropertiesLabel.Tag());

            const char* typeName = "Неизвестно";
            switch (type) {
                case FT_Start:       typeName = "Папка Start"; break;
                case FT_Plane:       typeName = "Плоскость"; break;
                case FT_Axis:        typeName = "Ось"; break;
                case FT_OriginPoint: typeName = "Точка Origin"; break;
                case FT_Box:         typeName = "Куб"; break;
                case FT_Cylinder:    typeName = "Цилиндр"; break;
                case FT_Hole:        typeName = "Отверстие"; break;
                case FT_Sketch:      typeName = "Эскиз"; break;
                case FT_SketchLine:  typeName = "Линия эскиза"; break;
            }
            ImGui::Text("Описание: %s", typeName);

            ImGui::Separator();
            ImGui::TextDisabled("(расширенные свойства — позже)");
        }
    }
    ImGui::End();
}

bool Application::IsLabelVisibleByDefault(const TDF_Label& label, bool hasOps) const {
    if (label.IsNull()) return false;

    int type = BreezeDocument::GetType(label);

    // Скрываемые системные элементы
    bool isSystemHideable = (type == FT_Axis || type == FT_Plane);
    if (isSystemHideable && hasOps) return false;

    return true;
}

void Application::ShowAllRespectingDefaults() {
    if (!myOcctView || !myDocument) return;

    // Сбрасываем карту видимости полностью — она была навязана изоляцией
    myVisibility.clear();
    myIsolationBackup.clear();
    myIsolationActive = false;

    bool hasOps = myDocument->Has3DOperations();

    // Проходим по всем объектам и применяем правила видимости
    auto allLabels = myOcctView->GetAllLabels();
    for (const auto& lbl : allLabels) {
        bool visible = IsLabelVisibleByDefault(lbl, hasOps);
        myOcctView->SetShapeVisible(lbl, visible);
    }

    std::cout << "ShowAllRespectingDefaults: restored\n";
}

void Application::DrawContextMenuItems(const TDF_Label& label, bool hasOps) {
    if (label.IsNull()) return;

    std::string labelName = "<unnamed>";
    Handle(TDataStd_Name) nameAttr;
    if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
        labelName = ExtToUtf8(nameAttr->Get());
    }

    int type = BreezeDocument::GetType(label);
    bool isSystem = (type < 0);
    std::string key = LabelKey(label);

    // === Сброс изоляции (если активна) ===
    if (myIsolationActive) {
        if (ImGui::MenuItem("Показать всё (снять изоляцию)")) {
            ShowAllRespectingDefaults();
        }
        ImGui::Separator();
    }

    // === Плоскость ===
    if (type == FT_Plane) {
        if (ImGui::MenuItem("Создать эскиз")) {
            myPendingCreateSketchLabel = label;
        }
        ImGui::Separator();
    }

    // === Эскиз ===
    if (type == FT_Sketch) {
        if (ImGui::MenuItem("Редактировать эскиз")) {
            myPendingEnterSketchLabel = label;
        }
        ImGui::Separator();
    }

    // === 3D-операция ===
    if (type == FT_Box || type == FT_Cylinder || type == FT_Hole) {
        if (ImGui::MenuItem("Редактировать")) {}
        if (ImGui::MenuItem("Измерить")) {}
        if (ImGui::MenuItem("Изолировать")) {
            if (myOcctView && myDocument) {
                // 1. Копируем список меток ДО любых изменений
                std::vector<TDF_Label> allLabels = myOcctView->GetAllLabels();

                // 2. Сохраняем текущую видимость для последующего "Показать всё"
                myIsolationBackup = myVisibility;
                myIsolationActive = true;

                // 3. Скрываем всё, кроме выделенного
                for (const auto& entry : allLabels) {
                    if (entry.IsEqual(label)) continue;
                    if (BreezeDocument::IsSystem(entry)) continue;  // системные не трогаем

                    std::string k = LabelKey(entry);
                    myVisibility[k] = false;
                    myOcctView->SetShapeVisible(entry, false);
                }
            }
        }
        ImGui::Separator();
    }

    // === Start ===
    if (type == FT_Start) {
        if (ImGui::MenuItem("Показать все плоскости и оси")) {
            TDF_ChildIterator it(label, false);
            for (; it.More(); it.Next()) {
                TDF_Label child = it.Value();
                int ct = BreezeDocument::GetType(child);
                if (ct == FT_Plane || ct == FT_Axis) {
                    std::string k = LabelKey(child);
                    myVisibility[k] = true;
                    if (myOcctView) myOcctView->SetShapeVisible(child, true);
                }
            }
        }
        if (ImGui::MenuItem("Скрыть все плоскости и оси")) {
            TDF_ChildIterator it(label, false);
            for (; it.More(); it.Next()) {
                TDF_Label child = it.Value();
                int ct = BreezeDocument::GetType(child);
                if (ct == FT_Plane || ct == FT_Axis) {
                    std::string k = LabelKey(child);
                    myVisibility[k] = false;
                    if (myOcctView) myOcctView->SetShapeVisible(child, false);
                }
            }
        }
        ImGui::Separator();
    }

    // === Скрыть / Показать ===
    if (isSystem && (type == FT_Plane || type == FT_Axis)) {
        auto it = myVisibility.find(key);
        bool curVisible = (it != myVisibility.end()) ? it->second : !hasOps;

        if (ImGui::MenuItem(curVisible ? ICON_EYE_SLASH "  Скрыть" : ICON_EYE "  Показать")) {
            myVisibility[key] = !curVisible;
            if (myOcctView) myOcctView->SetShapeVisible(label, !curVisible);
        }
        ImGui::Separator();
    }

    // === Копировать ===
    if (!isSystem) {
        if (ImGui::MenuItem("Копировать")) {
            myClipboardLabel = label;
            std::cout << "Copied: " << labelName << "\n";
        }
    }

    // === Вставить ===
    if (!myClipboardLabel.IsNull()) {
        if (ImGui::MenuItem("Вставить")) {
            std::cout << "Paste — TODO\n";
        }
    }

    if (!isSystem) {
        ImGui::Separator();
    }

    // === Переименовать ===
    if (!isSystem) {
        if (ImGui::MenuItem("Переименовать")) {
            myPendingRenameLabel = label;
            std::strncpy(myRenameBuffer, labelName.c_str(), sizeof(myRenameBuffer) - 1);
            myRenameBuffer[sizeof(myRenameBuffer) - 1] = '\0';
            myShowRenameDialog = true;
        }
    }

    // === Свойства ===
    if (ImGui::MenuItem("Свойства...")) {
        myPendingPropertiesLabel = label;
        myShowPropertiesDialog = true;
    }

    // === Удалить ===
    if (!isSystem) {
        ImGui::Separator();
        if (ImGui::MenuItem("Удалить")) {
            RequestDelete(label);
        }
    }
}

void Application::DrawContextMenu3D() {
    if (myPendingContextMenu) {
        ImGui::SetNextWindowPos(myContextMenuPos, ImGuiCond_Always);
        ImGui::OpenPopup("##ctx_3d");
        myPendingContextMenu = false;
    }

    if (ImGui::BeginPopup("##ctx_3d")) {
        TDF_Label sel;
        if (myOcctView) sel = myOcctView->GetSelectedLabel();

        if (sel.IsNull()) {
            // === ПКМ по пустому месту ===
            if (ImGui::MenuItem("Домой (F6)")) {
                if (myOcctView) myOcctView->SetHomeView();
            }
            if (ImGui::MenuItem("Изометрия")) {
                if (myOcctView) myOcctView->SetHomeView();
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Вид")) {
                if (ImGui::MenuItem("Сверху"))
                    myOcctView->SetCameraDirectionAndUp(gp_Dir(0, 0, -1), gp_Dir(0, 1, 0));
                if (ImGui::MenuItem("Снизу"))
                    myOcctView->SetCameraDirectionAndUp(gp_Dir(0, 0, 1), gp_Dir(0, 1, 0));
                if (ImGui::MenuItem("Спереди"))
                    myOcctView->SetCameraDirectionAndUp(gp_Dir(0, -1, 0), gp_Dir(0, 0, 1));
                if (ImGui::MenuItem("Сзади"))
                    myOcctView->SetCameraDirectionAndUp(gp_Dir(0, 1, 0), gp_Dir(0, 0, 1));
                if (ImGui::MenuItem("Справа"))
                    myOcctView->SetCameraDirectionAndUp(gp_Dir(-1, 0, 0), gp_Dir(0, 0, 1));
                if (ImGui::MenuItem("Слева"))
                    myOcctView->SetCameraDirectionAndUp(gp_Dir(1, 0, 0), gp_Dir(0, 0, 1));
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Показать всё")) {
                if (myOcctView) myOcctView->FitAll();
            }
        } else {
            // === ПКМ по объекту — то же меню, что и в дереве ===
            std::string name = "<unnamed>";
            Handle(TDataStd_Name) nameAttr;
            if (sel.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
                name = ExtToUtf8(nameAttr->Get());
            }

            ImGui::TextDisabled("%s", name.c_str());
            ImGui::Separator();

            bool hasOps = myDocument ? myDocument->Has3DOperations() : false;
            DrawContextMenuItems(sel, hasOps);
        }
        ImGui::EndPopup();
    }
}

void Application::DrawTreeSplitter() {
    ImGuiIO& io = ImGui::GetIO();

    const float splitterW   = 5.0f;
    const float minTreeW    = 200.0f;
    const float maxTreeW    = 600.0f;

    const float topY    = 200.0f;
    const float bottomY = io.DisplaySize.y - 76.0f;
    const float xPos    = myTreeWidth;

    // Невидимое окно-полоса поверх границы
    ImGui::SetNextWindowPos(ImVec2(xPos - splitterW * 0.5f, topY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(splitterW, bottomY - topY), ImGuiCond_Always);
    ImGui::Begin("##tree_splitter", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::InvisibleButton("##splitter_btn", ImVec2(splitterW, bottomY - topY));

    bool hovered = ImGui::IsItemHovered();
    bool active  = ImGui::IsItemActive();

    if (hovered || active) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    if (active) {
        myTreeWidth += io.MouseDelta.x;
        if (myTreeWidth < minTreeW) myTreeWidth = minTreeW;
        if (myTreeWidth > maxTreeW) myTreeWidth = maxTreeW;
    }

    // Визуальный индикатор при перетаскивании
    if (active) {
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        draw->AddLine(ImVec2(xPos, topY), ImVec2(xPos, bottomY),
                      IM_COL32(80, 130, 180, 255), 2.0f);
    }

    ImGui::End();
}

void Application::DrawAddBoxDialog() {
    if (myShowAddBoxDialog) {
        ImGui::OpenPopup(Tr("dlg.addBox.title").c_str());
        myShowAddBoxDialog = false;
    }
    if (ImGui::BeginPopupModal(Tr("dlg.addBox.title").c_str(), nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText(Tr("dlg.name").c_str(), myNewBoxName, sizeof(myNewBoxName));
        ImGui::Separator();
        ImGui::Text("%s", Tr("dlg.position").c_str());
        ImGui::InputFloat("X##pos", &myNewBoxPos[0]);
        ImGui::InputFloat("Y##pos", &myNewBoxPos[1]);
        ImGui::InputFloat("Z##pos", &myNewBoxPos[2]);
        ImGui::Separator();
        ImGui::Text("%s", Tr("dlg.size").c_str());
        ImGui::InputFloat("DX", &myNewBoxSize[0]);
        ImGui::InputFloat("DY", &myNewBoxSize[1]);
        ImGui::InputFloat("DZ", &myNewBoxSize[2]);
        ImGui::Separator();
        if (ImGui::Button(Tr("btn.ok").c_str(), ImVec2(120, 0))) {
            if (myDocument) {
                myDocument->AddBoxFeature(myNewBoxName,
                    myNewBoxSize[0], myNewBoxSize[1], myNewBoxSize[2],
                    myNewBoxPos[0], myNewBoxPos[1], myNewBoxPos[2]);
                RefreshView();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(Tr("btn.cancel").c_str(), ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void Application::DrawDependenciesPanel() {
    ImGuiIO& io = ImGui::GetIO();

    // Панель только когда в эскизе
    if (myActiveSketchLabel.IsNull()) return;

    const float panelW = 280.0f;
    const float panelTop = 340.0f;   // под ViewCube + стрелками
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - panelW - 15, panelTop), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelW, 0), ImGuiCond_Always);

    ImGui::Begin(Tr("panel.dependencies").c_str(), nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::TextDisabled("%s", Tr("panel.deps.hint").c_str());

    // === Здесь будут отображаться ограничения эскиза (Coincident, Parallel, и т.д.) ===
    // Пока пусто — заполним, когда добавим solver.
    if (ImGui::BeginListBox("##deps_list", ImVec2(-1, 120))) {
        ImGui::TextDisabled("%s", Tr("panel.deps.empty").c_str());
        ImGui::EndListBox();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Fully Constrained: NO");
    ImGui::TextDisabled("DOF remaining: ?");

    ImGui::End();
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
    add("view.settings", "Настройки", "", [this]{ myShowSettingsDialog = true; });

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

void Application::DrawSettingsDialog() {
    if (!myShowSettingsDialog) return;

    ImGui::SetNextWindowSize(ImVec2(720, 480), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
                                    ImGui::GetIO().DisplaySize.y * 0.5f),
                             ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    if (ImGui::Begin("Настройки", &myShowSettingsDialog,
                      ImGuiWindowFlags_NoCollapse)) {

        // Левая панель — вкладки
        ImGui::BeginChild("##settings_tabs", ImVec2(180, 0), true);
        const char* tabs[] = {
            "Основные", "Графика", "Тема", "Язык", "Хоткеи", "О программе"
        };
        for (int i = 0; i < 6; i++) {
            if (ImGui::Selectable(tabs[i], myActiveSettingsTab == i)) {
                myActiveSettingsTab = i;
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Правая панель — контент
        ImGui::BeginChild("##settings_content", ImVec2(0, 0), true);

        switch (myActiveSettingsTab) {
            case 0: {
                ImGui::Text("Основные настройки");
                ImGui::Separator();
                ImGui::Spacing();

                static int units = 0;
                ImGui::Text("Единицы измерения:");
                ImGui::RadioButton("Миллиметры (мм)", &units, 0); ImGui::SameLine();
                ImGui::RadioButton("Дюймы (in)",      &units, 1);

                ImGui::Spacing();
                static int precision = 2;
                ImGui::SliderInt("Знаков после запятой", &precision, 0, 6);

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Автосохранение:");
                static bool autosave = true;
                ImGui::Checkbox("Включить автосохранение", &autosave);
                static int autosaveInterval = 5;
                ImGui::SliderInt("Интервал (мин)", &autosaveInterval, 1, 30);
                break;
            }
            case 1: {
                ImGui::Text("Графика");
                ImGui::Separator();
                ImGui::TextDisabled("(будет заполнено позже)");
                ImGui::Spacing();
                static int quality = 1;
                ImGui::Text("Качество рендеринга:");
                ImGui::RadioButton("Низкое",  &quality, 0); ImGui::SameLine();
                ImGui::RadioButton("Среднее", &quality, 1); ImGui::SameLine();
                ImGui::RadioButton("Высокое", &quality, 2);
                ImGui::Spacing();
                static bool aa = true;
                ImGui::Checkbox("Сглаживание (antialiasing)", &aa);
                static bool shadows = false;
                ImGui::Checkbox("Тени", &shadows);
                break;
            }
            case 2: {
                ImGui::Text("Тема оформления");
                ImGui::Separator();
                ImGui::Spacing();

                static int theme = myCurrentTheme;
                if (ImGui::RadioButton("Светлая", &theme, 0)) {
                    ApplyTheme(0);
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Тёмная", &theme, 1)) {
                    ApplyTheme(1);
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Авто", &theme, 2)) {
                    ApplyTheme(2);
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Цвета линий эскиза:");

                static ImVec4 colNormal(0.75f, 0.2f, 1.0f, 1.0f);
                ImGui::ColorEdit3("Основная (недоопределённая)", (float*)&colNormal);

                static ImVec4 colConstruction(0.55f, 0.55f, 0.75f, 1.0f);
                ImGui::ColorEdit3("Вспомогательная", (float*)&colConstruction);

                static ImVec4 colCenterline(0.9f, 0.45f, 0.15f, 1.0f);
                ImGui::ColorEdit3("Осевая", (float*)&colCenterline);

                ImGui::Spacing();
                ImGui::TextDisabled("(применение цветов — позже)");
                break;
            }
            case 3: {
                ImGui::Text("Язык интерфейса");
                ImGui::Separator();
                Language cur = Localization::Instance().GetLanguage();
                if (ImGui::RadioButton("English", cur == Language::EN))
                    Localization::Instance().SetLanguage(Language::EN);
                if (ImGui::RadioButton("Русский", cur == Language::RU))
                    Localization::Instance().SetLanguage(Language::RU);
                break;
            }
            case 4: {
                ImGui::Text("Горячие клавиши");
                ImGui::Separator();
                ImGui::TextDisabled("(редактор хоткеев — позже)");
                ImGui::Spacing();
                ImGui::Text("Ctrl+N  — Новый проект");
                ImGui::Text("Ctrl+O  — Открыть");
                ImGui::Text("Ctrl+S  — Сохранить");
                ImGui::Text("Ctrl+Z  — Отменить");
                ImGui::Text("Ctrl+Y  — Повторить");
                ImGui::Text("F11     — Полный экран");
                ImGui::Text("F6      — Изометрия (домашний вид)");
                ImGui::Text("Esc     — Прервать линию / выйти из эскиза");
                break;
            }
            case 5: {
                ImGui::Text("О программе BreezeCAD");
                ImGui::Separator();
                ImGui::Text("Версия: 0.1.0 (prototype)");
                ImGui::Text("Сборка: %s %s", __DATE__, __TIME__);
                ImGui::Spacing();
                ImGui::TextWrapped(
                    "BreezeCAD — лёгкая кроссплатформенная САПР на базе "
                    "OpenCASCADE и Dear ImGui.");
                ImGui::Spacing();
                ImGui::Text("Ядро: OpenCASCADE 8.0");
                ImGui::Text("UI: Dear ImGui + GLFW");
                break;
            }
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

std::string Application::GetLabelName(const TDF_Label& label) const {
    Handle(TDataStd_Name) nameAttr;
    if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
        TCollection_AsciiString ascii(nameAttr->Get());
        return ascii.ToCString();
    }
    // Если имени нет — генерируем "Line <tag>"
    return "Line" + std::to_string(label.Tag());
}

void Application::OpenDependencyDialog(int type) {
    myDependencyDialogOpen = true;
    myCurrentDependencyType = type;
    myDependencyBase.clear();
    myDependencyDependent.clear();
    // Позиция сохранится (myDependencyDialogPosSet)
}

void Application::DrawDependencyDialog() {
    if (!myDependencyDialogOpen) return;

    // Названия зависимостей по индексу
    static const char* depNames[] = {
        "Совпадение", "Параллельность", "Перпендикулярность",
        "Горизонталь", "Вертикаль", "Касание",
        "Концентричность", "Равенство"
    };
    const char* title = depNames[myCurrentDependencyType % 8];

    // Позиция — по умолчанию по центру, но запоминается после первого перетаскивания
    ImGuiIO& io = ImGui::GetIO();
    if (!myDependencyDialogPosSet) {
        myDependencyDialogPos = ImVec2(io.DisplaySize.x * 0.5f - 150,
                                        io.DisplaySize.y * 0.5f - 150);
        myDependencyDialogPosSet = true;
    }

    // Первая установка позиции — только один раз
    ImGui::SetNextWindowPos(myDependencyDialogPos, ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_Once);

    std::string winTitle = std::string(title) + "##dep_dialog";
    ImGui::Begin(winTitle.c_str(), nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);

    // Запоминаем позицию — она сохранится до конца сеанса
    myDependencyDialogPos = ImGui::GetWindowPos();

    // === Базовый объект ===
    ImGui::Text("Базовый объект:");
    ImGui::BeginChild("##base_list", ImVec2(0, 80), true);
    if (myDependencyBase.empty()) {
        ImGui::TextDisabled("(кликните по линии в сцене)");
    } else {
        for (size_t i = 0; i < myDependencyBase.size(); i++) {
            std::string name = GetLabelName(myDependencyBase[i]);
            ImGui::PushID((int)i);
            ImGui::Text("%s", name.c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20);
            if (ImGui::SmallButton("x")) {
                myDependencyBase.erase(myDependencyBase.begin() + i);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();

    // === Зависимые объекты ===
    ImGui::Text("Зависимые объекты:");
    ImGui::BeginChild("##dep_list", ImVec2(0, 80), true);
    if (myDependencyDependent.empty()) {
        ImGui::TextDisabled("(кликните по линиям в сцене)");
    } else {
        for (size_t i = 0; i < myDependencyDependent.size(); i++) {
            std::string name = GetLabelName(myDependencyDependent[i]);
            ImGui::PushID((int)(1000 + i));
            ImGui::Text("%s", name.c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20);
            if (ImGui::SmallButton("x")) {
                myDependencyDependent.erase(myDependencyDependent.begin() + i);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::Separator();

    // === Кнопки ===
    float btnW = 95.0f;
    if (ImGui::Button("Отмена", ImVec2(btnW, 0))) {
        myDependencyDialogOpen = false;
        myDependencyBase.clear();
        myDependencyDependent.clear();
        if (myOcctView) myOcctView->ClearSelection();
    }
    ImGui::SameLine();
    if (ImGui::Button("Применить", ImVec2(btnW, 0))) {
        // TODO: сохранить зависимость в OCAF
        std::cout << "Dependency '" << title << "' applied with "
                  << myDependencyBase.size() << " base and "
                  << myDependencyDependent.size() << " dependent objects\n";
        // Очищаем поля, но окно оставляем открытым
        myDependencyBase.clear();
        myDependencyDependent.clear();
        if (myOcctView) myOcctView->ClearSelection();
    }
    ImGui::SameLine();
    if (ImGui::Button("Готово", ImVec2(btnW, 0))) {
        myDependencyDialogOpen = false;
        myDependencyBase.clear();
        myDependencyDependent.clear();
        if (myOcctView) myOcctView->ClearSelection();
    }

    ImGui::End();
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
        if (glfwWindowShouldClose(myWindow->GetNativeWindow()) && !myShowUnsavedDialog) {
            int modIdx = -1;
            for (int i = 0; i < (int)myDocuments.size(); i++) {
                if (myDocuments[i]->IsModified()) { modIdx = i; break; }
            }
            if (modIdx >= 0) {
                glfwSetWindowShouldClose(myWindow->GetNativeWindow(), GLFW_FALSE);
                myPendingWindowClose = true;
                myPendingCloseDocIndex = modIdx;
                std::snprintf(myUnsavedDialogInfo, sizeof(myUnsavedDialogInfo),
                    "Проект «%s» содержит "
                    "несохранённые изменения.\n"
                    "Сохранить перед выходом?",
                    myProjectNames[modIdx].c_str());
                myShowUnsavedDialog = true;
            }
        }

        bool curF11 = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_F11) == GLFW_PRESS;
        if (curF11 && !prevF11) myWindow->ToggleFullscreen();
        prevF11 = curF11;

        // === Polling ПКМ через GLFW напрямую (ImGui колбэки перезаписаны нашими) ===
        if (!myDependencyDialogOpen && !myShowRenameDialog &&
            !myShowDeleteConfirm && !myShowPropertiesDialog) {
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
                ShowAllRespectingDefaults();
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
            DrawDocumentTree();
            DrawTreeSplitter();
            DrawAddBoxDialog();
            DrawPreviewInfo();
            DrawViewCube();
            DrawDependencyDialog();
            DrawRenameDialog();
            DrawDeleteConfirmDialog();
            DrawUnsavedChangesDialog();
            DrawPropertiesDialog();
            DrawContextMenu3D();
            UI::Widgets::DrawSelectionRect(*myOcctView, myWindow->GetNativeWindow());
            DrawRightPanel();
        }
        DrawHomeScreen();
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
        DrawSettingsDialog();

        // === Модалка «Не разработано» ===
        if (myShowNotImplementedDialog) {
            ImGui::OpenPopup("##not_implemented");
        }
        if (ImGui::BeginPopupModal("##not_implemented", &myShowNotImplementedDialog,
                                    ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Функция \"%s\" не разработана.", myNotImplementedFeature.c_str());
            ImGui::Spacing();
            ImGui::TextDisabled("Будет доступно в следующих версиях BreezeCAD.");
            ImGui::Spacing();
            ImGui::Separator();
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                myShowNotImplementedDialog = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
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

void Application::DrawRightPanel() {
    ImGuiIO& io = ImGui::GetIO();

    const float toolW    = 44.0f;
    const float toolH    = 34.0f;
    const float arrowH   = 20.0f;
    const float handleW  = 26.0f;
    const float pad      = 3.0f;
    const float panelW   = toolW + pad * 2.0f;   // = 50
    // Адаптив: если окно узкое — принудительно свернуть панель
    bool forceCollapsed = (io.DisplaySize.x < 1100.0f);
    bool panelExpanded = myRightPanelExpanded && !forceCollapsed;

    const float topY     = 235.0f;   // сразу под ribbon
    const float panelH   = 330.0f;   // по контенту
    const float rightEdge = io.DisplaySize.x;

    // === Хлястик (стрелка-таб) слева от панели ===
    const float edgeMargin = 14.0f;  // отступ от правого края
    float handleX = panelExpanded
                    ? (rightEdge - panelW - handleW - edgeMargin)
                    : (rightEdge - handleW - edgeMargin);
    float handleH = 84.0f;
    float handleY = topY + (panelH - handleH) * 0.35f;

    ImGui::SetNextWindowPos(ImVec2(handleX, handleY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(handleW, handleH), ImGuiCond_Always);
    ImGui::Begin("##right_handle", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Кастомный хлястик — рисуем через GetWindowPos (надёжнее)
    ImVec2 wpos = ImGui::GetWindowPos();
    ImVec2 hMin = wpos;
    ImVec2 hMax = ImVec2(wpos.x + handleW, wpos.y + handleH);

    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::InvisibleButton("##right_handle_btn", ImVec2(handleW, handleH));
    bool hClicked = ImGui::IsItemClicked();
    bool hHovered = ImGui::IsItemHovered();

    ImDrawList* hdl = ImGui::GetWindowDrawList();

    // Фон хлястика — отчётливо видимый
    ImU32 hBg;
    bool isLight = (ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).x > 0.5f);
    if (hHovered) {
        hBg = IM_COL32(60, 120, 200, 255);
    } else if (isLight) {
        hBg = IM_COL32(140, 130, 110, 240);   // тёмно-бежевый в светлой
    } else {
        hBg = IM_COL32(90, 100, 120, 240);    // серо-синий в тёмной
    }
    hdl->AddRectFilled(hMin, hMax, hBg, 6.0f);

    // Иконка шеврона
    const char* hIcon = panelExpanded ? "chevron-right" : "chevron-left";
    GLuint hTex = myIcons.Get(hIcon, 20);
    if (hTex != 0) {
        ImVec2 ip(hMin.x + (handleW - 20) * 0.5f,
                  hMin.y + (handleH - 20) * 0.5f);
        hdl->AddImage((ImTextureID)(intptr_t)hTex, ip,
                      ImVec2(ip.x + 20, ip.y + 20),
                      ImVec2(0, 0), ImVec2(1, 1),
                      ImGui::GetColorU32(UI::Widgets::IconTintColor(true)));
    }

    if (hClicked) {
        myRightPanelExpanded = !myRightPanelExpanded;
    }
    if (hHovered) {
        ImGui::SetTooltip(panelExpanded ? "Свернуть панель" : "Развернуть панель");
    }
    ImGui::End();

    if (!panelExpanded) return;

    // === Тело панели ===
    float panelX = rightEdge - panelW - edgeMargin;
    ImGui::SetNextWindowPos(ImVec2(panelX, topY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_Always);
    ImGui::Begin("##right_panel", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 5));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 6));

    // --- 1. Панорамировать ---
    if (UI::Widgets::SvgIconBtn(myIcons, "panel_pan", "hand", 20, toolW, toolH, "Панорамировать")) {
        // TODO: активировать режим панорамирования
    }
    myTooltipState.Draw("panel.pan", "Панорамировать",
        "Сдвинуть вид мышью",
        "Активирует инструмент панорамирования. После активации зажмите ЛКМ "
        "в 3D-сцене и перемещайте мышь, чтобы сдвинуть вид.");

    ImGui::Separator();

    // --- 2. Показать рамкой (с выпадающим меню) ---
    if (UI::Widgets::SvgIconBtn(myIcons, "panel_zoom", "search", 20, toolW, toolH, "Показать рамкой")) {
        // TODO: активировать режим "показать рамкой"
    }
    myTooltipState.Draw("panel.zoom", "Показать рамкой",
        "Зуммирование выделенной области",
        "Основное действие — приблизить область, выделенную рамкой. "
        "Стрелка вниз открывает дополнительные режимы зуммирования.");

    if (UI::Widgets::SvgIconBtn(myIcons, "zoom_dd", "chevron-down", 14, toolW, arrowH, "Доп. режимы зума")) {
        ImGui::OpenPopup("##zoom_menu");
        ImVec2 btnMin = ImGui::GetItemRectMin();
        ImVec2 pp(btnMin.x - 280.0f, btnMin.y);
        if (pp.x < 8.0f) pp.x = 8.0f;
        ImGui::SetNextWindowPos(pp, ImGuiCond_Always);
    }
    if (ImGui::BeginPopup("##zoom_menu")) {
        if (ImGui::MenuItem("Зуммировать")) {}
        if (ImGui::MenuItem("Показать всё")) {
            if (myOcctView) {
                if (myIsolationActive) {
                    ShowAllRespectingDefaults();
                }
                myOcctView->FitAll();
            }
        }
        if (ImGui::MenuItem("Показать рамкой")) {}
        if (ImGui::MenuItem("Показать выбранное")) {}
        ImGui::EndPopup();
    }

    ImGui::Separator();

    // --- 3. Свободная орбита (с выпадающим меню) ---
    if (UI::Widgets::SvgIconBtn(myIcons, "panel_orbit", "rotate-cw", 20, toolW, toolH, "Орбита")) {
        // TODO: активировать орбиту
    }
    myTooltipState.Draw("panel.orbit", "Свободная орбита",
        "Вращение модели вокруг центра",
        "Основное действие — свободное вращение вида. Стрелка вниз открывает "
        "различные режимы вращения (с зависимостями, поворот под углом).");

    if (UI::Widgets::SvgIconBtn(myIcons, "orbit_dd", "chevron-down", 14, toolW, arrowH, "Доп. режимы орбиты")) {
        ImGui::OpenPopup("##orbit_menu");
        ImVec2 btnMin = ImGui::GetItemRectMin();
        ImVec2 pp(btnMin.x - 280.0f, btnMin.y);
        if (pp.x < 8.0f) pp.x = 8.0f;
        ImGui::SetNextWindowPos(pp, ImGuiCond_Always);
    }
    if (ImGui::BeginPopup("##orbit_menu")) {
        if (ImGui::MenuItem("Орбита")) {}
        if (ImGui::MenuItem("Орбита с зависимостями")) {}
        if (ImGui::MenuItem("Повернуть под углом")) {}
        ImGui::EndPopup();
    }

    ImGui::Separator();

    // --- 4. Вид на объект ---
    if (UI::Widgets::SvgIconBtn(myIcons, "panel_viewon", "eye", 20, toolW, toolH, "Вид на объект")) {
        // TODO: активировать режим "вид на грань"
    }
    myTooltipState.Draw("panel.viewon", "Вид на объект",
        "Ориентировать вид по выбранной грани",
        "После активации кликните по любой грани модели — камера развернётся "
        "так, чтобы вы смотрели на эту грань перпендикулярно.");

    ImGui::Separator();

    // --- 5. Стили отображения (с выпадающим меню) ---
    if (UI::Widgets::SvgIconBtn(myIcons, "panel_style", "palette", 20, toolW, toolH, "Стиль отображения")) {
        // TODO: циклить стиль
    }
    myTooltipState.Draw("panel.style", "Стили отображения",
        "Тонированный, каркасный и другие режимы",
        "Основное действие — переключение стиля отображения модели. "
        "Стрелка вниз открывает полный список стилей.");

    if (UI::Widgets::SvgIconBtn(myIcons, "style_dd", "chevron-down", 14, toolW, arrowH, "Все стили отображения")) {
        ImGui::OpenPopup("##style_menu");
        ImVec2 btnMin = ImGui::GetItemRectMin();
        ImVec2 pp(btnMin.x - 280.0f, btnMin.y);
        if (pp.x < 8.0f) pp.x = 8.0f;
        ImGui::SetNextWindowPos(pp, ImGuiCond_Always);
    }
    if (ImGui::BeginPopup("##style_menu")) {
        if (ImGui::MenuItem("Тонированный с рёбрами")) {
            if (myOcctView) myOcctView->SetDisplayModeAll(1, true);   // AIS_Shaded = 1
        }
        if (ImGui::MenuItem("Тонированный")) {
            if (myOcctView) myOcctView->SetDisplayModeAll(1, false);
        }
        if (ImGui::MenuItem("Каркасный")) {
            if (myOcctView) myOcctView->SetDisplayModeAll(0, false);  // AIS_WireFrame = 0
        }
        ImGui::Separator();
        ImGui::MenuItem("Реалистичный (скоро)", nullptr, false, false);
        ImGui::MenuItem("Скрытые линии (скоро)", nullptr, false, false);
        ImGui::MenuItem("Прозрачный (скоро)", nullptr, false, false);
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);
    ImGui::End();
}

void Application::DrawHomeScreen() {
    if (!myShowHomeScreen) return;

    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y - 76.0f));  // -76 = таб 52 + статус 24
    ImGui::PushStyleColor(ImGuiCol_WindowBg, myTheme.bgContent);
    ImGui::Begin("##home_bg", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    const float titleH  = 32.0f;              // титульная полоса
    const float topBarH = 76.0f;              // топ-полоса с кнопками
    const float topH    = titleH + topBarH;   // = 108
    const float leftW   = 240.0f;
    const float pad     = 16.0f;

    // ===== ТИТУЛЬНАЯ ПОЛОСА (32 px) =====
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(0, 0), ImVec2(io.DisplaySize.x, titleH),
        myTheme.bgTitle);

    ImGui::SetCursorPos(ImVec2(pad, 8));
    ImGui::PushStyleColor(ImGuiCol_Text, myTheme.textMain);
    ImGui::TextUnformatted("BreezeCAD");
    ImGui::PopStyleColor();

    {
        char telBuf[128];
        std::snprintf(telBuf, sizeof(telBuf), "FPS: %.0f | RAM: %.0f MB",
                      io.Framerate, myLastRAMMB);
        ImVec2 tsz = ImGui::CalcTextSize(telBuf);
        float tx = io.DisplaySize.x - pad - tsz.x;
        ImGui::SetCursorPos(ImVec2(tx, 8));
        ImGui::TextDisabled("%s", telBuf);
    }

    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(0, titleH), ImVec2(io.DisplaySize.x, titleH),
        myTheme.border, 1.0f);

    // ===== ТОП-ПОЛОСА (76 px): Деталь / Сборка / Открыть =====
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(0, titleH), ImVec2(io.DisplaySize.x, topH),
        myTheme.bgRibbon);

    const float hbtnW   = 88.0f;
    const float hbtnH   = 74.0f;
    const float hbtnY   = titleH + (topBarH - hbtnH) * 0.5f;
    const float hbtnGap = 8.0f;
    const char* hbtnIcons[3]  = { "box", "boxes", "folder-open" };
    const char* hbtnLabels[3] = { "Деталь", "Сборка", "Открыть" };
    float hbx = pad;
    for (int bi = 0; bi < 3; bi++) {
        ImGui::PushID(1000 + bi);
        ImGui::SetCursorPos(ImVec2(hbx, hbtnY));
        ImGui::PushStyleColor(ImGuiCol_Button,        myTheme.homeBtnBg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, myTheme.homeBtnBgHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  myTheme.accent);
        bool hbPressed = ImGui::Button("##hbtn", ImVec2(hbtnW, hbtnH));
        ImGui::PopStyleColor(3);

        ImVec2 bmin = ImGui::GetItemRectMin();
        GLuint icoTex = myIcons.Get(hbtnIcons[bi], 32);
        if (icoTex) {
            float ix = bmin.x + (hbtnW - 32) * 0.5f;
            float iy = bmin.y + 12.0f;
            ImGui::GetWindowDrawList()->AddImage(
                (ImTextureID)(intptr_t)icoTex,
                ImVec2(ix, iy), ImVec2(ix + 32, iy + 32),
                ImVec2(0, 0), ImVec2(1, 1),
                ImGui::GetColorU32(UI::Widgets::IconTintColor(true)));
        }
        ImVec2 lsz = ImGui::CalcTextSize(hbtnLabels[bi]);
        float lx = bmin.x + (hbtnW - lsz.x) * 0.5f;
        float ly = bmin.y + hbtnH - lsz.y - 8.0f;
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(lx, ly), myTheme.homeBtnLabel, hbtnLabels[bi]);

        if (hbPressed) {
            if (bi == 0)      CreateNewDoc();
            else if (bi == 1) CreateNewDoc();   // TODO: сборка
            else if (bi == 2) OnOpen();
        }
        ImGui::PopID();
        hbx += hbtnW + hbtnGap;
    }

    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(0, topH), ImVec2(io.DisplaySize.x, topH),
        IM_COL32(60, 68, 82, 255), 1.0f);
    // ЛЕВАЯ КОЛОНКА
    ImGui::SetCursorPos(ImVec2(0, topH));
    ImGui::BeginChild("##home_left", ImVec2(leftW, io.DisplaySize.y - topH - 76.0f),
                      false, ImGuiWindowFlags_NoScrollbar);

    const float colH = io.DisplaySize.y - topH - 76.0f;

    // ===== Название BreezeCAD (крупно, полужирно) =====
    {
        ImFont* font = ImGui::GetFont();
        const float bigSize = 26.0f;
        const char* nm = "BreezeCAD";
        ImVec2 base = ImGui::GetCursorScreenPos();
        ImVec2 tsz = font->CalcTextSizeA(bigSize, FLT_MAX, 0.0f, nm);
        ImVec2 pos(base.x + (leftW - tsz.x) * 0.5f, base.y + 22.0f);
        ImU32 colText = myTheme.textMain;
        // Псевдо-жирный: дважды с 0.6 px
        ImGui::GetWindowDrawList()->AddText(font, bigSize, pos, colText, nm);
        ImGui::GetWindowDrawList()->AddText(font, bigSize, ImVec2(pos.x + 0.6f, pos.y), colText, nm);
    }

    // ===== Логотип 140x140 (под названием) =====
    {
        const float logoSz = 140.0f;
        float logoX = (leftW - logoSz) * 0.5f;
        float logoY = 22.0f + 36.0f + 16.0f;  // название + gap
        GLuint logo = myIcons.Get("breeze-logo", (int)logoSz);
        if (logo) {
            ImGui::SetCursorPos(ImVec2(logoX, logoY));
            ImGui::Image((ImTextureID)(intptr_t)logo, ImVec2(logoSz, logoSz));
        }
    }

    // ===== Хелпер: кнопка с фиксированной шириной и центрированным текстом =====
    auto homeBtn = [&](const char* id, const char* label, float w, float h, bool disabled) -> bool {
        ImGui::PushID(id);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##btn", ImVec2(w, h));
        bool clicked = ImGui::IsItemClicked();
        bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        ImU32 bg;
        if (disabled)      bg = myTheme.homeBtnBgDisabled;
        else if (clicked)  bg = myTheme.accent;
        else if (hovered)  bg = myTheme.homeBtnBgHover;
        else               bg = myTheme.homeBtnBg;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, 4.0f);
        ImVec2 tsz = ImGui::CalcTextSize(label);
        ImU32 tcol = disabled ? myTheme.homeBtnTextDisabled : myTheme.homeBtnLabel;
        dl->AddText(ImVec2(p.x + (w - tsz.x) * 0.5f, p.y + (h - tsz.y) * 0.5f), tcol, label);
        return clicked && !disabled;
    };

    // ===== Позиции в левой колонке (абсолютные Y от низа) =====
    // Схема снизу вверх:
    //   20   нижий отступ
    //   32   Параметры/Выход
    //   20   gap
    //   32   О программе
    //    6   gap
    //   32   Справка
    //    6   gap
    //   32   Управление модулями (disabled)
    //   16   gap
    //    1   разделитель
    //    8   gap
    //   36   Версия (2 строки)
    const float bh      = 32.0f;
    const float gap     = 6.0f;
    const float bwFull  = leftW - pad * 2.0f;
    const float yParamsTop  = colH - 20.0f - bh;
    const float yAboutTop   = yParamsTop - 20.0f - bh;
    const float yHelpTop    = yAboutTop - gap - bh;
    const float yModulesTop = yHelpTop  - gap - bh;
    const float ySepY       = yModulesTop - 16.0f;
    const float yVerTop     = ySepY - 8.0f - 36.0f;

    // ===== Версия (над разделителем) =====
    {
        const char* v1 = "Версия 0.1.x";
        const char* v2 = "Прототип";
        ImVec2 s1 = ImGui::CalcTextSize(v1);
        ImVec2 s2 = ImGui::CalcTextSize(v2);
        ImGui::SetCursorPos(ImVec2((leftW - s1.x) * 0.5f, yVerTop));
        ImGui::TextDisabled("%s", v1);
        ImGui::SetCursorPos(ImVec2((leftW - s2.x) * 0.5f, yVerTop + 18.0f));
        ImGui::TextDisabled("%s", v2);
        ImGui::SetCursorPos(ImVec2(pad, ySepY));
        ImGui::Separator();
    }

    // ===== 3 кнопки внизу колонки (абсолютные Y) =====
    ImGui::SetCursorPos(ImVec2(pad, yModulesTop));
    homeBtn("btn_modules", "Управление модулями", bwFull, bh, true);
    ImGui::SetCursorPos(ImVec2(pad, yHelpTop));
    if (homeBtn("btn_help", "Справка", bwFull, bh, false)) myShowNotImplementedDialog = true;
    ImGui::SetCursorPos(ImVec2(pad, yAboutTop));
    if (homeBtn("btn_about", "О программе", bwFull, bh, false)) myShowSettingsDialog = true;

    // ===== Параметры / Выход (самый низ) =====
    {
        const float bw2 = (bwFull - 8.0f) * 0.5f;
        ImGui::SetCursorPos(ImVec2(pad, yParamsTop));
        if (homeBtn("btn_settings", "Параметры", bw2, bh, false)) myShowSettingsDialog = true;
        ImGui::SetCursorPos(ImVec2(pad + bw2 + 8.0f, yParamsTop));
        if (homeBtn("btn_exit", "Выход", bw2, bh, false)) {
            glfwSetWindowShouldClose(myWindow->GetNativeWindow(), GLFW_TRUE);
        }
    }
    ImGui::EndChild();
    // ПРАВАЯ ОБЛАСТЬ
    const float rightX = leftW + 1.0f;
    const float rightW = io.DisplaySize.x - rightX;
    const float contentY = topH;

    ImGui::SetCursorPos(ImVec2(rightX, contentY));
    ImGui::BeginChild("##home_right", ImVec2(rightW, io.DisplaySize.y - topH - 76.0f),
                      false, ImGuiWindowFlags_NoScrollbar);

    // Заголовок по центру
    {
        const char* h = "Последние документы";
        ImVec2 hs = ImGui::CalcTextSize(h);
        ImGui::SetCursorPos(ImVec2((rightW - hs.x) * 0.5f, 20.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, myTheme.textMain);
        ImGui::TextUnformatted(h);
        ImGui::PopStyleColor();
    }

    // Тумблер Плитки/Список + Поиск
    {
        const float toggleY = 60.0f;
        const float tgW = 36.0f;
        const float tgH = 28.0f;

        // Тумблер слева
        ImGui::SetCursorPos(ImVec2(pad, toggleY));
        ImGui::PushID(2001);
        bool activeTiles = (myHomeViewMode == 0);
        ImGui::PushStyleColor(ImGuiCol_Button, activeTiles
            ? ImVec4(0.20f, 0.55f, 0.95f, 1.0f)
            : ImVec4(0.20f, 0.23f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.40f, 0.55f, 1.0f));
        if (ImGui::Button("##tg_tiles", ImVec2(tgW, tgH))) myHomeViewMode = 0;
        ImGui::PopStyleColor(2);
        {
            ImVec2 mn = ImGui::GetItemRectMin();
            GLuint ico = myIcons.Get("layout-grid", 18);
            if (ico) {
                ImGui::GetWindowDrawList()->AddImage(
                    (ImTextureID)(intptr_t)ico,
                    ImVec2(mn.x + (tgW - 18) * 0.5f, mn.y + (tgH - 18) * 0.5f),
                    ImVec2(mn.x + (tgW + 18) * 0.5f, mn.y + (tgH + 18) * 0.5f),
                    ImVec2(0, 0), ImVec2(1, 1),
                    ImGui::GetColorU32(UI::Widgets::IconTintColor(true)));
            }
        }
        ImGui::PopID();

        ImGui::SameLine(0, 4);
        ImGui::PushID(2002);
        ImGui::PushStyleColor(ImGuiCol_Button, !activeTiles
            ? ImVec4(0.20f, 0.55f, 0.95f, 1.0f)
            : ImVec4(0.20f, 0.23f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.40f, 0.55f, 1.0f));
        if (ImGui::Button("##tg_list", ImVec2(tgW, tgH))) myHomeViewMode = 1;
        ImGui::PopStyleColor(2);
        {
            ImVec2 mn = ImGui::GetItemRectMin();
            GLuint ico = myIcons.Get("list", 18);
            if (ico) {
                ImGui::GetWindowDrawList()->AddImage(
                    (ImTextureID)(intptr_t)ico,
                    ImVec2(mn.x + (tgW - 18) * 0.5f, mn.y + (tgH - 18) * 0.5f),
                    ImVec2(mn.x + (tgW + 18) * 0.5f, mn.y + (tgH + 18) * 0.5f),
                    ImVec2(0, 0), ImVec2(1, 1),
                    ImGui::GetColorU32(UI::Widgets::IconTintColor(true)));
            }
        }
        ImGui::PopID();

        // Поиск справа
        const float searchW = 260.0f;
        ImGui::SetCursorPos(ImVec2(rightW - pad - searchW, toggleY));
        ImGui::SetNextItemWidth(searchW);
        ImGui::InputTextWithHint("##home_search", "Поиск...",
                                 myHomeSearch, sizeof(myHomeSearch));
    }

    // Контент
    const float listY = 110.0f;
    const float listH = io.DisplaySize.y - topH - 76.0f - listY - 16.0f;
    ImGui::SetCursorPos(ImVec2(pad, listY));
    ImGui::BeginChild("##home_content", ImVec2(rightW - pad * 2, listH), false,
                      ImGuiWindowFlags_NoScrollbar);

    std::string searchLower = myHomeSearch;
    for (auto& ch : searchLower) ch = (char)std::tolower((unsigned char)ch);

    if (myHomeViewMode == 0) {
        // ===== ПЛИТКИ =====
        const float tileW = 200.0f;
        const float tileH = 200.0f;
        const float spacing = 16.0f;
        int col = 0; float x = 0, y = 0;
        bool any = false;
        for (int i = 0; i < (int)myDocuments.size(); i++) {
            std::string nm = myProjectNames[i];
            std::string lname = nm;
            for (auto& ch : lname) ch = (char)std::tolower((unsigned char)ch);
            if (!searchLower.empty() && lname.find(searchLower) == std::string::npos) continue;
            any = true;
            ImGui::PushID(i);
            ImGui::SetCursorPos(ImVec2(x, y));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, myTheme.bgCardBg);
            ImGui::PushStyleColor(ImGuiCol_Border,  myTheme.borderStrong);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
            ImGui::BeginChild(("##tile_" + std::to_string(i)).c_str(),
                              ImVec2(tileW, tileH), true, ImGuiWindowFlags_NoScrollbar);
            ImVec2 thumbPos = ImGui::GetCursorScreenPos();
            ImVec2 thumbSize(tileW - 24, 120);
            bool thumbLoaded = false;
            std::string thumbPath = GetThumbnailPath(i);
            if (!thumbPath.empty() && std::filesystem::exists(thumbPath)) {
                static std::map<std::string, ImTextureID> s_thumbs;
                auto cacheIt = s_thumbs.find(thumbPath);
                if (cacheIt == s_thumbs.end()) {
                    int w = 0, h = 0, channels = 0;
                    unsigned char* data = ::stbi_load(thumbPath.c_str(), &w, &h, &channels, 4);
                    if (data) {
                        GLuint texId = 0;
                        glGenTextures(1, &texId);
                        glBindTexture(GL_TEXTURE_2D, texId);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
                        ::stbi_image_free(data);
                        s_thumbs[thumbPath] = (ImTextureID)(intptr_t)texId;
                        cacheIt = s_thumbs.find(thumbPath);
                    }
                }
                if (cacheIt != s_thumbs.end()) {
                    ImGui::GetWindowDrawList()->AddImage(cacheIt->second, thumbPos,
                        ImVec2(thumbPos.x + thumbSize.x, thumbPos.y + thumbSize.y));
                    thumbLoaded = true;
                }
            }
            if (!thumbLoaded) {
                ImGui::GetWindowDrawList()->AddRectFilled(thumbPos,
                    ImVec2(thumbPos.x + thumbSize.x, thumbPos.y + thumbSize.y),
                    myTheme.thumbBg, 6.0f);
                ImVec2 ts = ImGui::CalcTextSize("[ 3D ]");
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(thumbPos.x + (thumbSize.x - ts.x) * 0.5f,
                           thumbPos.y + (thumbSize.y - ts.y) * 0.5f),
                    myTheme.textDim, "[ 3D ]");
            }
            ImGui::SetCursorPosY(132);
            ImGui::Text("%s", myProjectNames[i].c_str());
            ImGui::SetCursorPosY(152);
            ImGui::TextDisabled("%s", myProjectPaths[i].empty() ? "<не сохранён>" : myProjectPaths[i].c_str());
            ImGui::SetCursorPosY(172);
            if (ImGui::Button("Открыть", ImVec2((tileW - 36) * 0.5f - 2, 0))) {
                SwitchToDoc(i);
                myShowHomeScreen = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Закрыть", ImVec2((tileW - 36) * 0.5f - 2, 0))) {
                CloseDoc(i);
                ImGui::EndChild();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(2);
                ImGui::PopID();
                break;
            }
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
            ImGui::PopID();
            col++;
            if (col >= 4) { col = 0; x = 0; y += tileH + spacing; }
            else x += tileW + spacing;
        }
        if (!any) {
            ImGui::SetCursorPos(ImVec2(20, 40));
            ImGui::TextDisabled("Нет недавних документов. Создайте новый — кнопка «Деталь» сверху.");
        }
    } else {
        // ===== СПИСОК =====
        if (ImGui::BeginTable("##home_list", 4,
                ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Имя",           ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn("Расположение",   ImGuiTableColumnFlags_WidthStretch, 4.0f);
            ImGui::TableSetupColumn("Дата",           ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableSetupColumn("Размер",         ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableHeadersRow();
            for (int i = 0; i < (int)myDocuments.size(); i++) {
                std::string nm = myProjectNames[i];
                std::string lname = nm;
                for (auto& ch : lname) ch = (char)std::tolower((unsigned char)ch);
                if (!searchLower.empty() && lname.find(searchLower) == std::string::npos) continue;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(i);
                bool sel = ImGui::Selectable(nm.c_str(), false,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
                if (sel && ImGui::IsMouseDoubleClicked(0)) {
                    SwitchToDoc(i);
                    myShowHomeScreen = false;
                }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s", myProjectPaths[i].empty() ? "<не сохранён>" : myProjectPaths[i].c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled("—");
                ImGui::TableSetColumnIndex(3);
                ImGui::TextDisabled("—");
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    ImGui::EndChild();  // ##home_right
    ImGui::End();
    ImGui::PopStyleColor();
}

} // namespace BreezeCAD
