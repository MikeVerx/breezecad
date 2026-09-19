#pragma once

#include <TDocStd_Document.hxx>
#include <TDF_Label.hxx>

#include <string>

namespace OrdoCAD {

// Обёртка над TDocStd_Document.
// Хранит документ OCAF: дерево меток (TDF_Label) с атрибутами.
class OrdoDocument {
public:
    OrdoDocument();
    ~OrdoDocument();

    Handle(TDocStd_Document) GetDoc() const { return myDoc; }

    // Корневая метка документа
    TDF_Label MainLabel() const;

    // Добавить простой узел в дерево с именем и типом (пока без геометрии).
    // Возвращает метку созданного узла.
    TDF_Label AddFeature(const std::string& name, int type);

    // Загрузить тестовые данные — куб и цилиндр как узлы дерева.
    void CreateTestTree();

    // Сохранить/загрузить документ
    bool Save(const std::string& path);
    bool Load(const std::string& path);

private:
    Handle(TDocStd_Document) myDoc;
    int myNextFeatureId = 1;
};

} // namespace OrdoCAD
