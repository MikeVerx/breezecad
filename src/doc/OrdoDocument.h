#pragma once

#include <TDocStd_Document.hxx>
#include <TDF_Label.hxx>
#include <TopoDS_Shape.hxx>

#include <string>
#include <vector>

namespace OrdoCAD {

class OrdoDocument {
public:
    OrdoDocument();
    ~OrdoDocument();

    Handle(TDocStd_Document) GetDoc() const { return myDoc; }
    TDF_Label MainLabel() const;

    TDF_Label AddFeature(const std::string& name, int type);

    // Куб с позицией и размерами
    TDF_Label AddBoxFeature(const std::string& name,
                            double dx, double dy, double dz,
                            double px = 0.0, double py = 0.0, double pz = 0.0);

    TDF_Label AddCylinderFeature(const std::string& name,
                                 double radius, double height);

    TopoDS_Shape GetShape(const TDF_Label& label) const;
    std::vector<TopoDS_Shape> GetAllShapes() const;

    void CreateTestTree();

    bool Save(const std::string& path);
    bool Load(const std::string& path);

private:
    Handle(TDocStd_Document) myDoc;
    int myNextFeatureId = 1;

    void CollectShapes(const TDF_Label& label, std::vector<TopoDS_Shape>& out) const;
};

} // namespace OrdoCAD
