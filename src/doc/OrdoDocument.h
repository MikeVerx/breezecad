#pragma once

#include <TDocStd_Document.hxx>
#include <TDF_Label.hxx>
#include <TopoDS_Shape.hxx>

#include <string>
#include <vector>

namespace OrdoCAD {

enum FeatureType {
    FT_Origin    = -1,
    FT_Plane     = -2,
    FT_Axis      = -3,
    FT_Box       = 10,
    FT_Cylinder  = 11,
    FT_Hole      = 12,
    FT_Sketch    = 20,
    FT_Other     = 99
};

class OrdoDocument {
public:
    OrdoDocument();
    ~OrdoDocument();

    Handle(TDocStd_Document) GetDoc() const { return myDoc; }
    void SetDocument(const Handle(TDocStd_Document)& doc);

    TDF_Label MainLabel() const;

    void CreateBaseGeometry();
    TDF_Label GetBaseGeometryLabel() const;

    // Пользовательские фичи
    TDF_Label AddBoxFeature(const std::string& name,
                            double dx, double dy, double dz,
                            double px = 0.0, double py = 0.0, double pz = 0.0);

    TDF_Label AddCylinderFeature(const std::string& name,
                                 double radius, double height);

    // Эскиз как дочерний узел плоскости
    TDF_Label AddSketchFeature(const TDF_Label& parentPlane);

    void RemoveFeature(const TDF_Label& label);

    static bool IsSystem(const TDF_Label& label);
    static int  GetType(const TDF_Label& label);

    TopoDS_Shape GetShape(const TDF_Label& label) const;
    std::vector<TopoDS_Shape> GetAllShapes() const;

    void RefreshNextFeatureId();

private:
    Handle(TDocStd_Document) myDoc;
    int myNextFeatureId = 1;
    TDF_Label myBaseLabel;

    void CollectShapes(const TDF_Label& label, std::vector<TopoDS_Shape>& out) const;
};

} // namespace OrdoCAD
