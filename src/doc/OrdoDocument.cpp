#include "doc/OrdoDocument.h"
#include "doc/OrdoApplication.h"

#include <TDataStd_Name.hxx>
#include <TDataStd_Integer.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TCollection_AsciiString.hxx>

#include <iostream>

namespace OrdoCAD {

OrdoDocument::OrdoDocument() {
    myDoc = OrdoApplication::Instance().CreateDocument();
}

OrdoDocument::~OrdoDocument() = default;

TDF_Label OrdoDocument::MainLabel() const {
    if (myDoc.IsNull()) return TDF_Label();
    return myDoc->Main();
}

TDF_Label OrdoDocument::AddFeature(const std::string& name, int type) {
    if (myDoc.IsNull()) return TDF_Label();

    // Каждая фича — отдельная метка под корнем.
    // Тег — уникальный номер фичи.
    TDF_Label label = MainLabel().FindChild(myNextFeatureId++, Standard_True);

    // Имя фичи
    TCollection_ExtendedString extName(name.c_str());
    TDataStd_Name::Set(label, extName);

    // Тип фичи (целое число)
    TDataStd_Integer::Set(label, type);

    std::cout << "OrdoDocument: added feature '" << name
              << "' (type=" << type << ")\n";
    return label;
}

void OrdoDocument::CreateTestTree() {
    // Тип 1 = "Эскиз", 2 = "Выдавливание", 3 = "Отверстие" — пока условно.
    AddFeature("Box",       2);
    AddFeature("Cylinder",  2);
    AddFeature("Hole",      3);
}

bool OrdoDocument::Save(const std::string& /*path*/) {
    // TODO: реализуем на следующем шаге
    return true;
}

bool OrdoDocument::Load(const std::string& /*path*/) {
    // TODO: реализуем на следующем шаге
    return true;
}

} // namespace OrdoCAD
