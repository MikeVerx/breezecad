#include "app/Application.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <iostream>

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

    myWindow->SetMouseButtonCallback([this](int b, int a, int m) {
        if (myOcctView) myOcctView->OnMouseButton(b, a, m);
    });
    myWindow->SetCursorPosCallback([this](double x, double y) {
        if (myOcctView) myOcctView->OnMouseMove(x, y);
    });
    myWindow->SetScrollCallback([this](double x, double y) {
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

void Application::Run() {
    while (!myWindow->ShouldClose()) {
        myWindow->PollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("OrdoCAD Debug");
        ImGui::Text("Hello, OrdoCAD!");
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        if (myOcctView) {
            ImGui::Text("OCCT view: active");
        } else {
            ImGui::Text("OCCT view: NOT initialized");
        }
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
