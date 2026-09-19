#include "doc/OrdoDocument.h"
#include "doc/OrdoApplication.h"

#include <TDataStd_Name.hxx>
#include <TDataStd_Integer.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TCollection_AsciiString.hxx>
#include <TDF_ChildIterator.hxx>

#include <TNaming_Builder.hxx>
#include <TNaming_NamedShape.hxx>

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <gp_Lin.hxx>

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

// === Системная геометрия ===

void OrdoDocument::CreateBaseGeometry() {
    if (myDoc.IsNull()) return;

    // Создаём контейнер "Origin" с тегом 1
    myBaseLabel = MainLabel().FindChild(1, true);
    TDataStd_Name::Set(myBaseLabel, TCollection_ExtendedString("Origin"));
    TDataStd_Integer::Set(myBaseLabel, FT_Origin);

    const double planeSize = 100.0;   // полразмера плоскости
    const double axisLen   = 100.0;   // полудлина оси

    // 1. Origin — точка в (0,0,0)
    {
        TDF_Label l = myBaseLabel.FindChild(1, true);
        TDataStd_Name::Set(l, TCollection_ExtendedString("Origin Point"));
        TDataStd_Integer::Set(l, FT_Origin);
        TopoDS_Shape v = BRepBuilderAPI_MakeVertex(gp_Pnt(0, 0, 0)).Shape();
        TNaming_Builder(l).Generated(v);
    }

    // 2. XY Plane (Z = 0)
    {
        TDF_Label l = myBaseLabel.FindChild(2, true);
        TDataStd_Name::Set(l, TCollection_ExtendedString("XY Plane"));
        TDataStd_Integer::Set(l, FT_Plane);
        gp_Pln pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
        TopoDS_Shape f = BRepBuilderAPI_MakeFace(pln,
            -planeSize, planeSize, -planeSize, planeSize).Shape();
        TNaming_Builder(l).Generated(f);
    }

    // 3. YZ Plane (X = 0)
    {
        TDF_Label l = myBaseLabel.FindChild(3, true);
        TDataStd_Name::Set(l, TCollection_ExtendedString("YZ Plane"));
        TDataStd_Integer::Set(l, FT_Plane);
        gp_Pln pln(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0));
        TopoDS_Shape f = BRepBuilderAPI_MakeFace(pln,
            -planeSize, planeSize, -planeSize, planeSize).Shape();
        TNaming_Builder(l).Generated(f);
    }

    // 4. XZ Plane (Y = 0)
    {
        TDF_Label l = myBaseLabel.FindChild(4, true);
        TDataStd_Name::Set(l, TCollection_ExtendedString("XZ Plane"));
        TDataStd_Integer::Set(l, FT_Plane);
        gp_Pln pln(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0));
        TopoDS_Shape f = BRepBuilderAPI_MakeFace(pln,
            -planeSize, planeSize, -planeSize, planeSize).Shape();
        TNaming_Builder(l).Generated(f);
    }

    // 5. X Axis
    {
        TDF_Label l = myBaseLabel.FindChild(5, true);
        TDataStd_Name::Set(l, TCollection_ExtendedString("X Axis"));
        TDataStd_Integer::Set(l, FT_Axis);
        gp_Lin lin(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0));
        TopoDS_Shape e = BRepBuilderAPI_MakeEdge(lin, -axisLen, axisLen).Shape();
        TNaming_Builder(l).Generated(e);
    }

    // 6. Y Axis
    {
        TDF_Label l = myBaseLabel.FindChild(6, true);
        TDataStd_Name::Set(l, TCollection_ExtendedString("Y Axis"));
        TDataStd_Integer::Set(l, FT_Axis);
        gp_Lin lin(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0));
        TopoDS_Shape e = BRepBuilderAPI_MakeEdge(lin, -axisLen, axisLen).Shape();
        TNaming_Builder(l).Generated(e);
    }

    // 7. Z Axis
    {
        TDF_Label l = myBaseLabel.FindChild(7, true);
        TDataStd_Name::Set(l, TCollection_ExtendedString("Z Axis"));
        TDataStd_Integer::Set(l, FT_Axis);
        gp_Lin lin(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
        TopoDS_Shape e = BRepBuilderAPI_MakeEdge(lin, -axisLen, axisLen).Shape();
        TNaming_Builder(l).Generated(e);
    }

    // Сдвигаем счётчик, чтобы пользовательские фичи не попали в диапазон 1..7
    myNextFeatureId = 10;

    std::cout << "OrdoDocument: base geometry created (Origin + 3 planes + 3 axes)\n";
}

TDF_Label OrdoDocument::GetBaseGeometryLabel() const {
    return myBaseLabel;
}

// === Пользовательские фичи ===

TDF_Label OrdoDocument::AddBoxFeature(const std::string& name,
                                       double dx, double dy, double dz,
                                       double px, double py, double pz) {
    if (myDoc.IsNull()) return TDF_Label();

    TDF_Label label = MainLabel().FindChild(myNextFeatureId++, true);

    TDataStd_Name::Set(label, TCollection_ExtendedString(name.c_str()));
    TDataStd_Integer::Set(label, FT_Box);

    gp_Pnt corner(px, py, pz);
    TopoDS_Shape box = BRepPrimAPI_MakeBox(corner, dx, dy, dz).Shape();
    TNaming_Builder(label).Generated(box);

    std::cout << "OrdoDocument: added box '" << name << "'\n";
    return label;
}

TDF_Label OrdoDocument::AddCylinderFeature(const std::string& name,
                                            double radius, double height) {
    if (myDoc.IsNull()) return TDF_Label();

    TDF_Label label = MainLabel().FindChild(myNextFeatureId++, true);
    TDataStd_Name::Set(label, TCollection_ExtendedString(name.c_str()));
    TDataStd_Integer::Set(label, FT_Cylinder);

    TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(radius, height).Shape();
    TNaming_Builder(label).Generated(cyl);

    return label;
}

// === Проверки ===

int OrdoDocument::GetType(const TDF_Label& label) {
    Handle(TDataStd_Integer) attr;
    if (label.FindAttribute(TDataStd_Integer::GetID(), attr)) {
        return attr->Get();
    }
    return FT_Other;
}

bool OrdoDocument::IsSystem(const TDF_Label& label) {
    return GetType(label) < 0;
}

// === Удаление ===

void OrdoDocument::RemoveFeature(const TDF_Label& label) {
    if (label.IsNull()) return;
    if (IsSystem(label)) {
        std::cout << "OrdoDocument: refusing to delete system feature (tag="
                  << label.Tag() << ")\n";
        return;
    }

    std::string name = "<unnamed>";
    Handle(TDataStd_Name) nameAttr;
    if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
        TCollection_AsciiString ascii(nameAttr->Get());
        name = ascii.ToCString();
    }

    label.ForgetAllAttributes(true);
    std::cout << "OrdoDocument: removed feature '" << name << "'\n";
}

// === Чтение форм ===

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
    if (!s.IsNull()) out.push_back(s);

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
    // Пользовательское дерево — пока только куб и цилиндр
    AddBoxFeature("Box", 100.0, 100.0, 100.0, 0.0, 0.0, 0.0);
    AddCylinderFeature("Cylinder", 30.0, 150.0);
}

bool OrdoDocument::Save(const std::string& /*path*/) { return true; }
bool OrdoDocument::Load(const std::string& /*path*/) { return true; }

} // namespace OrdoCAD
