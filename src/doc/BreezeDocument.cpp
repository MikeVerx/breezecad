#include "doc/BreezeDocument.h"
#include "doc/BreezeApplication.h"

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

namespace BreezeCAD {

BreezeDocument::BreezeDocument() {
    myDoc = BreezeApplication::Instance().CreateDocument();
    if (!myDoc.IsNull()) {
        myDoc->SetUndoLimit(20);
    }
}

BreezeDocument::~BreezeDocument() = default;

void BreezeDocument::SetDocument(const Handle(TDocStd_Document)& doc) {
    myDoc = doc;
    myBaseLabel = TDF_Label();
    RefreshNextFeatureId();

    // Найти существующий Origin (тег 1)
    if (!myDoc.IsNull()) {
        TDF_Label candidate = myDoc->Main().FindChild(1, false);
        if (!candidate.IsNull()) {
            myBaseLabel = candidate;
        }
    }
}

TDF_Label BreezeDocument::MainLabel() const {
    if (myDoc.IsNull()) return TDF_Label();
    return myDoc->Main();
}

bool BreezeDocument::Has3DOperations() const {
    if (myDoc.IsNull()) return false;
    TDF_ChildIterator it(MainLabel(), false);
    for (; it.More(); it.Next()) {
        int type = GetType(it.Value());
        if (type == FT_Box || type == FT_Cylinder || type == FT_Hole) {
            return true;
        }
    }
    return false;
}

void BreezeDocument::RefreshNextFeatureId() {
    if (myDoc.IsNull()) return;
    int maxTag = 0;
    TDF_ChildIterator it(myDoc->Main(), false);
    for (; it.More(); it.Next()) {
        if (it.Value().Tag() > maxTag) maxTag = it.Value().Tag();
    }
    myNextFeatureId = maxTag + 1;
    std::cout << "BreezeDocument: next feature id = " << myNextFeatureId << "\n";
}

// === Системная геометрия ===

void BreezeDocument::CreateBaseGeometry() {
    if (myDoc.IsNull()) return;

    myBaseLabel = MainLabel().FindChild(1, true);
    TDataStd_Name::Set(myBaseLabel, TCollection_ExtendedString("Start"));
    TDataStd_Integer::Set(myBaseLabel, FT_Start);

    const double planeSize = 100.0;
    const double axisLen   = 100.0;

    // Origin Point
    {
        TDF_Label l = myBaseLabel.FindChild(1, true);
        TDataStd_Name::Set(l, TCollection_ExtendedString("Origin Point"));
        TDataStd_Integer::Set(l, FT_OriginPoint);
        TopoDS_Shape v = BRepBuilderAPI_MakeVertex(gp_Pnt(0, 0, 0)).Shape();
        TNaming_Builder(l).Generated(v);
    }
    // XY Plane
    {
        TDF_Label l = myBaseLabel.FindChild(2, true);
        TDataStd_Name::Set(l, TCollection_ExtendedString("XY Plane"));
        TDataStd_Integer::Set(l, FT_Plane);
        gp_Pln pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
        TopoDS_Shape f = BRepBuilderAPI_MakeFace(pln,
            -planeSize, planeSize, -planeSize, planeSize).Shape();
        TNaming_Builder(l).Generated(f);
    }
    // YZ Plane
    {
        TDF_Label l = myBaseLabel.FindChild(3, true);
        TDataStd_Name::Set(l, TCollection_ExtendedString("YZ Plane"));
        TDataStd_Integer::Set(l, FT_Plane);
        gp_Pln pln(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0));
        TopoDS_Shape f = BRepBuilderAPI_MakeFace(pln,
            -planeSize, planeSize, -planeSize, planeSize).Shape();
        TNaming_Builder(l).Generated(f);
    }
    // XZ Plane
    {
        TDF_Label l = myBaseLabel.FindChild(4, true);
        TDataStd_Name::Set(l, TCollection_ExtendedString("XZ Plane"));
        TDataStd_Integer::Set(l, FT_Plane);
        gp_Pln pln(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0));
        TopoDS_Shape f = BRepBuilderAPI_MakeFace(pln,
            -planeSize, planeSize, -planeSize, planeSize).Shape();
        TNaming_Builder(l).Generated(f);
    }
    // X Axis
    {
        TDF_Label l = myBaseLabel.FindChild(5, true);
        TDataStd_Name::Set(l, TCollection_ExtendedString("X Axis"));
        TDataStd_Integer::Set(l, FT_Axis);
        gp_Lin lin(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0));
        TopoDS_Shape e = BRepBuilderAPI_MakeEdge(lin, -axisLen, axisLen).Shape();
        TNaming_Builder(l).Generated(e);
    }
    // Y Axis
    {
        TDF_Label l = myBaseLabel.FindChild(6, true);
        TDataStd_Name::Set(l, TCollection_ExtendedString("Y Axis"));
        TDataStd_Integer::Set(l, FT_Axis);
        gp_Lin lin(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0));
        TopoDS_Shape e = BRepBuilderAPI_MakeEdge(lin, -axisLen, axisLen).Shape();
        TNaming_Builder(l).Generated(e);
    }
    // Z Axis
    {
        TDF_Label l = myBaseLabel.FindChild(7, true);
        TDataStd_Name::Set(l, TCollection_ExtendedString("Z Axis"));
        TDataStd_Integer::Set(l, FT_Axis);
        gp_Lin lin(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
        TopoDS_Shape e = BRepBuilderAPI_MakeEdge(lin, -axisLen, axisLen).Shape();
        TNaming_Builder(l).Generated(e);
    }

    myNextFeatureId = 10;
    std::cout << "BreezeDocument: base geometry created\n";
}

TDF_Label BreezeDocument::GetBaseGeometryLabel() const {
    return myBaseLabel;
}

// === Пользовательские фичи ===

void BreezeDocument::StartTransaction() {
    if (myDoc.IsNull()) return;
    myDoc->OpenCommand();
}

void BreezeDocument::CommitTransaction() {
    if (myDoc.IsNull()) return;
    myDoc->CommitCommand();
}

bool BreezeDocument::Undo() {
    if (myDoc.IsNull()) return false;
    return myDoc->Undo() == Standard_True;
}

bool BreezeDocument::Redo() {
    if (myDoc.IsNull()) return false;
    return myDoc->Redo() == Standard_True;
}

int BreezeDocument::GetUndoLimit() const {
    if (myDoc.IsNull()) return 0;
    return myDoc->GetUndoLimit();
}

void BreezeDocument::SetUndoLimit(int n) {
    if (myDoc.IsNull()) return;
    myDoc->SetUndoLimit(n);
}

TDF_Label BreezeDocument::AddBoxFeature(const std::string& name,
                                       double dx, double dy, double dz,
                                       double px, double py, double pz) {
    if (myDoc.IsNull()) return TDF_Label();

    StartTransaction();

    TDF_Label label = MainLabel().FindChild(myNextFeatureId++, true);
    TDataStd_Name::Set(label, TCollection_ExtendedString(name.c_str()));
    TDataStd_Integer::Set(label, FT_Box);

    gp_Pnt corner(px, py, pz);
    TopoDS_Shape box = BRepPrimAPI_MakeBox(corner, dx, dy, dz).Shape();
    TNaming_Builder(label).Generated(box);

    CommitTransaction();

    std::cout << "BreezeDocument: added box '" << name << "'\n";
    return label;
}

TDF_Label BreezeDocument::AddCylinderFeature(const std::string& name,
                                            double radius, double height) {
    if (myDoc.IsNull()) return TDF_Label();

    TDF_Label label = MainLabel().FindChild(myNextFeatureId++, true);
    TDataStd_Name::Set(label, TCollection_ExtendedString(name.c_str()));
    TDataStd_Integer::Set(label, FT_Cylinder);

    TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(radius, height).Shape();
    TNaming_Builder(label).Generated(cyl);

    return label;
}

TDF_Label BreezeDocument::AddSketchFeature(const TDF_Label& parentPlane) {
    if (myDoc.IsNull() || parentPlane.IsNull()) return TDF_Label();

    // Находим свободный тег среди детей плоскости
    int tag = 1;
    while (!parentPlane.FindChild(tag, false).IsNull()) tag++;

    // Имя эскиза — Sketch1, Sketch2... по количеству уже имеющихся
    int count = 0;
    TDF_ChildIterator it(parentPlane, false);
    for (; it.More(); it.Next()) count++;
    std::string name = "Sketch" + std::to_string(count + 1);

    TDF_Label label = parentPlane.FindChild(tag, true);
    TDataStd_Name::Set(label, TCollection_ExtendedString(name.c_str()));
    TDataStd_Integer::Set(label, FT_Sketch);

    std::cout << "BreezeDocument: added sketch '" << name
              << "' under plane tag=" << parentPlane.Tag() << "\n";
    return label;
}


int BreezeDocument::GetType(const TDF_Label& label) {
    Handle(TDataStd_Integer) attr;
    if (label.FindAttribute(TDataStd_Integer::GetID(), attr)) {
        return attr->Get();
    }
    return FT_Other;
}

bool BreezeDocument::IsSystem(const TDF_Label& label) {
    return GetType(label) < 0;
}

TDF_Label BreezeDocument::AddSketchLine(const TDF_Label& sketchLabel,
                                      const gp_Pnt& p1, const gp_Pnt& p2,
                                      LineStyle style) {
    if (myDoc.IsNull() || sketchLabel.IsNull()) return TDF_Label();

    try {
        StartTransaction();

        int tag = 1;
        while (!sketchLabel.FindChild(tag, false).IsNull()) tag++;

        TDF_Label lineLabel = sketchLabel.FindChild(tag, true);
        TDataStd_Integer::Set(lineLabel, FT_SketchLine);

        TDF_Label styleLabel = lineLabel.FindChild(100, true);
        TDataStd_Integer::Set(styleLabel, static_cast<int>(style));

        TopoDS_Shape edge = BRepBuilderAPI_MakeEdge(p1, p2).Shape();
        if (edge.IsNull()) {
            std::cerr << "AddSketchLine: edge is null\n";
            CommitTransaction();
            return TDF_Label();
        }
        TNaming_Builder(lineLabel).Generated(edge);

        CommitTransaction();
        return lineLabel;
    } catch (const std::exception& e) {
        std::cerr << "AddSketchLine exception: " << e.what() << "\n";
        return TDF_Label();
    } catch (...) {
        std::cerr << "AddSketchLine unknown exception\n";
        return TDF_Label();
    }
}

LineStyle BreezeDocument::GetLineStyle(const TDF_Label& lineLabel) {
    if (lineLabel.IsNull()) return LS_Normal;
    TDF_Label styleLabel = lineLabel.FindChild(100, false);
    if (styleLabel.IsNull()) return LS_Normal;
    Handle(TDataStd_Integer) attr;
    if (styleLabel.FindAttribute(TDataStd_Integer::GetID(), attr)) {
        int v = attr->Get();
        if (v >= 0 && v <= 2) return static_cast<LineStyle>(v);
    }
    return LS_Normal;
}

void BreezeDocument::RemoveLastSketchLine(const TDF_Label& sketchLabel) {
    if (myDoc.IsNull() || sketchLabel.IsNull()) return;

    int maxTag = 0;
    TDF_ChildIterator it(sketchLabel, false);
    for (; it.More(); it.Next()) {
        if (it.Value().Tag() > maxTag) maxTag = it.Value().Tag();
    }
    if (maxTag == 0) return;

    TDF_Label last = sketchLabel.FindChild(maxTag, false);
    if (!last.IsNull()) {
        last.ForgetAllAttributes(true);
        std::cout << "BreezeDocument: removed last sketch line (tag=" << maxTag << ")\n";
    }
}

void BreezeDocument::ClearSketch(const TDF_Label& sketchLabel) {
    if (myDoc.IsNull() || sketchLabel.IsNull()) return;

    std::vector<TDF_Label> toRemove;
    TDF_ChildIterator it(sketchLabel, false);
    for (; it.More(); it.Next()) {
        toRemove.push_back(it.Value());
    }
    for (auto& l : toRemove) {
        l.ForgetAllAttributes(true);
    }
    std::cout << "BreezeDocument: cleared sketch\n";
}

void BreezeDocument::RemoveFeature(const TDF_Label& label) {
    if (label.IsNull()) return;
    if (IsSystem(label)) {
        std::cout << "BreezeDocument: refusing to delete system feature\n";
        return;
    }

    std::string name = "<unnamed>";
    Handle(TDataStd_Name) nameAttr;
    if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
        TCollection_AsciiString ascii(nameAttr->Get());
        name = ascii.ToCString();
    }

    StartTransaction();
    label.ForgetAllAttributes(true);
    CommitTransaction();

    std::cout << "BreezeDocument: removed feature '" << name << "'\n";
}

TopoDS_Shape BreezeDocument::GetShape(const TDF_Label& label) const {
    Handle(TNaming_NamedShape) ns;
    if (label.FindAttribute(TNaming_NamedShape::GetID(), ns) && !ns.IsNull()) {
        return ns->Get();
    }
    return TopoDS_Shape();
}

void BreezeDocument::CollectShapes(const TDF_Label& label,
                                  std::vector<TopoDS_Shape>& out) const {
    TopoDS_Shape s = GetShape(label);
    if (!s.IsNull()) out.push_back(s);

    TDF_ChildIterator it(label, false);
    for (; it.More(); it.Next()) {
        CollectShapes(it.Value(), out);
    }
}

std::vector<TopoDS_Shape> BreezeDocument::GetAllShapes() const {
    std::vector<TopoDS_Shape> shapes;
    if (myDoc.IsNull()) return shapes;
    CollectShapes(MainLabel(), shapes);
    return shapes;
}

} // namespace BreezeCAD
