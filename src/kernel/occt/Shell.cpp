#include "kernel/occt/Shell.h"

#include "kernel/occt/Extrude.h"   // ApplyBoolean
#include "kernel/occt/FaceMatch.h"

#include <BRepCheck_Analyzer.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <Standard_Failure.hxx>
#include <TopTools_ListOfShape.hxx>

namespace bz::kernel::occt {

Expected<TopoDS_Shape> MakeShell(const TopoDS_Shape& body, const doc::ShellFeature& f) {
    if (f.thickness.HasError()) return Error{f.thickness.error};
    if (f.thickness.value <= 1e-6) return Error{"Толщина стенки должна быть больше нуля"};

    TopTools_ListOfShape open;
    for (const doc::FaceReference& ref : f.openFaces) {
        const std::optional<TopoDS_Face> face = ResolveFace(body, ref);
        if (!face) return Error{"Грань оболочки не найдена: модель изменилась слишком сильно"};
        if (!open.Contains(*face)) open.Append(*face);
    }
    try {
        // OCCT offsets along the outward normal: negative grows the walls inward.
        const double offset = f.outward ? f.thickness.value : -f.thickness.value;
        if (open.IsEmpty()) {
            // A closed hollow: MakeThickSolid with nothing to open returns the
            // offset surface as a solid, so cut the inner solid out instead.
            BRepOffsetAPI_MakeOffsetShape offsetShape;
            offsetShape.PerformByJoin(body, offset, 1e-4);
            if (!offsetShape.IsDone()) return Error{"Оболочка не строится: стенка слишком толстая для этой формы?"};
            const TopoDS_Shape outer = f.outward ? offsetShape.Shape() : body;
            const TopoDS_Shape inner = f.outward ? body : offsetShape.Shape();
            return ApplyBoolean(outer, inner, doc::BooleanOp::Cut);
        }
        BRepOffsetAPI_MakeThickSolid maker;
        maker.MakeThickSolidByJoin(body, open, offset, 1e-4);
        maker.Build();
        if (!maker.IsDone()) return Error{"Оболочка не строится: стенка слишком толстая для этой формы?"};
        const TopoDS_Shape result = maker.Shape();
        if (!BRepCheck_Analyzer(result).IsValid()) return Error{"Оболочка получилась некорректной: уменьшите толщину"};
        return result;
    } catch (const Standard_Failure& failure) {
        return Error{std::string("Ошибка оболочки: ") + failure.GetMessageString()};
    }
}

} // namespace bz::kernel::occt
