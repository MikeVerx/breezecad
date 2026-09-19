#pragma once

#include <TDocStd_Document.hxx>
#include <TDF_Label.hxx>
#include <TopoDS_Shape.hxx>

#include <string>
#include <vector>

namespace OrdoCAD {

// Типы фич. Отрицательные — системные (нельзя удалить).
enum FeatureType {
    FT_Origin    = -1,
    FT_Plane     = -2,
    FT_Axis      = -3,
    FT_Box       = 10,
    FT_Cylinder  = 11,
    FT_Hole      = 12,
    FT_Other     = 99
};

class OrdoDocument {
public:
    OrdoDocument();
    ~OrdoDocument();

    Handle(TDocStd_Document) GetDoc() const { return myDoc; }
    TDF_Label MainLabel() const;

    // --- Системная геометрия (Origin + 3 плоскости + 3 оси) ---
    void CreateBaseGeometry();
    TDF_Label GetBaseGeometryLabel() const;  // метка-контейнер "Origin"

    // --- Пользовательские фичи ---
    TDF_Label AddBoxFeature(const std::string& name,
                            double dx, double dy, double dz,
                            double px = 0.0, double py = 0.0, double pz = 0.0);

    TDF_Label AddCylinderFeature(const std::string& name,
                                 double radius, double height);

    // Удалить метку (системные не удаляются)
    void RemoveFeature(const TDF_Label& label);

    // Проверки
    static bool IsSystem(const TDF_Label& label);
    static int  GetType(const TDF_Label& label);

    TopoDS_Shape GetShape(const TDF_Label& label) const;
    std::vector<TopoDS_Shape> GetAllShapes() const;

    void CreateTestTree();

    bool Save(const std::string& path);
    bool Load(const std::string& path);

private:
    Handle(TDocStd_Document) myDoc;
    int myNextFeatureId = 1;
    TDF_Label myBaseLabel;   // "Origin" — контейнер системных элементов

    void CollectShapes(const TDF_Label& label, std::vector<TopoDS_Shape>& out) const;
};

} // namespace OrdoCAD
