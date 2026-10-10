#include "kernel/occt/Shell.h"

#include "kernel/occt/Extrude.h"

#include <BRepCheck_Analyzer.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepOffset_Mode.hxx>
#include <GeomAbs_JoinType.hxx>
#include <Standard_Failure.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>

namespace bz::kernel::occt {
namespace {

constexpr double kTolerance = 1e-4;
constexpr const char* kFailed = "Не удалось построить оболочку: толщина слишком большая для этого тела?";

/// The whole body offset by `distance` (negative: inside) with sharp corners.
Expected<TopoDS_Shape> Offset(const TopoDS_Shape& body, double distance) {
    BRepOffsetAPI_MakeOffsetShape offset;
    offset.PerformByJoin(body, distance, kTolerance, BRepOffset_Skin, false, false, GeomAbs_Intersection);
    if (!offset.IsDone() || offset.Shape().IsNull()) return Error{kFailed};
    return offset.Shape();
}

} // namespace

Expected<TopoDS_Shape> MakeShell(const TopoDS_Shape& body, const std::vector<TopoDS_Face>& open, double thickness, bool outward) {
    if (thickness <= 1e-6) return Error{"Толщина должна быть больше нуля"};
    try {
        TopoDS_Shape result;
        if (open.empty()) {
            // Closed hollow: cut the inner offset from the body (or the body
            // from the outer offset).
            const Expected<TopoDS_Shape> other = Offset(body, outward ? thickness : -thickness);
            if (!other) return other.GetError();
            const Expected<TopoDS_Shape> cut =
                outward ? ApplyBoolean(*other, body, doc::BooleanOp::Cut) : ApplyBoolean(body, *other, doc::BooleanOp::Cut);
            if (!cut) return Error{kFailed};
            result = *cut;
        } else {
            TopTools_ListOfShape faces;
            for (const TopoDS_Face& f : open) faces.Append(f);
            BRepOffsetAPI_MakeThickSolid solid;
            solid.MakeThickSolidByJoin(body, faces, outward ? thickness : -thickness, kTolerance, BRepOffset_Skin, false,
                                       false, GeomAbs_Intersection);
            if (!solid.IsDone() || solid.Shape().IsNull()) return Error{kFailed};
            result = solid.Shape();
        }
        if (!BRepCheck_Analyzer(result).IsValid()) return Error{kFailed};
        return result;
    } catch (const Standard_Failure&) {
        return Error{kFailed};
    }
}

} // namespace bz::kernel::occt
