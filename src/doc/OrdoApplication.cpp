#include "doc/OrdoApplication.h"

#include <BinDrivers.hxx>
#include <TCollection_ExtendedString.hxx>
#include <PCDM_ReaderStatus.hxx>
#include <TDocStd_FormatVersion.hxx>

#include <iostream>

namespace OrdoCAD {

OrdoApplication& OrdoApplication::Instance() {
    static OrdoApplication instance;
    return instance;
}

OrdoApplication::OrdoApplication() {
    myApp = new TDocStd_Application();
    BinDrivers::DefineFormat(myApp);
    std::cout << "OrdoApplication: initialized, format 'BinOcaf' registered\n";
}

Handle(TDocStd_Document) OrdoApplication::CreateDocument() {
    Handle(TDocStd_Document) doc;
    TCollection_ExtendedString format("BinOcaf");

    myApp->NewDocument(format, doc);
    if (doc.IsNull()) {
        std::cerr << "OrdoApplication: failed to create new document\n";
        return nullptr;
    }
    std::cout << "OrdoApplication: new document created\n";
    return doc;
}

bool OrdoApplication::SaveDocument(const Handle(TDocStd_Document)& doc,
                                    const std::string& path) {
    if (doc.IsNull()) {
        std::cerr << "OrdoApplication: SaveDocument — doc is null\n";
        return false;
    }

    // Используем std::filesystem::path через ExtendedString для кириллицы
    TCollection_ExtendedString extPath(path.c_str());

    try {
        myApp->SaveAs(doc, extPath);
        std::cout << "OrdoApplication: saved to '" << path << "'\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "OrdoApplication: SaveAs failed: " << e.what() << "\n";
        return false;
    } catch (...) {
        std::cerr << "OrdoApplication: SaveAs failed (unknown)\n";
        return false;
    }
}

Handle(TDocStd_Document) OrdoApplication::OpenDocument(const std::string& path) {
    TCollection_ExtendedString extPath(path.c_str());

    Handle(TDocStd_Document) doc;
    try {
        PCDM_ReaderStatus status = myApp->Open(extPath, doc);
        if (status != PCDM_RS_OK || doc.IsNull()) {
            std::cerr << "OrdoApplication: Open failed, status=" << status << "\n";
            return nullptr;
        }
        std::cout << "OrdoApplication: opened '" << path << "'\n";
        return doc;
    } catch (const std::exception& e) {
        std::cerr << "OrdoApplication: Open exception: " << e.what() << "\n";
        return nullptr;
    } catch (...) {
        std::cerr << "OrdoApplication: Open unknown exception\n";
        return nullptr;
    }
}

void OrdoApplication::CloseDocument(const Handle(TDocStd_Document)& doc) {
    if (doc.IsNull()) return;
    try {
        myApp->Close(doc);
    } catch (...) {
        // ignore
    }
}

} // namespace OrdoCAD
