#include "app/Application.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <TDF_ChildIterator.hxx>
#include <TDataStd_Name.hxx>
#include <TDataStd_Integer.hxx>
#include <TCollection_AsciiString.hxx>

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

    // Создаём документ с тестовым деревом
    myDocument = std::make_unique<OrdoDocument>();
    myDocument->CreateTestTree();

    // Отображаем все формы из документа
    if (myOcctView) {
        auto shapes = myDocument->GetAllShapes();
        std::cout << "OrdoCAD: displaying " << shapes.size()
                  << " shape(s) from document\n";
        for (const auto& s : shapes) {
            myOcctView->DisplayShape(s);
        }
        myOcctView->FitAll();
    }

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

static void DrawLabelTree(const TDF_Label& label) {
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

    bool opened = ImGui::TreeNodeEx(labelName.c_str(), flags);

    ImGui::SameLine();
    ImGui::TextDisabled("[type=%d, tag=%d]", type, label.Tag());

    if (opened && hasChildren) {
        TDF_ChildIterator it(label, false);
        for (; it.More(); it.Next()) {
            DrawLabelTree(it.Value());
        }
        ImGui::TreePop();
    }
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
    ImGui::Separator();

    if (!myDocument || myDocument->GetDoc().IsNull()) {
        ImGui::Text("Document: NULL");
        ImGui::End();
        return;
    }

    TDF_Label root = myDocument->MainLabel();

    if (ImGui::TreeNodeEx("Document (BinOcaf)",
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
        TDF_ChildIterator it(root, false);
        for (; it.More(); it.Next()) {
            DrawLabelTree(it.Value());
        }
        ImGui::TreePop();
    }

    ImGui::End();
}

void Application::Run() {
    bool prevF11 = false;

    while (!myWindow->ShouldClose()) {
        myWindow->PollEvents();

        bool curF11 = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_F11) == GLFW_PRESS;
        if (curF11 && !prevF11) {
            myWindow->ToggleFullscreen();
        }
        prevF11 = curF11;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        DrawDocumentTree();

        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 340, 10), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(330, 0), ImGuiCond_Always);
        ImGui::Begin("OrdoCAD Debug", nullptr,
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        if (myOcctView) {
            ImGui::Text("OCCT view: active");
        } else {
            ImGui::Text("OCCT view: NOT initialized");
        }
        ImGui::Separator();
        ImGui::Text("Controls:");
        ImGui::BulletText("LMB - rotate");
        ImGui::BulletText("MMB - pan");
        ImGui::BulletText("Wheel - zoom");
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
