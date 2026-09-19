#include "Application.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
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
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(myWindow->GetNativeWindow(), true);
    ImGui_ImplOpenGL3_Init("#version 330");
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
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                    1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        ImGui::End();

        ImGui::Render();

        int display_w, display_h;
        glfwGetFramebufferSize(myWindow->GetNativeWindow(), &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        myWindow->SwapBuffers();
    }
}

} // namespace OrdoCAD
