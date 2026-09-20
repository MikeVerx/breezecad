#include "app/Application.h"
#include "app/Localization.h"
#include "app/IconsFontAwesome.h"
#include "doc/BreezeApplication.h"
#include "platform/FileDialog.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <TDF_ChildIterator.hxx>
#include <OSD_MemInfo.hxx>
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
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace BreezeCAD {

// Чистый C++ конвертеры (см. BreezeDocument.cpp — там идентичные, но локальные)

static TCollection_ExtendedString Utf8ToExt(const std::string& s) {
    if (s.empty()) return TCollection_ExtendedString();
    std::wstring ws;
    ws.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp = 0;
        int extra = 0;
        if      (c < 0x80) { cp = c;          extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else { i++; continue; }
        if (i + extra >= s.size()) break;
        for (int k = 1; k <= extra; k++) {
            unsigned char cc = (unsigned char)s[i + k];
            if ((cc & 0xC0) != 0x80) { cp = 0; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        i += extra + 1;
        if (cp < 0x10000) {
            ws.push_back((wchar_t)cp);
        } else if (cp <= 0x10FFFF) {
            cp -= 0x10000;
            ws.push_back((wchar_t)(0xD800 + (cp >> 10)));
            ws.push_back((wchar_t)(0xDC00 + (cp & 0x3FF)));
        }
    }
    ws.push_back(0);
    return TCollection_ExtendedString(ws.c_str());
}

static std::string ExtToUtf8(const TCollection_ExtendedString& es) {
    const uint16_t* ws = (const uint16_t*)es.ToExtString();
    if (!ws) return "";
    std::string out;
    for (size_t i = 0; ws[i] != 0; i++) {
        uint32_t cp = ws[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && ws[i+1] != 0) {
            uint32_t lo = ws[i+1];
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                i++;
            }
        }
        if (cp < 0x80) {
            out.push_back((char)cp);
        } else if (cp < 0x800) {
            out.push_back((char)(0xC0 | (cp >> 6)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (cp >> 18)));
            out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}
static std::string LabelKey(const TDF_Label& label) {
    std::ostringstream oss;
    label.EntryDump(oss);
    return oss.str();
}

// Диапазоны глифов Font Awesome (Private Use Area)
static const ImWchar kFontAwesomeRanges[] = { 0xf000, 0xf8ff, 0 };

static void LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();

    // 1) Основной шрифт (латиница + кириллица)
    const std::vector<std::string> mainCandidates = {
        "assets/fonts/Roboto-Regular.ttf",
        "../assets/fonts/Roboto-Regular.ttf",
        "../../assets/fonts/Roboto-Regular.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };

    bool mainLoaded = false;
    for (const auto& path : mainCandidates) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) continue;
        ImFont* font = io.Fonts->AddFontFromFileTTF(
            path.c_str(), 18.0f, nullptr,
            io.Fonts->GetGlyphRangesCyrillic());
        if (font) {
            std::cout << "BreezeCAD: font '" << path << "'\n";
            mainLoaded = true;
            break;
        }
    }
    if (!mainLoaded) io.Fonts->AddFontDefault();

    // 2) Font Awesome — в режиме MergeMode поверх основного
    const std::vector<std::string> faCandidates = {
        "assets/fonts/fa-solid-900.ttf",
        "../assets/fonts/fa-solid-900.ttf",
        "../../assets/fonts/fa-solid-900.ttf",
    };

    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.PixelSnapH = true;
    cfg.GlyphMinAdvanceX = 18.0f;   // выравнивание иконок по ширине
    cfg.GlyphOffset = ImVec2(0.0f, 2.0f);  // вертикальное выравнивание

    for (const auto& path : faCandidates) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) continue;
        ImFont* fa = io.Fonts->AddFontFromFileTTF(
            path.c_str(), 18.0f, &cfg, kFontAwesomeRanges);
        if (fa) {
            std::cout << "BreezeCAD: Font Awesome loaded from '" << path << "'\n";
            break;
        }
    }
}

Application::Application() {
    myWindow = std::make_unique<GlfwWindow>(1280, 720, "BreezeCAD - Prototype");
    if (!myWindow->GetNativeWindow()) return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    LoadFonts();
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
        std::cout << "Saved to: " << myCurrentPath << "\n";
    }
}

void Application::OnOpen() {
    std::string path;
    if (!ShowFileDialog(false, path, "Открыть файл")) return;

    Handle(TDocStd_Document) loaded =
        BreezeApplication::Instance().OpenDocument(path);
    if (loaded.IsNull()) { std::cerr << "Open failed\n"; return; }

    AddDocFromFile(path, loaded);
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

        const char* eyeIcon = visible ? ICON_EYE : ICON_EYE_SLASH;
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(70, 80, 100, 120));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(90, 110, 140, 180));
        if (ImGui::Button(eyeIcon, ImVec2(24, 0))) {
            myVisibility[key] = !visible;
            if (myOcctView) myOcctView->SetShapeVisible(label, !visible);
        }
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
    }

    if (isSystem) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(160, 160, 160, 255));
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

void Application::DrawQuickAccessToolbar() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 5));

    // === 1. Логотип / Меню файл ===
    // Логотип/HOME — возврат на главную
    if (ImGui::Button(ICON_HOME, ImVec2(32, 0))) {
        myShowHomeScreen = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Главная");

    ImGui::SameLine();

    // Меню «Файл»
    if (ImGui::Button(ICON_FILE, ImVec2(32, 0))) {
        ImGui::OpenPopup("##file_menu");
    }
    DrawAdvancedTooltip("qat.file",
                        "Меню «Файл»",
                        "Создать, открыть, сохранить, экспортировать",
                        "Здесь собраны все операции с файлами проекта: создание нового документа, "
                        "открытие существующего, сохранение, экспорт в STEP/STL/OBJ, а также доступ к "
                        "списку недавних файлов и настройкам программы.");

    if (ImGui::BeginPopup("##file_menu")) {
        if (ImGui::MenuItem("Главная", "Ctrl+H", false, false)) {}
        ImGui::Separator();
        if (ImGui::MenuItem("Создать", "Ctrl+N")) OnNewDocument();
        if (ImGui::MenuItem("Открыть...", "Ctrl+O")) OnOpen();
        if (ImGui::MenuItem("Сохранить", "Ctrl+S")) OnSave();
        if (ImGui::MenuItem("Сохранить как...")) { myCurrentPath.clear(); OnSave(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Экспорт в STEP", "", false, false)) {}
        if (ImGui::MenuItem("Экспорт в STL", "", false, false)) {}
        if (ImGui::MenuItem("Экспорт в OBJ", "", false, false)) {}
        ImGui::Separator();
        if (ImGui::BeginMenu("Последние файлы")) {
            ImGui::MenuItem("(пока пусто)", "", false, false);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Настройки")) myShowSettingsDialog = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Закрыть проект")) {
            if (myActiveDocIndex >= 0) CloseDoc(myActiveDocIndex);
        }
        if (ImGui::MenuItem("Выход", "Alt+F4")) {
            glfwSetWindowShouldClose(myWindow->GetNativeWindow(), GLFW_TRUE);
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();

    // === 2. Undo / Redo ===
    if (ImGui::Button(ICON_UNDO, ImVec2(28, 0))) OnUndo();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Отменить (Ctrl+Z)");
    ImGui::SameLine();
    if (ImGui::Button(ICON_REDO, ImVec2(28, 0))) OnRedo();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Повторить (Ctrl+Y)");

    ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();

    // === 3. Save ===
    if (ImGui::Button(ICON_SAVE, ImVec2(32, 0))) OnSave();
    DrawAdvancedTooltip("qat.save",
                        "Сохранить (Ctrl+S)",
                        "Сохранить текущий проект в файл",
                        "Записывает все изменения текущего проекта в файл .cbf. Если проект "
                        "ещё не имеет имени файла — откроется диалог выбора пути. "
                        "Горячая клавиша — Ctrl+S.");
    ImGui::SameLine();

    // === 4. Settings ===
    if (ImGui::Button(ICON_SETTINGS, ImVec2(32, 0))) myShowSettingsDialog = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Настройки");

    ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();

    // === 5. Закреплённые команды ===
    for (size_t i = 0; i < myPinnedCommands.size(); i++) {
        ImGui::SameLine();
        ImGui::PushID((int)i);
        if (ImGui::Button(myPinnedCommands[i].c_str(), ImVec2(32, 0))) {}
        if (ImGui::BeginPopupContextItem("##pin_ctx")) {
            if (ImGui::MenuItem("Открепить от панели")) {
                myPinnedCommands.erase(myPinnedCommands.begin() + i);
                ImGui::EndPopup();
                ImGui::PopID();
                break;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    // === 6. Поиск команд (справа) ===
    ImGuiIO& io = ImGui::GetIO();
    float searchW     = 220.0f;
    float rightPanelW = 260.0f;
    float searchX     = io.DisplaySize.x - searchW - rightPanelW - 20.0f;

    ImGui::SameLine(searchX);
    ImGui::SetNextItemWidth(searchW);
    ImGui::InputTextWithHint("##cmd_search", "Поиск команд...",
                             mySearchBuffer, sizeof(mySearchBuffer));
    ImVec2 searchMin = ImGui::GetItemRectMin();
    ImVec2 searchMax = ImGui::GetItemRectMax();
    bool searchActive = ImGui::IsItemActive();

    // Результаты — плавающее окно (НЕ popup, чтобы не перехватывать клики)
    if (mySearchBuffer[0] != 0 && searchActive) {
        std::string query = mySearchBuffer;
        std::transform(query.begin(), query.end(), query.begin(),
                       [](unsigned char c){ return (char)std::tolower(c); });

        std::vector<CommandEntry*> matches;
        for (auto& cmd : myCommands) {
            std::string lower = cmd.name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c){ return (char)std::tolower(c); });
            if (lower.find(query) != std::string::npos) matches.push_back(&cmd);
        }

        ImGui::SetNextWindowPos(ImVec2(searchMin.x, searchMax.y + 2));
        ImGui::SetNextWindowSize(ImVec2(searchW, 0), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.95f);
        ImGui::Begin("##cmd_results_float", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        if (matches.empty()) {
            ImGui::TextDisabled("Ничего не найдено");
        } else {
            for (auto* cmd : matches) {
                ImGui::PushID(cmd->id.c_str());
                std::string label = cmd->name;
                if (!cmd->shortcut.empty()) label += "     " + cmd->shortcut;
                if (ImGui::Selectable(label.c_str())) {
                    if (cmd->action) cmd->action();
                    mySearchBuffer[0] = 0;
                }
                ImGui::PopID();
            }
        }
        ImGui::End();
    }

    // === 7. Телеметрия ===
    ImGui::SameLine(io.DisplaySize.x - rightPanelW);
    ImGui::Text("FPS: %.0f", io.Framerate);
    ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();
    ImGui::Text("RAM: %.0f MB", myLastRAMMB);
    ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();
    ImGui::Text("Obj: %d", myLastShapeCount);

    ImGui::PopStyleVar();
}

void Application::DrawRibbonTabs() {
    static const char* tabs[] = { "tab.sketch", "tab.model", "tab.annotate",
                                   "tab.tools", "tab.manage", "tab.view" };
    for (int i = 0; i < 6; i++) {
        if (i > 0) ImGui::SameLine();
        bool active = (myActiveRibbonTab == i);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(50, 90, 140, 255));
        if (ImGui::Button(Tr(tabs[i]).c_str(), ImVec2(120, 0))) {
            myActiveRibbonTab = i;
        }
        if (active) ImGui::PopStyleColor();
    }
}

void Application::DrawRibbonPlaceholderTab(const char* name) {
    ImGui::TextDisabled("Tab '%s' — not implemented yet", name);
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
static int RibbonSplitButton(const char* id,
                              const char* const* opts,
                              int optCount,
                              int& idx,
                              bool enabled,
                              float width,
                              float height) {
    int result = 0;
    ImGui::PushID(id);
    ImGui::BeginGroup();

    ImVec4 colNormal = enabled ? ImVec4(0.24f, 0.39f, 0.59f, 1.0f)
                                : ImVec4(0.51f, 0.16f, 0.16f, 1.0f);
    ImVec4 colHover  = enabled ? ImVec4(0.31f, 0.51f, 0.71f, 1.0f)
                                : ImVec4(0.51f, 0.16f, 0.16f, 1.0f);
    ImVec4 colActive = enabled ? ImVec4(0.16f, 0.27f, 0.43f, 1.0f)
                                : ImVec4(0.51f, 0.16f, 0.16f, 1.0f);

    const float arrowW = 24.0f;
    const float arrowH = 18.0f;
    const float mainW  = width - arrowW - 2.0f;   // -2 на зазор
    const float mainH  = height - arrowH - 2.0f;

    // ===== Верхняя часть: основная кнопка =====
    ImGui::PushStyleColor(ImGuiCol_Button, colNormal);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, colActive);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));

    if (ImGui::Button(Tr(opts[idx]).c_str(), ImVec2(width, mainH))) {
        if (enabled) result = 1;
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    // ===== Нижняя часть: кнопка со стрелкой =====
    ImGui::PushStyleColor(ImGuiCol_Button, colNormal);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, colActive);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

    if (ImGui::Button("  v  ", ImVec2(width, arrowH))) {
        if (enabled) result = 2;
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    if (ImGui::IsItemHovered() && enabled && optCount > 1) {
        ImGui::SetTooltip("%s", Tr("tool.more").c_str());
    }

    // Popup
    std::string popupName = std::string("##popup_") + id;
    if (result == 2) ImGui::OpenPopup(popupName.c_str());
    if (ImGui::BeginPopup(popupName.c_str())) {
        for (int i = 0; i < optCount; i++) {
            bool selected = (i == idx);
            if (ImGui::MenuItem(Tr(opts[i]).c_str(), nullptr, selected)) {
                idx = i;
                result = 3;
            }
        }
        ImGui::EndPopup();
    }

    ImGui::EndGroup();
    ImGui::PopID();
    return result;
}

void Application::DrawRibbonSketchTab() {
    bool inSketch = !myActiveSketchLabel.IsNull();

    // === Панель 1: Create ===
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", Tr("grp.create").c_str());

    static const char* lineOpts[]   = { "tool.line", "tool.curve", "tool.spline", "tool.bridge" };
    static const char* rectOpts[]   = { "tool.rect.2pt", "tool.rect.center", "tool.rect.3pt" };
    static const char* circleOpts[] = { "tool.circle.center", "tool.circle.2pt",
                                        "tool.circle.3pt", "tool.circle.tan" };
    static const char* arcOpts[]    = { "tool.arc.center", "tool.arc.3pt", "tool.arc.tan" };
    static const char* polyOpts[]   = { "tool.poly.inscribed", "tool.poly.circum" };

    int r;
    r = RibbonSplitButton("line", lineOpts, 4, myLineToolIndex, inSketch, 85, 46);
    if (r == 1) {
        myCurrentLineStyle = LS_Normal;
        myHasSketchStart = false;
        if (myOcctView) myOcctView->ClearPreviewLine();
    }
    ImGui::SameLine();
    RibbonSplitButton("rect", rectOpts, 3, myRectToolIndex, false, 85, 46);
    ImGui::SameLine();
    RibbonSplitButton("circle", circleOpts, 4, myCircleToolIndex, false, 85, 46);
    ImGui::SameLine();
    RibbonSplitButton("arc", arcOpts, 3, myArcToolIndex, false, 85, 46);
    ImGui::SameLine();
    RibbonSplitButton("poly", polyOpts, 2, myPolyToolIndex, false, 85, 46);
    ImGui::EndGroup();

    ImGui::SameLine(0, 8);

    // === Панель 2: Modify ===
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", Tr("grp.modify").c_str());

    static const char* trimOpts[]   = { "tool.trim", "tool.extend" };
    static const char* offsetOpts[] = { "tool.offset", "tool.scale" };
    static const char* moveOpts[]   = { "tool.move", "tool.copy", "tool.rotate", "tool.mirror" };

    RibbonSplitButton("trim", trimOpts, 2, myTrimToolIndex, false, 85, 46);
    ImGui::SameLine();
    RibbonSplitButton("offset", offsetOpts, 2, myOffsetToolIndex, false, 85, 46);
    ImGui::SameLine();
    RibbonSplitButton("move", moveOpts, 4, myMoveToolIndex, false, 85, 46);
    ImGui::EndGroup();

    ImGui::SameLine(0, 8);

    // === Панель 3: Constrain ===
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", Tr("grp.constrain").c_str());

    static const char* conOpts[] = { "tool.coincident", "tool.parallel", "tool.perpendicular",
                                     "tool.horizontal", "tool.vertical", "tool.tangent",
                                     "tool.concentric", "tool.equal" };
    int conResult = RibbonSplitButton("constrain", conOpts, 8, myConstrainToolIndex, inSketch, 130, 46);
    if (conResult == 1 || conResult == 3) {
        OpenDependencyDialog(myConstrainToolIndex);
    }
    ImGui::SameLine();
    static const char* dimOpts[] = { "tool.dimension", "tool.tangent", "tool.concentric" };
    static int myDimToolIndex = 0;
    RibbonSplitButton("dimension", dimOpts, 3, myDimToolIndex, false, 85, 46);
    ImGui::EndGroup();

    ImGui::SameLine(0, 8);

    // === Панель 4: Line Style ===
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", Tr("sketch.lineStyle").c_str());

    auto StyleBtn = [&](const char* label, LineStyle ls) {
        bool active = (myCurrentLineStyle == ls);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(50, 90, 140, 255));
        else        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 70, 90, 255));
        if (ImGui::Button(label, ImVec2(95, 44))) myCurrentLineStyle = ls;
        ImGui::PopStyleColor();
    };
    StyleBtn(Tr("sketch.normal").c_str(), LS_Normal);             ImGui::SameLine();
    StyleBtn(Tr("sketch.construction").c_str(), LS_Construction); ImGui::SameLine();
    StyleBtn(Tr("sketch.centerline").c_str(), LS_Centerline);
    ImGui::EndGroup();

    ImGui::SameLine(0, 8);

    // === Панель 5: Options ===
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", Tr("sketch.options").c_str());

    ImGui::Checkbox(Tr("sketch.showGrid").c_str(), &myShowGrid);
    if (ImGui::IsItemEdited() && myOcctView) myOcctView->ShowGrid(myShowGrid);
    ImGui::Checkbox(Tr("sketch.snapGrid").c_str(), &mySnapToGrid);
    ImGui::Checkbox(Tr("sketch.snapPoints").c_str(), &mySnapToPoints);
    ImGui::Checkbox(Tr("sketch.autoContinue").c_str(), &myAutoContinue);
    ImGui::EndGroup();

    ImGui::SameLine(0, 8);

    // === Панель 6: Exit ===
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", Tr("grp.exit").c_str());
    static const char* finishOpts[] = { "tool.finish" };
    int idx0 = 0;
    int rf = RibbonSplitButton("finish", finishOpts, 1, idx0, inSketch, 130, 46);
    if (rf == 1) ExitSketch();
    ImGui::SameLine();
    if (inSketch && ImGui::Button(Tr("sketch.clear").c_str(), ImVec2(110, 46))) {
        if (myDocument && !myActiveSketchLabel.IsNull()) {
            myDocument->ClearSketch(myActiveSketchLabel);
            if (myOcctView) myOcctView->ClearAllShapes();
            RefreshSketchLines();
            if (myOcctView) myOcctView->ShowGrid(myShowGrid);
        }
    }
    ImGui::EndGroup();
}

void Application::DrawRibbonModelTab() {
    // === Create ===
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", Tr("grp.create").c_str());

    static const char* extrudeOpts[] = { "tool.extrude" };
    static const char* revolveOpts[] = { "tool.revolve" };
    static const char* sweepOpts[]   = { "tool.sweep", "tool.loft" };
    static const char* holeOpts[]    = { "tool.hole" };

    int i0 = 0;
    RibbonSplitButton("extrude", extrudeOpts, 1, i0, false, 85, 46);
    ImGui::SameLine();
    RibbonSplitButton("revolve", revolveOpts, 1, i0, false, 85, 46);
    ImGui::SameLine();
    RibbonSplitButton("sweep", sweepOpts, 2, i0, false, 85, 46);
    ImGui::SameLine();
    RibbonSplitButton("hole", holeOpts, 1, i0, false, 85, 46);
    ImGui::EndGroup();

    ImGui::SameLine(0, 8);

    // === Work Features ===
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", Tr("grp.work").c_str());

    static const char* planeOpts[] = { "tool.plane" };
    static const char* axisOpts[]  = { "tool.axis" };
    static const char* pointOpts[] = { "tool.point" };

    RibbonSplitButton("plane", planeOpts, 1, i0, false, 85, 46);
    ImGui::SameLine();
    RibbonSplitButton("axis", axisOpts, 1, i0, false, 85, 46);
    ImGui::SameLine();
    RibbonSplitButton("point", pointOpts, 1, i0, false, 85, 46);
    ImGui::EndGroup();

    ImGui::SameLine(0, 8);

    // === Modify ===
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", Tr("grp.modify").c_str());

    static const char* filletOpts[] = { "tool.fillet" };
    static const char* chamferOpts[] = { "tool.chamfer" };
    static const char* shellOpts[]   = { "tool.shell" };
    static const char* mirrorOpts[]  = { "tool.mirror" };
    static const char* patternOpts[] = { "tool.pattern" };

    RibbonSplitButton("fillet", filletOpts, 1, i0, false, 85, 46);
    ImGui::SameLine();
    RibbonSplitButton("chamfer", chamferOpts, 1, i0, false, 85, 46);
    ImGui::SameLine();
    RibbonSplitButton("shell", shellOpts, 1, i0, false, 85, 46);
    ImGui::SameLine();
    RibbonSplitButton("mirror", mirrorOpts, 1, i0, false, 85, 46);
    ImGui::SameLine();
    RibbonSplitButton("pattern", patternOpts, 1, i0, false, 85, 46);
    ImGui::EndGroup();
}

void Application::DrawRibbon() {
    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, 155), ImGuiCond_Always);
    ImGui::Begin("##ribbon", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

    DrawQuickAccessToolbar();
    ImGui::Separator();
    DrawRibbonTabs();
    ImGui::Separator();
    if (ImGui::BeginChild("##ribbon_content", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        switch (myActiveRibbonTab) {
            case 0: DrawRibbonSketchTab(); break;
            case 1: DrawRibbonModelTab();  break;
            case 2: DrawRibbonPlaceholderTab("Annotate"); break;
            case 3: DrawRibbonPlaceholderTab("Tools"); break;
            case 4: DrawRibbonPlaceholderTab("Manage"); break;
            case 5: DrawRibbonPlaceholderTab("View"); break;
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

void Application::DrawMenuBar() {
    // Оставлен для совместимости — но UI теперь полностью в DrawRibbon()
    // Вызывается из Run()
}

void Application::DrawDocumentTree() {
    ImGuiIO& io = ImGui::GetIO();
    const float panelWidth = myTreeWidth;

    ImGui::SetNextWindowPos(ImVec2(0, 155), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelWidth, io.DisplaySize.y - 155 - 56), ImGuiCond_Always);

    ImGui::Begin("##tree_panel", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoTitleBar);

    ImGui::Text("%s", Tr("panel.title").c_str());
    ImGui::TextDisabled("%s", Tr("panel.hint.delete").c_str());
    ImGui::Separator();

    if (ImGui::Button(Tr("panel.addBox").c_str())) {
        myShowAddBoxDialog = true;
        std::strncpy(myNewBoxName, Tr("default.boxName").c_str(), sizeof(myNewBoxName) - 1);
        myNewBoxName[sizeof(myNewBoxName) - 1] = ' ';
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

            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(160, 160, 160, 255));
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

                    // Оси — только если нет 3D-операций
                    if (type == FT_Axis && hasOps) continue;

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
static void ComputeLengthAngle(const gp_Pln& plane, const gp_Pnt& from, const gp_Pnt& to,
                               double& length, double& angleDeg) {
    gp_Ax3 ax = plane.Position();
    gp_Pnt origin = ax.Location();
    gp_Dir xDir = ax.XDirection();
    gp_Dir yDir = ax.YDirection();

    gp_Vec v(from, to);
    double u = v.Dot(gp_Vec(xDir));
    double w = v.Dot(gp_Vec(yDir));

    length = std::sqrt(u*u + w*w);
    angleDeg = std::atan2(w, u) * 180.0 / M_PI;
}

// Принудительное выравнивание угла к ближайшему кратному 90° (Компас-стиль)
static gp_Pnt ApplyOrtho(const gp_Pln& plane, const gp_Pnt& from, const gp_Pnt& to) {
    gp_Ax3 ax = plane.Position();
    gp_Pnt origin = ax.Location();
    gp_Dir xDir = ax.XDirection();
    gp_Dir yDir = ax.YDirection();

    gp_Vec v(from, to);
    double u = v.Dot(gp_Vec(xDir));
    double w = v.Dot(gp_Vec(yDir));

    double len = std::sqrt(u*u + w*w);
    if (len < 1e-6) return to;

    // Выбираем ближайшее направление: 0 / 90 / 180 / 270
    double u2 = 0.0, w2 = 0.0;
    if (std::abs(u) >= std::abs(w)) {
        u2 = (u >= 0) ? len : -len;
        w2 = 0.0;
    } else {
        u2 = 0.0;
        w2 = (w >= 0) ? len : -len;
    }

    gp_Pnt result = from;  // орто работает от стартовой точки
    result.Translate(gp_Vec(xDir) * u2);
    result.Translate(gp_Vec(yDir) * w2);
    return result;
}

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
static gp_Pnt SnapToGridPoint(const gp_Pnt& p, const gp_Pln& plane) {
    gp_Ax3 ax = plane.Position();
    gp_Pnt origin = ax.Location();
    gp_Dir xDir   = ax.XDirection();
    gp_Dir yDir   = ax.YDirection();

    gp_Vec v(origin, p);
    double u = v.Dot(gp_Vec(xDir));
    double w = v.Dot(gp_Vec(yDir));

    const double step = 10.0;
    u = std::round(u / step) * step;
    w = std::round(w / step) * step;

    gp_Pnt snapped = origin;
    snapped.Translate(gp_Vec(xDir) * u);
    snapped.Translate(gp_Vec(yDir) * w);
    return snapped;
}

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
    myActiveRibbonTab = 0;  // Sketch tab

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
    myActiveRibbonTab = 1;  // 3D Model tab

    // Принудительно перерисовываем всё из документа —
    // это гарантирует, что плоскости и другие системные объекты вернутся
    RefreshView();
    if (myOcctView) myOcctView->FitAll();

    std::cout << "Application: exited sketch mode\n";
}

static gp_Pnt PointFromLengthAngle(const gp_Pln& plane,
                                    const gp_Pnt& from,
                                    double length, double angleDeg) {
    gp_Ax3 ax = plane.Position();
    gp_Dir xDir = ax.XDirection();
    gp_Dir yDir = ax.YDirection();

    double rad = angleDeg * M_PI / 180.0;
    double u = length * std::cos(rad);
    double w = length * std::sin(rad);

    gp_Pnt result = from;
    result.Translate(gp_Vec(xDir) * u);
    result.Translate(gp_Vec(yDir) * w);
    return result;
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

    ImGuiIO& io = ImGui::GetIO();

    const float cubeSize   = 100.0f;
    const float cubeHalf   = 38.0f;
    const float margin     = 15.0f;
    const float topOffset  = 170.0f;

    ImVec2 center(io.DisplaySize.x - margin - cubeSize * 0.5f,
                  topOffset + cubeSize * 0.5f);

    ImDrawList* draw = ImGui::GetForegroundDrawList();

    // Фон
    draw->AddCircleFilled(center, cubeSize * 0.55f, IM_COL32(40, 45, 55, 180));

    // Ориентация камеры
    gp_Dir camDir   = myOcctView->GetCameraDirection();
    gp_Dir camUp    = myOcctView->GetCameraUp();

    // Защита: cross-product нулевой, если dir и up параллельны.
    // В этом случае подставляем правый вектор по эвристике.
    gp_Vec crossVec = camDir.Crossed(camUp);
    gp_Dir camRight;
    if (crossVec.SquareMagnitude() < 1e-9) {
        // Выбираем "right" так, чтобы он был не параллелен camDir
        gp_Dir fallback(1, 0, 0);
        if (std::abs(camDir.Dot(fallback)) > 0.9) fallback = gp_Dir(0, 1, 0);
        camRight = camDir.Crossed(fallback);
    } else {
        camRight = gp_Dir(crossVec);
    }

    // 8 вершин куба ±1
    gp_Pnt C[8] = {
        {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},
        {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}
    };

    auto project = [&](const gp_Pnt& p) -> ImVec2 {
        double x = p.X()*camRight.X() + p.Y()*camRight.Y() + p.Z()*camRight.Z();
        double y = p.X()*camUp.X()    + p.Y()*camUp.Y()    + p.Z()*camUp.Z();
        return ImVec2(center.x + (float)x * cubeHalf,
                      center.y - (float)y * cubeHalf);
    };
    auto depthOf = [&](const gp_Pnt& p) -> double {
        return p.X()*camDir.X() + p.Y()*camDir.Y() + p.Z()*camDir.Z();
    };

    struct Face { int v[4]; gp_Dir n; const char* label; };
    Face faces[6] = {
        {{4,5,6,7}, {0,0, 1}, "TOP"},
        {{0,3,2,1}, {0,0,-1}, "BOT"},
        {{0,1,5,4}, {0,-1,0}, "FRONT"},
        {{2,3,7,6}, {0, 1,0}, "BACK"},
        {{1,2,6,5}, { 1,0,0}, "RIGHT"},
        {{3,0,4,7}, {-1,0,0}, "LEFT"},
    };

    struct Visible { int idx; double depth; ImVec2 pts[4]; };
    std::vector<Visible> vis;

    for (int fi = 0; fi < 6; fi++) {
        if (faces[fi].n.Dot(camDir) >= -0.01) continue;  // back-facing
        Visible v;
        v.idx = fi;
        double d = 0;
        for (int i = 0; i < 4; i++) {
            v.pts[i] = project(C[faces[fi].v[i]]);
            d += depthOf(C[faces[fi].v[i]]);
        }
        v.depth = d / 4.0;
        vis.push_back(v);
    }

    std::sort(vis.begin(), vis.end(),
              [](const Visible& a, const Visible& b){ return a.depth < b.depth; });

    auto faceColor = [](const gp_Dir& n) -> ImU32 {
        if (n.Z() > 0.5)  return IM_COL32(180, 195, 210, 255);
        if (n.Z() < -0.5) return IM_COL32(120, 130, 145, 255);
        if (n.Y() < -0.5) return IM_COL32(160, 175, 195, 255);
        if (n.Y() > 0.5)  return IM_COL32(140, 155, 175, 255);
        if (n.X() > 0.5)  return IM_COL32(150, 165, 185, 255);
        return IM_COL32(130, 145, 165, 255);
    };

    // Mouse hover detection
    ImVec2 mouse = io.MousePos;
    int hoveredFace = -1;
    if (!io.WantCaptureMouse) {
        for (int i = (int)vis.size() - 1; i >= 0; i--) {
            auto& v = vis[i];
            int sign = 0; bool inside = true;
            for (int e = 0; e < 4; e++) {
                ImVec2 a = v.pts[e], b = v.pts[(e+1)%4];
                float cr = (b.x-a.x)*(mouse.y-a.y) - (b.y-a.y)*(mouse.x-a.x);
                int s = (cr > 0) ? 1 : -1;
                if (sign == 0) sign = s;
                else if (sign != s) { inside = false; break; }
            }
            if (inside) { hoveredFace = i; break; }
        }
    }

    // Draw faces
    for (int i = 0; i < (int)vis.size(); i++) {
        auto& v = vis[i];
        ImU32 col     = faceColor(faces[v.idx].n);
        ImU32 outline = IM_COL32(30, 35, 45, 255);
        if (i == hoveredFace) col = IM_COL32(255, 220, 120, 255);
        draw->AddConvexPolyFilled(v.pts, 4, col);
        draw->AddPolyline(v.pts, 4, outline, ImDrawFlags_Closed, 1.5f);
    }

    // Labels
    for (auto& v : vis) {
        ImVec2 c2(0, 0);
        for (int i = 0; i < 4; i++) { c2.x += v.pts[i].x; c2.y += v.pts[i].y; }
        c2.x *= 0.25f; c2.y *= 0.25f;
        ImVec2 ts = ImGui::CalcTextSize(faces[v.idx].label);
        draw->AddText(ImVec2(c2.x - ts.x*0.5f, c2.y - ts.y*0.5f),
                      IM_COL32(25, 30, 40, 255), faces[v.idx].label);
    }

    // Click handling — защита от выхода за границы
    if (hoveredFace >= 0 && hoveredFace < (int)vis.size()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            gp_Dir n = faces[vis[hoveredFace].idx].n;
            gp_Dir newDir = n.Reversed();
            gp_Dir newUp(0, 0, 1);
            if (std::abs(newDir.Z()) > 0.5) {
                newUp = gp_Dir(0, 1, 0);  // top/bottom view: Y up
            }
            myOcctView->SetCameraDirectionAndUp(newDir, newUp);
        }
    }

    // Rotation arrows below cube
    float arrowY = center.y + cubeSize * 0.5f + 10.0f;
    ImGui::SetNextWindowPos(ImVec2(center.x - 55, arrowY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(110, 28), ImGuiCond_Always);
    ImGui::Begin("##viewcube_arrows", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(60, 70, 90, 220));
    if (ImGui::Button("<<", ImVec2(50, 24))) {
        myOcctView->RotateAroundUp(90.0);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Повернуть влево на 90°");
    ImGui::SameLine();
    if (ImGui::Button(">>", ImVec2(50, 24))) {
        myOcctView->RotateAroundUp(-90.0);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Повернуть вправо на 90°");
    ImGui::PopStyleColor();

    ImGui::End();
}

void Application::DrawStatusBar() {
    ImGuiIO& io = ImGui::GetIO();
    const float h = 24.0f;

    ImGui::SetNextWindowPos(ImVec2(0, io.DisplaySize.y - h), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, h), ImGuiCond_Always);
    ImGui::Begin("##statusbar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (!myActiveSketchLabel.IsNull()) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s",
                            Tr("sketch.mode").c_str());
        ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();
        ImGui::Text("%s", Tr("sketch.hint.lmb").c_str());
        ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();
        ImGui::Text("%s", Tr("sketch.hint.shift").c_str());
        ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();
        ImGui::Text("%s", Tr("sketch.hint.esc").c_str());

        if (myHasSketchStart) {
            ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f),
                                "L=%.2f mm, A=%.1f deg",
                                myPreviewLength, myPreviewAngle);
        }
    } else {
        ImGui::Text("%s", Tr("panel.hint.delete").c_str());
    }

    ImGui::End();
}

void Application::DrawAdvancedTooltip(const std::string& id,
                                       const std::string& name,
                                       const std::string& shortDesc,
                                       const std::string& longDesc,
                                       const std::string& imagePath) {
    // Вызывается сразу после кнопки. Проверяем — наведена ли она.
    if (!ImGui::IsItemHovered()) {
        // Если это тот же элемент, что сейчас в hover — сбрасываем
        if (myHoveredItemId == id) {
            myHoveredItemId.clear();
            myHoverStartTime = 0.0;
            myLastTooltipLevel = 0;
        }
        return;
    }

    // Только что навели на новый элемент
    if (myHoveredItemId != id) {
        myHoveredItemId = id;
        myHoverStartTime = ImGui::GetTime();
        myLastTooltipLevel = 0;
    }

    double elapsed = ImGui::GetTime() - myHoverStartTime;

    int level = 0;
    if (elapsed >= 4.0)      level = 3;
    else if (elapsed >= 2.0) level = 2;
    else if (elapsed >= 0.5) level = 1;
    else                     return;  // ещё рано

    if (level == 0) return;

    // Рисуем tooltip
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.0f);

    // Уровень 1: название
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", name.c_str());

    // Уровень 2: краткое описание
    if (level >= 2 && !shortDesc.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted(shortDesc.c_str());
    }

    // Уровень 3: подробное описание + картинка
    if (level >= 3) {
        if (!longDesc.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("%s", longDesc.c_str());
        }
        if (!imagePath.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("(картинка: %s)", imagePath.c_str());
            // TODO: загрузка PNG через imgui — следующая итерация
        }
    }

    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
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

    // === Плоскость ===
    if (type == FT_Plane) {
        if (ImGui::MenuItem(ICON_SKETCH "  Создать эскиз")) {
            myPendingCreateSketchLabel = label;
        }
        ImGui::Separator();
    }

    // === Эскиз ===
    if (type == FT_Sketch) {
        if (ImGui::MenuItem(ICON_PENCIL "  Редактировать эскиз")) {
            myPendingEnterSketchLabel = label;
        }
        ImGui::Separator();
    }

    // === 3D-операция ===
    if (type == FT_Box || type == FT_Cylinder || type == FT_Hole) {
        if (ImGui::MenuItem(ICON_PENCIL "  Редактировать")) {}
        if (ImGui::MenuItem(ICON_RULER "  Измерить")) {}
        if (ImGui::MenuItem(ICON_SEARCH "  Изолировать")) {
            std::cout << "Isolate — TODO (отключено для отладки)\n";
        }
        ImGui::Separator();
    }

    // === Start ===
    if (type == FT_Start) {
        if (ImGui::MenuItem(ICON_EYE "  Показать все плоскости и оси")) {
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
        if (ImGui::MenuItem(ICON_EYE_SLASH "  Скрыть все плоскости и оси")) {
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
        if (ImGui::MenuItem(ICON_FILE "  Копировать")) {
            myClipboardLabel = label;
            std::cout << "Copied: " << labelName << "\n";
        }
    }

    // === Вставить ===
    if (!myClipboardLabel.IsNull()) {
        if (ImGui::MenuItem(ICON_FILE "  Вставить")) {
            std::cout << "Paste — TODO\n";
        }
    }

    if (!isSystem) {
        ImGui::Separator();
    }

    // === Переименовать ===
    if (!isSystem) {
        if (ImGui::MenuItem(ICON_RENAME "  Переименовать")) {
            myPendingRenameLabel = label;
            std::strncpy(myRenameBuffer, labelName.c_str(), sizeof(myRenameBuffer) - 1);
            myRenameBuffer[sizeof(myRenameBuffer) - 1] = '\0';
            myShowRenameDialog = true;
        }
    }

    // === Свойства ===
    if (ImGui::MenuItem(ICON_INFO "  Свойства...")) {
        myPendingPropertiesLabel = label;
        myShowPropertiesDialog = true;
    }

    // === Удалить ===
    if (!isSystem) {
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_TRASH "  Удалить")) {
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

    const float topY    = 155.0f;
    const float bottomY = io.DisplaySize.y - 56.0f;
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

void Application::DrawProjectTabs() {
    ImGuiIO& io = ImGui::GetIO();

    const float statusH = 24.0f;
    const float tabsH   = 32.0f;
    const float yPos    = io.DisplaySize.y - statusH - tabsH;

    ImGui::SetNextWindowPos(ImVec2(0, yPos), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, tabsH), ImGuiCond_Always);
    ImGui::Begin("##project_tabs", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    // === Вкладка "Главная" ===
    {
        bool isHome = (myActiveDocIndex < 0);
        if (isHome) ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(60, 100, 150, 255));
        if (ImGui::Button("⌂ Главная", ImVec2(120, 24))) {
            // Home — сбросить активный документ (но не закрыть)
            myActiveDocIndex = -1;
            myDocument = nullptr;
            myActiveSketchLabel = TDF_Label();
            myHasSketchStart = false;
            if (myOcctView) {
                myOcctView->ClearAllShapes();
                myOcctView->ClearGrid();
                myOcctView->ExitSketchMode();
            }
        }
        if (isHome) ImGui::PopStyleColor();
    }

    // === Вкладки проектов ===
    static int pendingCloseIdx = -1;
    static int switchToIdx    = -2;  // -2 = no change

    for (int i = 0; i < (int)myDocuments.size(); i++) {
        ImGui::SameLine();

        ImGui::PushID(i);

        bool active = (i == myActiveDocIndex);
        ImVec4 colNormal = active ? ImVec4(0.24f, 0.39f, 0.59f, 1.0f)
                                   : ImVec4(0.20f, 0.20f, 0.24f, 1.0f);
        ImVec4 colHover  = active ? ImVec4(0.31f, 0.51f, 0.71f, 1.0f)
                                   : ImVec4(0.28f, 0.28f, 0.34f, 1.0f);
        ImVec4 colActive = active ? ImVec4(0.16f, 0.27f, 0.43f, 1.0f)
                                   : ImVec4(0.16f, 0.16f, 0.20f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, colNormal);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, colActive);

        // Имя вкладки + " *" если не сохранён
        std::string label = myProjectNames[i];
        if (myProjectPaths[i].empty()) label += " *";

        if (ImGui::Button((label + "##tab").c_str(), ImVec2(140, 24))) {
            switchToIdx = i;
        }

        // ПКМ — контекстное меню
        if (ImGui::BeginPopupContextItem("##tab_ctx")) {
            if (ImGui::MenuItem("Сохранить")) {
                // TODO: save this doc
            }
            if (ImGui::MenuItem("Сохранить как...")) {
                // TODO: save as
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Закрыть")) {
                pendingCloseIdx = i;
            }
            if (ImGui::MenuItem("Закрыть другие")) {
                // TODO: close others
            }
            ImGui::EndPopup();
        }

        ImGui::PopStyleColor(3);

        // Кнопка × закрытия — только если больше одного документа
        if (myDocuments.size() > 1) {
            ImGui::SameLine(0, 2);
            if (ImGui::Button(ICON_CLOSE, ImVec2(20, 24))) {
                pendingCloseIdx = i;
            }
        }

        ImGui::PopID();
    }

    // Кнопка "+" — создать новый проект
    ImGui::SameLine();
    if (ImGui::Button(ICON_PLUS, ImVec2(28, 24))) {
        CreateNewDoc();
        RefreshView();
        if (myOcctView) myOcctView->FitAll();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Новый проект");

    // === Прогресс-бар справа (placeholder) ===
    {
        float panelW = 200.0f;
        ImGui::SameLine(io.DisplaySize.x - panelW - 10);
        ImGui::TextDisabled("Готово");
    }

    ImGui::End();

    // === Отложенные операции (после закрытия окна — иначе ломаем итерацию) ===
    if (pendingCloseIdx >= 0) {
        CloseDoc(pendingCloseIdx);
        pendingCloseIdx = -1;
    }
    if (switchToIdx != -2) {
        if (switchToIdx >= 0) SwitchToDoc(switchToIdx);
        switchToIdx = -2;
    }
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
    // RAM через OCCT OSD_MemInfo — правильное имя: MemWorkingSet
    OSD_MemInfo memInfo;
    size_t ws = memInfo.Value(OSD_MemInfo::MemWorkingSet);
    if (ws != Standard_Size(-1)) {
        myLastRAMMB = (double)ws / (1024.0 * 1024.0);
    } else {
        myLastRAMMB = 0.0;
    }

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
                ImGui::TextDisabled("(будет заполнено позже)");
                ImGui::Spacing();
                static int units = 0;
                ImGui::Text("Единицы измерения:");
                ImGui::RadioButton("Миллиметры (мм)", &units, 0); ImGui::SameLine();
                ImGui::RadioButton("Дюймы (in)",      &units, 1);
                ImGui::Spacing();
                static int precision = 2;
                ImGui::SliderInt("Знаков после запятой", &precision, 0, 6);
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
                ImGui::TextDisabled("(будет заполнено позже)");
                ImGui::Spacing();
                static int theme = 2;
                ImGui::RadioButton("Светлая",            &theme, 0); ImGui::SameLine();
                ImGui::RadioButton("Тёмная",             &theme, 1); ImGui::SameLine();
                ImGui::RadioButton("Как в системе",      &theme, 2);
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
            if (ImGui::SmallButton(ICON_CLOSE)) {
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
            if (ImGui::SmallButton(ICON_CLOSE)) {
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
        myWindow->PollEvents();

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

        // F6 — Home view
        static bool prevF6 = false;
        bool curF6 = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_F6) == GLFW_PRESS;
        if (curF6 && !prevF6 && myOcctView) myOcctView->SetHomeView();
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
            DrawProjectTabs();
            DrawStatusBar();
            DrawViewCube();
            DrawDependencyDialog();
            DrawRenameDialog();
            DrawDeleteConfirmDialog();
            DrawPropertiesDialog();
            DrawContextMenu3D();
            DrawSelectionRect();
            DrawRightPanel();
        }
        DrawHomeScreen();
        DrawSettingsDialog();
        UpdateTelemetry();

        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 360, 30), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(350, 0), ImGuiCond_Always);
        ImGui::Begin(Tr("debug.title").c_str(), nullptr,
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        if (myOcctView) {
            TDF_Label sel = myOcctView->GetSelectedLabel();
            if (!sel.IsNull()) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), Tr("debug.selected.tag").c_str(), sel.Tag());
                ImGui::Text("%s", buf);
            } else {
                ImGui::TextDisabled("%s", Tr("debug.selected.none").c_str());
            }
        }
        if (!myCurrentPath.empty()) ImGui::Text("File: %s", myCurrentPath.c_str());
        else ImGui::TextDisabled("File: <unsaved>");
        ImGui::Separator();
        ImGui::Text("Ctrl+S save, Ctrl+O open");
        ImGui::Text("RMB on plane - create sketch");
        ImGui::End();

        ImGui::Render();

        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(myWindow->GetNativeWindow(), &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (myOcctView) myOcctView->Redraw();

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        myWindow->SwapBuffers();
      } catch (const std::exception& e) {
        std::cerr << "Runtime exception in main loop: " << e.what() << "\n";
      } catch (...) {
        std::cerr << "Unknown exception in main loop (ignored)\n";
      }
    }
}

void Application::DrawSelectionRect() {
    if (!myOcctView) return;
    if (!myOcctView->IsSelectionRectActive()) return;
    if (ImGui::GetIO().WantCaptureMouse) return;

    int x1, y1, x2, y2;
    myOcctView->GetSelectionRect(x1, y1, x2, y2);

    // Координаты в framebuffer'е — переводим в логические (ImGui)
    int winW = 1, winH = 1, fbW = 1, fbH = 1;
    glfwGetWindowSize(myWindow->GetNativeWindow(), &winW, &winH);
    glfwGetFramebufferSize(myWindow->GetNativeWindow(), &fbW, &fbH);

    float sx = (fbW > 0) ? (float)winW / fbW : 1.0f;
    float sy = (fbH > 0) ? (float)winH / fbH : 1.0f;

    ImVec2 p1(x1 * sx, y1 * sy);
    ImVec2 p2(x2 * sx, y2 * sy);

    ImVec2 tl(std::min(p1.x, p2.x), std::min(p1.y, p2.y));
    ImVec2 br(std::max(p1.x, p2.x), std::max(p1.y, p2.y));

    ImDrawList* draw = ImGui::GetForegroundDrawList();

    draw->AddRectFilled(tl, br, IM_COL32(80, 130, 180, 60));
    draw->AddRect(tl, br, IM_COL32(80, 130, 180, 255), 0.0f, 0, 1.5f);
}

void Application::DrawRightPanel() {
    ImGuiIO& io = ImGui::GetIO();

    const float toolW    = 40.0f;
    const float toolH    = 32.0f;
    const float arrowH   = 14.0f;
    const float handleW  = 10.0f;
    const float pad      = 2.0f;
    const float panelW   = toolW + pad * 2.0f;
    const float topY     = 340.0f;
    const float bottomY  = io.DisplaySize.y - 56.0f;
    const float panelH   = bottomY - topY;
    const float rightEdge = io.DisplaySize.x;

    // === Хлястик (стрелка-таб) слева от панели ===
    float handleX = myRightPanelExpanded
                    ? (rightEdge - panelW - handleW)
                    : (rightEdge - handleW);
    float handleH = 60.0f;
    float handleY = topY + (panelH - handleH) * 0.5f;

    ImGui::SetNextWindowPos(ImVec2(handleX, handleY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(handleW, handleH), ImGuiCond_Always);
    ImGui::Begin("##right_handle", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(60, 70, 90, 220));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(90, 110, 140, 240));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(40, 50, 70, 240));

    if (ImGui::Button(myRightPanelExpanded ? ICON_CHEVRON_RIGHT : ICON_CHEVRON_LEFT, ImVec2(handleW, handleH))) {
        myRightPanelExpanded = !myRightPanelExpanded;
    }
    ImGui::PopStyleColor(3);

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(myRightPanelExpanded ? "Свернуть панель" : "Развернуть панель");
    }
    ImGui::End();

    if (!myRightPanelExpanded) return;

    // === Тело панели ===
    float panelX = rightEdge - panelW;
    ImGui::SetNextWindowPos(ImVec2(panelX, topY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_Always);
    ImGui::Begin("##right_panel", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));

    // --- 1. Панорамировать ---
    if (ImGui::Button(ICON_PAN "##pan", ImVec2(toolW, toolH))) {
        // TODO: активировать режим панорамирования
    }
    DrawAdvancedTooltip("panel.pan", "Панорамировать",
        "Сдвинуть вид мышью",
        "Активирует инструмент панорамирования. После активации зажмите ЛКМ "
        "в 3D-сцене и перемещайте мышь, чтобы сдвинуть вид.");

    ImGui::Separator();

    // --- 2. Показать рамкой (с выпадающим меню) ---
    if (ImGui::Button(ICON_ZOOM "##zoom", ImVec2(toolW, toolH))) {
        // TODO: активировать режим "показать рамкой"
    }
    DrawAdvancedTooltip("panel.zoom", "Показать рамкой",
        "Зуммирование выделенной области",
        "Основное действие — приблизить область, выделенную рамкой. "
        "Стрелка вниз открывает дополнительные режимы зуммирования.");

    if (ImGui::Button(ICON_CHEVRON_DOWN "##zoom_dd", ImVec2(toolW, arrowH))) {
        ImGui::OpenPopup("##zoom_menu");
    }
    if (ImGui::BeginPopup("##zoom_menu")) {
        if (ImGui::MenuItem("Зуммировать")) {}
        if (ImGui::MenuItem("Показать всё")) {
            if (myOcctView) myOcctView->FitAll();
        }
        if (ImGui::MenuItem("Показать рамкой")) {}
        if (ImGui::MenuItem("Показать выбранное")) {}
        ImGui::EndPopup();
    }

    ImGui::Separator();

    // --- 3. Свободная орбита (с выпадающим меню) ---
    if (ImGui::Button(ICON_ORBIT "##orbit", ImVec2(toolW, toolH))) {
        // TODO: активировать орбиту
    }
    DrawAdvancedTooltip("panel.orbit", "Свободная орбита",
        "Вращение модели вокруг центра",
        "Основное действие — свободное вращение вида. Стрелка вниз открывает "
        "различные режимы вращения (с зависимостями, поворот под углом).");

    if (ImGui::Button(ICON_CHEVRON_DOWN "##orbit_dd", ImVec2(toolW, arrowH))) {
        ImGui::OpenPopup("##orbit_menu");
    }
    if (ImGui::BeginPopup("##orbit_menu")) {
        if (ImGui::MenuItem("Орбита")) {}
        if (ImGui::MenuItem("Орбита с зависимостями")) {}
        if (ImGui::MenuItem("Повернуть под углом")) {}
        ImGui::EndPopup();
    }

    ImGui::Separator();

    // --- 4. Вид на объект ---
    if (ImGui::Button(ICON_VIEW_ON "##viewon", ImVec2(toolW, toolH))) {
        // TODO: активировать режим "вид на грань"
    }
    DrawAdvancedTooltip("panel.viewon", "Вид на объект",
        "Ориентировать вид по выбранной грани",
        "После активации кликните по любой грани модели — камера развернётся "
        "так, чтобы вы смотрели на эту грань перпендикулярно.");

    ImGui::Separator();

    // --- 5. Стили отображения (с выпадающим меню) ---
    if (ImGui::Button(ICON_STYLE "##style", ImVec2(toolW, toolH))) {
        // TODO: циклить стиль
    }
    DrawAdvancedTooltip("panel.style", "Стили отображения",
        "Тонированный, каркасный и другие режимы",
        "Основное действие — переключение стиля отображения модели. "
        "Стрелка вниз открывает полный список стилей.");

    if (ImGui::Button(ICON_CHEVRON_DOWN "##style_dd", ImVec2(toolW, arrowH))) {
        ImGui::OpenPopup("##style_menu");
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

    // === Фоновое затемнение 3D-сцены ===
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(20, 22, 28, 240));
    ImGui::Begin("##home_bg", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // === Заголовок ===
    ImGui::SetCursorPos(ImVec2(40, 40));
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
    ImGui::TextColored(ImVec4(0.9f, 0.95f, 1.0f, 1.0f), "BreezeCAD");
    ImGui::PopFont();
    ImGui::SetCursorPos(ImVec2(40, 70));
    ImGui::TextDisabled("Проекты");

    // === Кнопки управления (сверху справа) ===
    // Общая ширина: 180 + 10 + 180 + 10 + 50 = 430. Отступ от правого края — 40.
    const float totalBtnW = 430.0f;
    float btnX = io.DisplaySize.x - totalBtnW - 40.0f;
    if (btnX < 40.0f) btnX = 40.0f;  // защита от узкого окна

    ImGui::SetCursorPos(ImVec2(btnX, 40));

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20, 12));

    if (ImGui::Button(ICON_PLUS "  Создать", ImVec2(180, 0))) {
        CreateNewDoc();
        myShowHomeScreen = false;
        RefreshView();
        if (myOcctView) myOcctView->FitAll();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FILE "  Открыть", ImVec2(180, 0))) {
        OnOpen();
        if (myActiveDocIndex >= 0) myShowHomeScreen = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_SETTINGS, ImVec2(50, 0))) {
        myShowSettingsDialog = true;
    }

    ImGui::PopStyleVar();

    // === Разделитель ===
    ImGui::SetCursorPos(ImVec2(40, 120));
    ImGui::Separator();
    ImGui::SetCursorPos(ImVec2(40, 140));

    // === Плитки проектов ===
    const float tileW = 240.0f;
    const float tileH = 220.0f;
    const float spacing = 20.0f;
    int col = 0;
    float x = 40.0f;
    float y = 180.0f;

    ImGui::BeginChild("##home_tiles", ImVec2(0, 0), false);

    for (int i = 0; i < (int)myDocuments.size(); i++) {
        ImGui::PushID(i);

        ImVec2 pos(x + col * (tileW + spacing), y);

        // Плитка — это группа кнопок, стилизованная под карточку
        ImGui::SetCursorPos(pos);

        // Фон плитки
        ImGui::BeginGroup();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(35, 40, 50, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(60, 70, 90, 255));

        ImGui::BeginChild(("##tile_" + std::to_string(i)).c_str(),
                           ImVec2(tileW, tileH), true,
                           ImGuiWindowFlags_NoScrollbar);

        // Миниатюра (пока placeholder — просто тёмный прямоугольник)
        ImVec2 thumbPos = ImGui::GetCursorScreenPos();
        ImVec2 thumbSize(tileW - 20, 140);
        ImGui::GetWindowDrawList()->AddRectFilled(
            thumbPos,
            ImVec2(thumbPos.x + thumbSize.x, thumbPos.y + thumbSize.y),
            IM_COL32(25, 28, 35, 255), 6.0f);

        // Иконка куба в центре превью
        ImGui::SetCursorPos(ImVec2(tileW * 0.5f - 15, 60));
        ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.7f, 0.6f), ICON_CUBE);

        ImGui::SetCursorPosY(150);

        // Имя проекта
        ImGui::Text("%s", myProjectNames[i].c_str());

        // Путь (или «не сохранён»)
        ImGui::TextDisabled("%s", myProjectPaths[i].empty()
                                     ? "<не сохранён>"
                                     : myProjectPaths[i].c_str());

        // Кнопки на плитке
        ImGui::SetCursorPosY(180);
        if (ImGui::Button("Открыть", ImVec2(100, 30))) {
            SwitchToDoc(i);
            myShowHomeScreen = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_CLOSE " Удалить", ImVec2(100, 30))) {
            // TODO: подтверждение + удаление
        }

        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
        ImGui::EndGroup();

        ImGui::PopID();

        col++;
        if (col >= 4) {  // 4 плитки в строке
            col = 0;
            y += tileH + spacing;
        }
    }

    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleColor();
}

} // namespace BreezeCAD
