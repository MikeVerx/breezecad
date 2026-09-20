#pragma once

#include <TDocStd_Application.hxx>
#include <TDocStd_Document.hxx>

#include <string>

namespace BreezeCAD {

class BreezeApplication {
public:
    static BreezeApplication& Instance();

    Handle(TDocStd_Application) GetApp() const { return myApp; }

    Handle(TDocStd_Document) CreateDocument();

    // Сохранить документ в файл (.cbf)
    bool SaveDocument(const Handle(TDocStd_Document)& doc, const std::string& path);

    // Загрузить документ из файла. Возвращает null при ошибке.
    Handle(TDocStd_Document) OpenDocument(const std::string& path);

    // Закрыть документ (освободить ресурсы)
    void CloseDocument(const Handle(TDocStd_Document)& doc);

private:
    BreezeApplication();
    ~BreezeApplication() = default;

    BreezeApplication(const BreezeApplication&) = delete;
    BreezeApplication& operator=(const BreezeApplication&) = delete;

    Handle(TDocStd_Application) myApp;
};

} // namespace BreezeCAD
