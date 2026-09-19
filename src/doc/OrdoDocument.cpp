#include "doc/OrdoDocument.h"
#include "doc/OrdoApplication.h"

#include <TDataStd_Name.hxx>
#include <TDataStd_Integer.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TCollection_AsciiString.hxx>
#include <TDF_ChildIterator.hxx>

#include <TNaming_Builder.hxx>
#include <TNaming_NamedShape.hxx>
#include <TNaming_Tool.hxx>

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>

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

    TDF_Label label = MainLabel().FindChild(myNextFeatureId++, true);

    TCollection_ExtendedString extName(name.c_str());
    TDataStd_Name::Set(label, extName);
    TDataStd_Integer::Set(label, type);

    std::cout << "OrdoDocument: added feature '" << name
              << "' (type=" << type << ")\n";
    return label;
}

TDF_Label OrdoDocument::AddBoxFeature(const std::string& name,
                                       double dx, double dy, double dz) {
    if (myDoc.IsNull()) return TDF_Label();

    TDF_Label label = MainLabel().FindChild(myNextFeatureId++, true);

    TDataStd_Name::Set(label, TCollection_ExtendedString(name.c_str()));
    TDataStd_Integer::Set(label, 2);  // type = 2 (Box)

    // Создаём форму и привязываем её к метке через TNaming
    TopoDS_Shape box = BRepPrimAPI_MakeBox(dx, dy, dz).Shape();

    TNaming_Builder builder(label);
    builder.Generated(box);  // фиксируем форму как «порождённую» этой меткой

    std::cout << "OrdoDocument: added box '" << name << "' ("
              << dx << "x" << dy << "x" << dz << ")\n";
    return label;
}

TDF_Label OrdoDocument::AddCylinderFeature(const std::string& name,
                                            double radius, double height) {
    if (myDoc.IsNull()) return TDF_Label();

    TDF_Label label = MainLabel().FindChild(myNextFeatureId++, true);

    TDataStd_Name::Set(label, TCollection_ExtendedString(name.c_str()));
    TDataStd_Integer::Set(label, 3);  // type = 3 (Cylinder)

    TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(radius, height).Shape();

    TNaming_Builder builder(label);
    builder.Generated(cyl);

    std::cout << "OrdoDocument: added cylinder '" << name << "' (r="
              << radius << ", h=" << height << ")\n";
    return label;
}

TopoDS_Shape OrdoDocument::GetShape(const TDF_Label& label) const {
    Handle(TNaming_NamedShape) ns;
    if (label.FindAttribute(TNaming_NamedShape::GetID(), ns) && !ns.IsNull()) {
        return ns->Get();
    }
    return TopoDS_Shape();
}

void OrdoDocument::CollectShapes(const TDF_Label& label,
                                  std::vector<TopoDS_Shape>& out) const {
    TopoDS_Shape s = GetShape(label);
    if (!s.IsNull()) {
        out.push_back(s);
    }

    TDF_ChildIterator it(label, false);
    for (; it.More(); it.Next()) {
        CollectShapes(it.Value(), out);
    }
}

std::vector<TopoDS_Shape> OrdoDocument::GetAllShapes() const {
    std::vector<TopoDS_Shape> shapes;
    if (myDoc.IsNull()) return shapes;
    CollectShapes(MainLabel(), shapes);
    return shapes;
}

void OrdoDocument::CreateTestTree() {
    AddBoxFeature("Box", 100.0, 100.0, 100.0);
    AddCylinderFeature("Cylinder", 30.0, 150.0);
}

bool OrdoDocument::Save(const std::string& /*path*/) {
    return true;
}

bool OrdoDocument::Load(const std::string& /*path*/) {
    return true;
}

} // namespace OrdoCAD
