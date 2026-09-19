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

    // Базовая фича: узел с именем и типом, без геометрии
    TDF_Label AddFeature(const std::string& name, int type);

    // Фича с геометрией: куб
    TDF_Label AddBoxFeature(const std::string& name, double dx, double dy, double dz);

    // Фича с геометрией: цилиндр
    TDF_Label AddCylinderFeature(const std::string& name, double radius, double height);

    // Прочитать форму из метки (если есть)
    TopoDS_Shape GetShape(const TDF_Label& label) const;

    // Прочитать все формы из дерева (рекурсивно)
    std::vector<TopoDS_Shape> GetAllShapes() const;

    // Тестовое дерево: куб и цилиндр
    void CreateTestTree();

    bool Save(const std::string& path);
    bool Load(const std::string& path);

private:
    Handle(TDocStd_Document) myDoc;
    int myNextFeatureId = 1;

    void CollectShapes(const TDF_Label& label, std::vector<TopoDS_Shape>& out) const;
};

} // namespace OrdoCAD
