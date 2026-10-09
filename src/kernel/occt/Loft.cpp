#include "kernel/occt/Loft.h"

#include <BRepCheck_Analyzer.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <Standard_Failure.hxx>

namespace bz::kernel::occt {

Expected<TopoDS_Shape> MakeLoft(const std::vector<TopoDS_Wire>& outlines, bool ruled) {
    try {
        BRepOffsetAPI_ThruSections loft(/*isSolid=*/true, ruled);
        loft.CheckCompatibility(true);   // matches the outlines' starts, so the loft does not twist
        for (const TopoDS_Wire& w : outlines) loft.AddWire(w);
        loft.Build();
        if (!loft.IsDone()) return Error{"Лофт не строится через эти сечения"};
        const TopoDS_Shape result = loft.Shape();
        if (!BRepCheck_Analyzer(result).IsValid()) return Error{"Лофт получился некорректным"};
        return result;
    } catch (const Standard_Failure& failure) {
        return Error{std::string("Ошибка лофта: ") + failure.GetMessageString()};
    }
}

} // namespace bz::kernel::occt
