#include "doc/OrdoApplication.h"

#include <BinDrivers.hxx>
#include <TCollection_ExtendedString.hxx>

#include <iostream>

namespace OrdoCAD {

OrdoApplication& OrdoApplication::Instance() {
    static OrdoApplication instance;
    return instance;
}

OrdoApplication::OrdoApplication() {
    myApp = new TDocStd_Application();

    // Регистрируем бинарный драйвер для формата BinOcaf
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

} // namespace OrdoCAD
