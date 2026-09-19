#include "app/Application.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <TDF_ChildIterator.hxx>
#include <TDataStd_Name.hxx>
#include <TDataStd_Integer.hxx>
#include <TCollection_AsciiString.hxx>

#include <cstring>
#include <iostream>
#include <string>

namespace OrdoCAD {

Application::Application() {
    myWindow = std::make_unique<GlfwWindow>(1280, 720, "OrdoCAD - Prototype");
    if (!myWindow->GetNativeWindow()) {
        std::cerr << "Failed to create window\n";
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(myWindow->GetNativeWindow(), true);
    ImGui_ImplOpenGL3_Init("#version 330");

    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(myWindow->GetNativeWindow(), &fbW, &fbH);

    myOcctView = std::make_unique<OcctView>();
    try {
        if (!myOcctView->Init(myWindow->GetNativeWindow(), fbW, fbH)) {
            std::cerr << "Failed to initialize OCCT view\n";
            myOcctView.reset();
            return;
        }
    } catch (const std::exception& e) {
        std::cerr << "OCCT init exception: " << e.what() << "\n";
        myOcctView.reset();
        return;
    } catch (...) {
        std::cerr << "OCCT init unknown exception\n";
        myOcctView.reset();
        return;
    }

    myDocument = std::make_unique<OrdoDocument>();
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

    // Обходим дерево и отображаем формы с сохранением связи метка ↔ AIS
    TDF_Label root = myDocument->MainLabel();
    TDF_ChildIterator it(root, false);
    int count = 0;
    for (; it.More(); it.Next()) {
        TDF_Label child = it.Value();
        TopoDS_Shape s = myDocument->GetShape(child);
        if (!s.IsNull()) {
            myOcctView->DisplayShape(s, child);
            ++count;
        }
    }
    std::cout << "RefreshView: displaying " << count << " shape(s)\n";
    myOcctView->Redraw();
}

// Возвращает true, если был запрошен delete
static bool DrawLabelTree(const TDF_Label& label,
                           const TDF_Label& selectedLabel,
                           TDF_Label& pendingDelete) {
    std::string labelName = "<unnamed>";
    Handle(TDataStd_Name) nameAttr;
    if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
        TCollection_AsciiString ascii(nameAttr->Get());
        labelName = ascii.ToCString();
    }

    int type = -1;
    Handle(TDataStd_Integer) typeAttr;
    if (label.FindAttribute(TDataStd_Integer::GetID(), typeAttr)) {
        type = typeAttr->Get();
    }

    TDF_ChildIterator childIt(label, false);
    bool hasChildren = childIt.More();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                ImGuiTreeNodeFlags_SpanAvailWidth |
                                ImGuiTreeNodeFlags_DefaultOpen;
    if (!hasChildren) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    // Подсветка, если эта метка соответствует выделенному в 3D объекту
    if (!selectedLabel.IsNull() && selectedLabel.IsEqual(label)) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    ImGui::PushID(label.Tag());

    bool opened = ImGui::TreeNodeEx(labelName.c_str(), flags);

    if (ImGui::BeginPopupContextItem("##ctx")) {
        ImGui::TextDisabled("%s", labelName.c_str());
        ImGui::Separator();
        if (ImGui::MenuItem("Delete")) {
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
    const float panelWidth = 280.0f;

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelWidth, io.DisplaySize.y), ImGuiCond_Always);

    ImGui::Begin("##ocaf_tree", nullptr,
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoTitleBar);

    if (ImGui::Button(myWindow->IsFullscreen() ? "Exit Fullscreen (F11)" : "Fullscreen (F11)")) {
        myWindow->ToggleFullscreen();
    }
    ImGui::SameLine();
    if (ImGui::Button("Maximize")) {
        myWindow->Maximize();
    }
    ImGui::Separator();

    ImGui::Text("OCAF Document Tree");
    ImGui::TextDisabled("RMB on node - delete, Delete key - remove selected");
    ImGui::Separator();

    if (ImGui::Button("Add Box")) {
        myShowAddBoxDialog = true;
        std::strncpy(myNewBoxName, "Box", sizeof(myNewBoxName) - 1);
        myNewBoxPos[0] = myNewBoxPos[1] = myNewBoxPos[2] = 0.0f;
        myNewBoxSize[0] = myNewBoxSize[1] = myNewBoxSize[2] = 100.0f;
    }
    ImGui::Separator();

    if (!myDocument || myDocument->GetDoc().IsNull()) {
        ImGui::Text("Document: NULL");
        ImGui::End();
        return;
    }

    TDF_Label selectedLabel;
    if (myOcctView) selectedLabel = myOcctView->GetSelectedLabel();

    TDF_Label root = myDocument->MainLabel();

    if (ImGui::TreeNodeEx("Document (BinOcaf)",
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
        TDF_ChildIterator it(root, false);
        for (; it.More(); it.Next()) {
            DrawLabelTree(it.Value(), selectedLabel, myPendingDeleteLabel);
        }
        ImGui::TreePop();
    }

    ImGui::End();

    // Отложенное удаление
    if (!myPendingDeleteLabel.IsNull() && myDocument) {
        myDocument->RemoveFeature(myPendingDeleteLabel);
        myPendingDeleteLabel = TDF_Label();
        RefreshView();
        if (myOcctView) myOcctView->FitAll();
    }
}

void Application::DrawAddBoxDialog() {
    if (myShowAddBoxDialog) {
        ImGui::OpenPopup("Add Box");
        myShowAddBoxDialog = false;
    }

    if (ImGui::BeginPopupModal("Add Box", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Name", myNewBoxName, sizeof(myNewBoxName));

        ImGui::Separator();
        ImGui::Text("Position (corner):");
        ImGui::InputFloat("X##pos", &myNewBoxPos[0]);
        ImGui::InputFloat("Y##pos", &myNewBoxPos[1]);
        ImGui::InputFloat("Z##pos", &myNewBoxPos[2]);

        ImGui::Separator();
        ImGui::Text("Size:");
        ImGui::InputFloat("DX", &myNewBoxSize[0]);
        ImGui::InputFloat("DY", &myNewBoxSize[1]);
        ImGui::InputFloat("DZ", &myNewBoxSize[2]);

        ImGui::Separator();

        if (ImGui::Button("OK", ImVec2(120, 0))) {
            if (myDocument) {
                myDocument->AddBoxFeature(myNewBoxName,
                                          myNewBoxSize[0], myNewBoxSize[1], myNewBoxSize[2],
                                          myNewBoxPos[0],  myNewBoxPos[1],  myNewBoxPos[2]);
                RefreshView();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Application::Run() {
    bool prevF11    = false;
    bool prevDelete = false;

    while (!myWindow->ShouldClose()) {
        myWindow->PollEvents();

        // F11
        bool curF11 = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_F11) == GLFW_PRESS;
        if (curF11 && !prevF11) myWindow->ToggleFullscreen();
        prevF11 = curF11;

        // Delete
        bool curDelete = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_DELETE) == GLFW_PRESS;
        if (curDelete && !prevDelete && myOcctView && myDocument) {
            TDF_Label sel = myOcctView->GetSelectedLabel();
            if (!sel.IsNull()) {
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
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 340, 10), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(330, 0), ImGuiCond_Always);
        ImGui::Begin("OrdoCAD Debug", nullptr,
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        if (myOcctView) {
            ImGui::Text("OCCT view: active");
            TDF_Label sel = myOcctView->GetSelectedLabel();
            if (!sel.IsNull()) {
                ImGui::Text("Selected: tag=%d", sel.Tag());
            } else {
                ImGui::TextDisabled("Selected: none");
            }
        } else {
            ImGui::Text("OCCT view: NOT initialized");
        }
        ImGui::Separator();
        ImGui::Text("Controls:");
        ImGui::BulletText("LMB - select");
        ImGui::BulletText("RMB - rotate");
        ImGui::BulletText("MMB - pan");
        ImGui::BulletText("Wheel - zoom");
        ImGui::BulletText("Delete - remove selected");
        ImGui::BulletText("F11 - fullscreen");
        ImGui::End();

        ImGui::Render();

        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(myWindow->GetNativeWindow(), &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (myOcctView) {
            myOcctView->Redraw();
        }

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        myWindow->SwapBuffers();
    }
}

} // namespace OrdoCAD
