#include "doc/BreezeApplication.h"

#include <BinDrivers.hxx>
#include <TCollection_ExtendedString.hxx>
#include <PCDM_ReaderStatus.hxx>
#include <TDocStd_FormatVersion.hxx>

#include <iostream>

namespace BreezeCAD {

BreezeApplication& BreezeApplication::Instance() {
    static BreezeApplication instance;
    return instance;
}

BreezeApplication::BreezeApplication() {
    myApp = new TDocStd_Application();
    BinDrivers::DefineFormat(myApp);
    std::cout << "BreezeApplication: initialized, format 'BinOcaf' registered\n";
}

Handle(TDocStd_Document) BreezeApplication::CreateDocument() {
    Handle(TDocStd_Document) doc;
    TCollection_ExtendedString format("BinOcaf");

    myApp->NewDocument(format, doc);
    if (doc.IsNull()) {
        std::cerr << "BreezeApplication: failed to create new document\n";
        return nullptr;
    }
    std::cout << "BreezeApplication: new document created\n";
    return doc;
}

bool BreezeApplication::SaveDocument(const Handle(TDocStd_Document)& doc,
                                    const std::string& path) {
    if (doc.IsNull()) {
        std::cerr << "BreezeApplication: SaveDocument — doc is null\n";
        return false;
    }

    // Используем std::filesystem::path через ExtendedString для кириллицы
    TCollection_ExtendedString extPath(path.c_str());

    try {
        myApp->SaveAs(doc, extPath);
        std::cout << "BreezeApplication: saved to '" << path << "'\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "BreezeApplication: SaveAs failed: " << e.what() << "\n";
        return false;
    } catch (...) {
        std::cerr << "BreezeApplication: SaveAs failed (unknown)\n";
        return false;
    }
}

Handle(TDocStd_Document) BreezeApplication::OpenDocument(const std::string& path) {
    TCollection_ExtendedString extPath(path.c_str());

    Handle(TDocStd_Document) doc;
    try {
        PCDM_ReaderStatus status = myApp->Open(extPath, doc);
        if (status != PCDM_RS_OK || doc.IsNull()) {
            std::cerr << "BreezeApplication: Open failed, status=" << status << "\n";
            return nullptr;
        }
        std::cout << "BreezeApplication: opened '" << path << "'\n";
        return doc;
    } catch (const std::exception& e) {
        std::cerr << "BreezeApplication: Open exception: " << e.what() << "\n";
        return nullptr;
    } catch (...) {
        std::cerr << "BreezeApplication: Open unknown exception\n";
        return nullptr;
    }
}

void BreezeApplication::CloseDocument(const Handle(TDocStd_Document)& doc) {
    if (doc.IsNull()) return;
    try {
        myApp->Close(doc);
    } catch (...) {
        // ignore
    }
}

} // namespace BreezeCAD
