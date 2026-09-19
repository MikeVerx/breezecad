#pragma once

#include <TDocStd_Application.hxx>
#include <TDocStd_Document.hxx>

namespace OrdoCAD {

// Обёртка над TDocStd_Application.
// Одна на всё приложение — создаёт документы, регистрирует драйверы сохранения.
class OrdoApplication {
public:
    static OrdoApplication& Instance();

    Handle(TDocStd_Application) GetApp() const { return myApp; }

    // Создать новый пустой документ
    Handle(TDocStd_Document) CreateDocument();

private:
    OrdoApplication();
    ~OrdoApplication() = default;

    OrdoApplication(const OrdoApplication&) = delete;
    OrdoApplication& operator=(const OrdoApplication&) = delete;

    Handle(TDocStd_Application) myApp;
};

} // namespace OrdoCAD
