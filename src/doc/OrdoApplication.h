#pragma once

#include <TDocStd_Application.hxx>
#include <TDocStd_Document.hxx>

#include <string>

namespace OrdoCAD {

class OrdoApplication {
public:
    static OrdoApplication& Instance();

    Handle(TDocStd_Application) GetApp() const { return myApp; }

    Handle(TDocStd_Document) CreateDocument();

    // Сохранить документ в файл (.cbf)
    bool SaveDocument(const Handle(TDocStd_Document)& doc, const std::string& path);

    // Загрузить документ из файла. Возвращает null при ошибке.
    Handle(TDocStd_Document) OpenDocument(const std::string& path);

    // Закрыть документ (освободить ресурсы)
    void CloseDocument(const Handle(TDocStd_Document)& doc);

private:
    OrdoApplication();
    ~OrdoApplication() = default;

    OrdoApplication(const OrdoApplication&) = delete;
    OrdoApplication& operator=(const OrdoApplication&) = delete;

    Handle(TDocStd_Application) myApp;
};

} // namespace OrdoCAD
