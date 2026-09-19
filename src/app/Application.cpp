#include "app/Application.h"
#include "app/Localization.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <TDF_ChildIterator.hxx>
#include <TDataStd_Name.hxx>
#include <TDataStd_Integer.hxx>
#include <TCollection_AsciiString.hxx>

#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace OrdoCAD {

static void LoadCyrillicFont() {
    ImGuiIO& io = ImGui::GetIO();
    const std::vector<std::string> candidates = {
        "assets/fonts/Roboto-Regular.ttf",
        "../assets/fonts/Roboto-Regular.ttf",
        "../../assets/fonts/Roboto-Regular.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/tahoma.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };
    for (const auto& path : candidates) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) continue;
        ImFont* font = io.Fonts->AddFontFromFileTTF(
            path.c_str(), 18.0f, nullptr,
            io.Fonts->GetGlyphRangesCyrillic());
        if (font) {
            std::cout << "OrdoCAD: loaded font '" << path << "'\n";
            return;
        }
    }
    std::cerr << "OrdoCAD: no Cyrillic font found\n";
    io.Fonts->AddFontDefault();
}

Application::Application() {
    myWindow = std::make_unique<GlfwWindow>(1280, 720, "OrdoCAD - Prototype");
    if (!myWindow->GetNativeWindow()) return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    LoadCyrillicFont();
    ImGui_ImplGlfw_InitForOpenGL(myWindow->GetNativeWindow(), true);
    ImGui_ImplOpenGL3_Init("#version 330");

    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(myWindow->GetNativeWindow(), &fbW, &fbH);

    myOcctView = std::make_unique<OcctView>();
    try {
        if (!myOcctView->Init(myWindow->GetNativeWindow(), fbW, fbH)) {
            myOcctView.reset();
            return;
        }
    } catch (...) { myOcctView.reset(); return; }

    myDocument = std::make_unique<OrdoDocument>();
    myDocument->CreateBaseGeometry();
    myDocument->CreateTestTree();

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

    TDF_Label root = myDocument->MainLabel();
    TDF_ChildIterator it(root, false);
    int count = 0;
    for (; it.More(); it.Next()) {
        TDF_Label child = it.Value();
        TopoDS_Shape s = myDocument->GetShape(child);
        if (!s.IsNull()) {
            bool isSys = OrdoDocument::IsSystem(child);
            myOcctView->DisplayShape(s, child, isSys);
            ++count;
        }
        // Дети базовой геометрии — плоскости, оси, точка
        if (OrdoDocument::GetType(child) == FT_Origin) {
            TDF_ChildIterator subIt(child, false);
            for (; subIt.More(); subIt.Next()) {
                TDF_Label sub = subIt.Value();
                TopoDS_Shape ss = myDocument->GetShape(sub);
                if (!ss.IsNull()) {
                    myOcctView->DisplayShape(ss, sub, true);
                    ++count;
                }
            }
        }
    }
    std::cout << "RefreshView: " << count << " shape(s)\n";
    myOcctView->Redraw();
    myOcctView->Redraw();
}

// Отрисовка одного узла дерева. Возвращает true, если запрошено удаление.
static bool DrawLabelTree(const TDF_Label& label,
                           const TDF_Label& selectedLabel,
                           TDF_Label& pendingDelete) {
    std::string labelName = "<unnamed>";
    Handle(TDataStd_Name) nameAttr;
    if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
        TCollection_AsciiString ascii(nameAttr->Get());
        labelName = ascii.ToCString();
    }

    int type = OrdoDocument::GetType(label);
    bool isSystem = (type < 0);

    TDF_ChildIterator childIt(label, false);
    bool hasChildren = childIt.More();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                ImGuiTreeNodeFlags_SpanAvailWidth |
                                ImGuiTreeNodeFlags_DefaultOpen;
    if (!hasChildren) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (!selectedLabel.IsNull() && selectedLabel.IsEqual(label)) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    ImGui::PushID(label.Tag());

    // Системные — серым цветом
    if (isSystem) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(160, 160, 160, 255));
    bool opened = ImGui::TreeNodeEx(labelName.c_str(), flags);
    if (isSystem) ImGui::PopStyleColor();

    // Контекстное меню — только для не-системных
    if (!isSystem && ImGui::BeginPopupContextItem("##ctx")) {
        ImGui::TextDisabled("%s", labelName.c_str());
        ImGui::Separator();
        if (ImGui::MenuItem(Tr("btn.delete").c_str())) {
            pendingDelete = label;
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("[type=%d, tag=%d]", type, label.Tag());

    bool requested = false;
    if (opened && hasChildren) {
        TDF_ChildIterator it(label, false);
        for (; it.More(); it.Next()) {
            if (DrawLabelTree(it.Value(), selectedLabel, pendingDelete)) {
                requested = true;
            }
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
    return requested || !pendingDelete.IsNull();
}

void Application::DrawDocumentTree() {
    ImGuiIO& io = ImGui::GetIO();
    const float panelWidth = 320.0f;

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelWidth, io.DisplaySize.y), ImGuiCond_Always);

    ImGui::Begin("##ocaf_tree", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoTitleBar);

    ImGui::Text("%s", Tr("lang.label").c_str());
    ImGui::SameLine();
    Language curLang = Localization::Instance().GetLanguage();
    if (ImGui::RadioButton(Tr("lang.en").c_str(), curLang == Language::EN))
        Localization::Instance().SetLanguage(Language::EN);
    ImGui::SameLine();
    if (ImGui::RadioButton(Tr("lang.ru").c_str(), curLang == Language::RU))
        Localization::Instance().SetLanguage(Language::RU);
    ImGui::Separator();

    if (ImGui::Button(myWindow->IsFullscreen()
                        ? Tr("btn.exitFullscreen").c_str()
                        : Tr("btn.fullscreen").c_str()))
        myWindow->ToggleFullscreen();
    ImGui::SameLine();
    if (ImGui::Button(Tr("btn.maximize").c_str()))
        myWindow->Maximize();
    ImGui::Separator();

    ImGui::Text("%s", Tr("panel.title").c_str());
    ImGui::TextDisabled("%s", Tr("panel.hint.delete").c_str());
    ImGui::Separator();

    if (ImGui::Button(Tr("btn.addBox").c_str())) {
        myShowAddBoxDialog = true;
        std::strncpy(myNewBoxName, Tr("default.boxName").c_str(), sizeof(myNewBoxName) - 1);
        myNewBoxName[sizeof(myNewBoxName) - 1] = '\0';
        myNewBoxPos[0] = myNewBoxPos[1] = myNewBoxPos[2] = 0.0f;
        myNewBoxSize[0] = myNewBoxSize[1] = myNewBoxSize[2] = 100.0f;
    }
    ImGui::Separator();

    if (!myDocument || myDocument->GetDoc().IsNull()) {
        ImGui::Text("%s", Tr("panel.document.null").c_str());
        ImGui::End();
        return;
    }

    TDF_Label selectedLabel;
    if (myOcctView) selectedLabel = myOcctView->GetSelectedLabel();

    TDF_Label root = myDocument->MainLabel();

    if (ImGui::TreeNodeEx(Tr("panel.document").c_str(),
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
        // Сначала — базовые элементы (Origin), потом пользовательские
        TDF_Label baseLabel = myDocument->GetBaseGeometryLabel();

        // Origin group
        if (!baseLabel.IsNull()) {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(160, 160, 160, 255));
            bool opened = ImGui::TreeNodeEx("Origin",
                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
            ImGui::PopStyleColor();

            if (opened) {
                TDF_ChildIterator it(baseLabel, false);
                for (; it.More(); it.Next()) {
                    DrawLabelTree(it.Value(), selectedLabel, myPendingDeleteLabel);
                }
                ImGui::TreePop();
            }
        }

        // Пользовательские фичи
        TDF_ChildIterator it(root, false);
        for (; it.More(); it.Next()) {
            TDF_Label child = it.Value();
            if (child.IsEqual(baseLabel)) continue;
            DrawLabelTree(child, selectedLabel, myPendingDeleteLabel);
        }
        ImGui::TreePop();
    }

    ImGui::End();

    if (!myPendingDeleteLabel.IsNull() && myDocument) {
        myDocument->RemoveFeature(myPendingDeleteLabel);
        myPendingDeleteLabel = TDF_Label();
        myHasPendingDelete = false;
        RefreshView();
        if (myOcctView) myOcctView->FitAll();
    }
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
        if (ImGui::Button(Tr("btn.cancel").c_str(), ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Application::Run() {
    bool prevF11 = false;
    bool prevDelete = false;

    while (!myWindow->ShouldClose()) {
        myWindow->PollEvents();

        bool curF11 = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_F11) == GLFW_PRESS;
        if (curF11 && !prevF11) myWindow->ToggleFullscreen();
        prevF11 = curF11;

        bool curDelete = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_DELETE) == GLFW_PRESS;
        if (curDelete && !prevDelete && myOcctView && myDocument) {
            TDF_Label sel = myOcctView->GetSelectedLabel();
            if (!sel.IsNull() && !OrdoDocument::IsSystem(sel)) {
                myDocument->RemoveFeature(sel);
                myOcctView->ClearSelection();
                RefreshView();
                myOcctView->FitAll();
            }
        }
        prevDelete = curDelete;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        DrawDocumentTree();
        DrawAddBoxDialog();

        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 360, 10), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(350, 0), ImGuiCond_Always);
        ImGui::Begin(Tr("debug.title").c_str(), nullptr,
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        if (myOcctView) {
            ImGui::Text("%s", Tr("debug.view.active").c_str());
            TDF_Label sel = myOcctView->GetSelectedLabel();
            if (!sel.IsNull()) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), Tr("debug.selected.tag").c_str(), sel.Tag());
                ImGui::Text("%s", buf);
            } else {
                ImGui::TextDisabled("%s", Tr("debug.selected.none").c_str());
            }
        } else {
            ImGui::Text("%s", Tr("debug.view.inactive").c_str());
        }
        ImGui::Separator();
        ImGui::Text("%s", Tr("debug.controls").c_str());
        ImGui::BulletText("%s", Tr("debug.lmb").c_str());
        ImGui::BulletText("%s", Tr("debug.rmb").c_str());
        ImGui::BulletText("%s", Tr("debug.mmb").c_str());
        ImGui::BulletText("%s", Tr("debug.wheel").c_str());
        ImGui::BulletText("%s", Tr("debug.delete").c_str());
        ImGui::BulletText("%s", Tr("debug.f11").c_str());
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
    }
}

} // namespace OrdoCAD
