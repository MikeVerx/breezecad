#pragma once

#include <TDocStd_Document.hxx>
#include <TDF_Label.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>

#include <string>
#include <vector>

namespace BreezeCAD {

enum FeatureType {
    FT_Start       = -1,   // папка Start (корневой контейнер базовой геометрии)
    FT_Plane       = -2,
    FT_Axis        = -3,
    FT_OriginPoint = -4,   // точка (0,0,0) — не показывается в дереве
    FT_Box         = 10,
    FT_Cylinder    = 11,
    FT_Hole        = 12,
    FT_Sketch      = 20,
    FT_SketchLine  = 21,
    FT_Other       = 99
};

// Стиль линии эскиза (как в Inventor)
enum LineStyle {
    LS_Normal       = 0,  // обычная геометрия — фиолетовая сплошная
    LS_Construction = 1,  // вспомогательная — серая пунктирная
    LS_Centerline   = 2   // осевая — красно-коричневая штрих-пунктирная
};

class BreezeDocument {
public:
    BreezeDocument();
    ~BreezeDocument();

    Handle(TDocStd_Document) GetDoc() const { return myDoc; }
    void SetDocument(const Handle(TDocStd_Document)& doc);

    TDF_Label MainLabel() const;

    void CreateBaseGeometry();
    TDF_Label GetBaseGeometryLabel() const;

    TDF_Label AddBoxFeature(const std::string& name,
                            double dx, double dy, double dz,
                            double px = 0.0, double py = 0.0, double pz = 0.0);

    TDF_Label AddCylinderFeature(const std::string& name,
                                 double radius, double height);

    TDF_Label AddSketchFeature(const TDF_Label& parentPlane);

    // === Линии эскиза ===
    TDF_Label AddSketchLine(const TDF_Label& sketchLabel,
                            const gp_Pnt& p1, const gp_Pnt& p2,
                            LineStyle style = LS_Normal);

    // Получить стиль линии (по умолчанию LS_Normal)
    static LineStyle GetLineStyle(const TDF_Label& lineLabel);
    void RemoveLastSketchLine(const TDF_Label& sketchLabel);
    void ClearSketch(const TDF_Label& sketchLabel);

    void RemoveFeature(const TDF_Label& label);

    // === Undo / Redo через OCAF-транзакции ===
    void StartTransaction();
    void CommitTransaction();
    bool Undo();
    bool Redo();
    int  GetUndoLimit() const;
    void SetUndoLimit(int n);

    static bool IsSystem(const TDF_Label& label);
    static int  GetType(const TDF_Label& label);

    TopoDS_Shape GetShape(const TDF_Label& label) const;
    std::vector<TopoDS_Shape> GetAllShapes() const;

    void RefreshNextFeatureId();

    // Есть ли среди прямых детей Main хотя бы одна 3D-операция?
    bool Has3DOperations() const;

private:
    Handle(TDocStd_Document) myDoc;
    int myNextFeatureId = 1;
    TDF_Label myBaseLabel;

    void CollectShapes(const TDF_Label& label, std::vector<TopoDS_Shape>& out) const;
};

} // namespace BreezeCAD
