#include "kernel/occt/Extrude.h"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GProp_GProps.hxx>
#include <LocOpe_DPrism.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <cmath>

namespace bz::kernel::occt {
namespace {

bool IsEmpty(const TopoDS_Shape& shape) {
    if (shape.IsNull()) return true;
    TopExp_Explorer it(shape, TopAbs_SOLID);
    return !it.More();
}

/// One face swept along `dir` by `length`, optionally with a draft angle.
TopoDS_Shape Sweep(const TopoDS_Face& face, const gp_Vec& dir, double length, double taper) {
    if (std::abs(taper) < 1e-9) return BRepPrimAPI_MakePrism(face, dir * length).Shape();

    // LocOpe_DPrism sweeps along the face normal; orient the face so its
    // normal points where we want to go. Positive taper narrows the solid.
    TopoDS_Face oriented = face;
    BRepGProp_Face props(face);
    double u1, u2, v1, v2;
    props.Bounds(u1, u2, v1, v2);
    gp_Pnt p;
    gp_Vec normal;
    props.Normal((u1 + u2) / 2, (v1 + v2) / 2, p, normal);
    if (normal.Dot(dir) < 0) oriented.Reverse();
    LocOpe_DPrism prism(oriented, length, taper);
    return prism.Shape();
}

} // namespace

Expected<TopoDS_Shape> MakeExtrusion(const std::vector<TopoDS_Face>& faces, const doc::ExtrudeFeature& f,
                                     const Frame& frame, double throughAllLength) {
    if (faces.empty()) return Error{"Нет замкнутого контура для выдавливания"};

    double length = f.distance.value;
    if (f.extent == doc::ExtentType::ThroughAll) length = throughAllLength;
    if (length <= 1e-6) return Error{"Расстояние должно быть больше нуля"};

    gp_Vec dir(frame.normal.x, frame.normal.y, frame.normal.z);
    if (f.reversed) dir.Reverse();

    // Symmetric (and through-all both ways) start half a length behind the plane.
    const bool symmetric = f.extent == doc::ExtentType::Symmetric;
    gp_Trsf shift;
    if (symmetric) shift.SetTranslation(dir * (-length / 2.0));

    try {
        TopoDS_Shape result;
        for (const TopoDS_Face& face : faces) {
            const TopoDS_Face start =
                symmetric ? TopoDS::Face(BRepBuilderAPI_Transform(face, shift, true).Shape()) : face;
            TopoDS_Shape piece = Sweep(start, dir, length, f.taper.value);
            if (IsEmpty(piece)) return Error{"Не удалось построить выдавливание (уклон слишком большой?)"};
            result = result.IsNull() ? piece : BRepAlgoAPI_Fuse(result, piece).Shape();
        }
        return Clean(result);
    } catch (const Standard_Failure& failure) {
        return Error{std::string("Ошибка выдавливания: ") + failure.GetMessageString()};
    }
}

Expected<TopoDS_Shape> ApplyBoolean(const TopoDS_Shape& target, const TopoDS_Shape& tool, doc::BooleanOp op) {
    try {
        TopoDS_Shape result;
        switch (op) {
            case doc::BooleanOp::NewBody:   return tool;
            case doc::BooleanOp::Join:      result = BRepAlgoAPI_Fuse(target, tool).Shape(); break;
            case doc::BooleanOp::Cut:       result = BRepAlgoAPI_Cut(target, tool).Shape(); break;
            case doc::BooleanOp::Intersect: result = BRepAlgoAPI_Common(target, tool).Shape(); break;
        }
        if (IsEmpty(result)) {
            return Error{op == doc::BooleanOp::Intersect ? "Пересечение пустое: тела не перекрываются"
                                                         : "После операции не осталось материала"};
        }
        result = Clean(result);
        if (!BRepCheck_Analyzer(result).IsValid()) return Error{"Результат операции некорректен"};
        return result;
    } catch (const Standard_Failure& failure) {
        return Error{std::string("Ошибка булевой операции: ") + failure.GetMessageString()};
    }
}

TopoDS_Shape Clean(const TopoDS_Shape& shape) {
    ShapeUpgrade_UnifySameDomain unify(shape, true, true, true);
    unify.Build();
    return unify.Shape();
}

} // namespace bz::kernel::occt
