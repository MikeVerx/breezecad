#include "kernel/occt/Extrude.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <gp_Pln.hxx>
#include <TopoDS_Solid.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
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
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <format>
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
                                     const Frame& frame, double throughAllLength, const std::optional<Frame>& target) {
    if (faces.empty()) return Error{"Нет замкнутого контура для выдавливания"};

    double length = f.distance.value;
    if (f.extent == doc::ExtentType::ThroughAll) length = throughAllLength;
    gp_Vec dir(frame.normal.x, frame.normal.y, frame.normal.z);
    if (f.reversed) dir.Reverse();

    // Up to a plane: parallel to the sketch, exactly that far (the side it is
    // on decides the direction); at an angle, far enough and then cut by it,
    // so the end takes the plane's slope.
    std::optional<gp_Pln> slanted;
    if (f.extent == doc::ExtentType::ToPlane) {
        if (!target) return Error{"Выберите плоскость или плоскую грань, до которой выдавливать"};
        const double across = target->normal.Dot(frame.normal);
        if (std::abs(across) < 1e-6) return Error{"Плоскость «до» параллельна направлению выдавливания — до неё не дойти"};
        if (std::abs(std::abs(across) - 1.0) < 1e-9) {
            const double gap = (target->origin - frame.origin).Dot(frame.normal);
            if (std::abs(gap) < 1e-6) return Error{"Плоскость «до» совпадает с плоскостью эскиза"};
            dir = gp_Vec(frame.normal.x, frame.normal.y, frame.normal.z) * (gap < 0 ? -1.0 : 1.0);
            length = std::abs(gap);
        } else {
            length = throughAllLength * 2;
            slanted = gp_Pln(gp_Pnt(target->origin.x, target->origin.y, target->origin.z),
                             gp_Dir(target->normal.x, target->normal.y, target->normal.z));
        }
    }
    if (length <= 1e-6) return Error{"Расстояние должно быть больше нуля"};

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
        if (slanted) {
            // Keep the part on the sketch's side of the plane.
            GProp_GProps props;
            for (const TopoDS_Face& face : faces) BRepGProp::SurfaceProperties(face, props);
            const TopoDS_Face plane = BRepBuilderAPI_MakeFace(*slanted).Face();
            const TopoDS_Solid half = BRepPrimAPI_MakeHalfSpace(plane, props.CentreOfMass()).Solid();
            result = BRepAlgoAPI_Common(result, half).Shape();
            if (IsEmpty(result)) return Error{"Плоскость «до» не пересекает выдавливание"};
        }
        return Clean(result);
    } catch (const Standard_Failure& failure) {
        return Error{std::format("Ошибка выдавливания: {}", failure.GetMessageString())};
    }
}

namespace {

/// What every boolean result goes through: not empty, cleaned, valid.
Expected<TopoDS_Shape> FinishBoolean(const TopoDS_Shape& raw, doc::BooleanOp op) {
    if (IsEmpty(raw)) {
        return Error{op == doc::BooleanOp::Intersect ? "Пересечение пустое: тела не перекрываются"
                                                     : "После операции не осталось материала"};
    }
    const TopoDS_Shape result = Clean(raw);
    if (!BRepCheck_Analyzer(result).IsValid()) return Error{"Результат операции некорректен"};
    return result;
}

} // namespace

Expected<TopoDS_Shape> ApplyBoolean(const TopoDS_Shape& target, const TopoDS_Shape& tool, doc::BooleanOp op) {
    try {
        TopoDS_Shape result;
        switch (op) {
            case doc::BooleanOp::NewBody:   return tool;
            case doc::BooleanOp::Join:      result = BRepAlgoAPI_Fuse(target, tool).Shape(); break;
            case doc::BooleanOp::Cut:       result = BRepAlgoAPI_Cut(target, tool).Shape(); break;
            case doc::BooleanOp::Intersect: result = BRepAlgoAPI_Common(target, tool).Shape(); break;
        }
        return FinishBoolean(result, op);
    } catch (const Standard_Failure& failure) {
        return Error{std::format("Ошибка булевой операции: {}", failure.GetMessageString())};
    }
}

Expected<TopoDS_Shape> ApplyBoolean(const TopoDS_Shape& target, const std::vector<TopoDS_Shape>& tools, doc::BooleanOp op) {
    if (tools.empty()) return target;
    if (op != doc::BooleanOp::Join && op != doc::BooleanOp::Cut) return Error{"Копировать можно только добавление и вырезание"};
    try {
        TopTools_ListOfShape arguments, toolList;
        arguments.Append(target);
        for (const TopoDS_Shape& t : tools) toolList.Append(t);
        TopoDS_Shape result;
        if (op == doc::BooleanOp::Join) {
            BRepAlgoAPI_Fuse fuse;
            fuse.SetArguments(arguments);
            fuse.SetTools(toolList);
            fuse.Build();
            if (!fuse.IsDone()) return Error{"Ошибка булевой операции"};
            result = fuse.Shape();
        } else {
            BRepAlgoAPI_Cut cut;
            cut.SetArguments(arguments);
            cut.SetTools(toolList);
            cut.Build();
            if (!cut.IsDone()) return Error{"Ошибка булевой операции"};
            result = cut.Shape();
        }
        return FinishBoolean(result, op);
    } catch (const Standard_Failure& failure) {
        return Error{std::format("Ошибка булевой операции: {}", failure.GetMessageString())};
    }
}

TopoDS_Shape Clean(const TopoDS_Shape& shape) {
    ShapeUpgrade_UnifySameDomain unify(shape, true, true, true);
    // Curved faces next to free-form ones (a cylinder cut by a thread
    // groove) are left as they are: merging them can take OCCT minutes.
    TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
    TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
    const auto typeOf = [](const TopoDS_Shape& face) { return BRepAdaptor_Surface(TopoDS::Face(face)).GetType(); };
    const auto freeform = [&](const TopoDS_Shape& face) { return typeOf(face) > GeomAbs_Torus; };
    for (TopExp_Explorer f(shape, TopAbs_FACE); f.More(); f.Next()) {
        if (typeOf(f.Current()) == GeomAbs_Plane) continue;
        bool touches = freeform(f.Current());
        for (TopExp_Explorer e(f.Current(), TopAbs_EDGE); e.More() && !touches; e.Next())
            for (const TopoDS_Shape& other : edgeFaces.FindFromKey(e.Current())) touches = touches || freeform(other);
        if (touches)
            for (TopExp_Explorer e(f.Current(), TopAbs_EDGE); e.More(); e.Next()) unify.KeepShape(e.Current());
    }
    unify.Build();
    return unify.Shape();
}

} // namespace bz::kernel::occt
