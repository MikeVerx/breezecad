#pragma once

#include "platform/glfw/GlfwWindow.h"
#include "occt/OcctView.h"
#include "doc/OrdoDocument.h"

#include <TDF_Label.hxx>

#include <memory>

namespace OrdoCAD {

class Application {
public:
    Application();
    ~Application();

    void Run();

private:
    std::unique_ptr<GlfwWindow>   myWindow;
    std::unique_ptr<OcctView>     myOcctView;
    std::unique_ptr<OrdoDocument> myDocument;

    // Состояние диалога «Add Box»
    bool  myShowAddBoxDialog = false;
    char  myNewBoxName[128]  = "Box";
    float myNewBoxPos[3]     = { 0.0f, 0.0f, 0.0f };
    float myNewBoxSize[3]    = { 100.0f, 100.0f, 100.0f };

    // Отложенное удаление: помечаем метку во время обхода дерева,
    // удаляем после обхода (иначе сломаем итератор).
    TDF_Label myPendingDeleteLabel;
    bool      myHasPendingDelete = false;

    void DrawDocumentTree();
    void DrawAddBoxDialog();
    void RefreshView();
};

} // namespace OrdoCAD
