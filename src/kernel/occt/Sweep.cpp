#include "kernel/occt/Sweep.h"

#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <Standard_Failure.hxx>

namespace bz::kernel::occt {

Expected<TopoDS_Shape> MakeSweep(const std::vector<TopoDS_Face>& faces, const TopoDS_Wire& path) {
    if (faces.empty()) return Error{"В эскизе профиля нет замкнутых контуров"};
    try {
        TopoDS_Shape result;
        for (const TopoDS_Face& face : faces) {
            BRepOffsetAPI_MakePipe pipe(path, face);
            pipe.Build();
            if (!pipe.IsDone()) return Error{"Сдвиг не строится вдоль этой траектории"};
            result = result.IsNull() ? pipe.Shape() : BRepAlgoAPI_Fuse(result, pipe.Shape()).Shape();
        }
        if (!BRepCheck_Analyzer(result).IsValid())
            return Error{"Сдвиг получился некорректным: профиль слишком велик для изгибов траектории?"};
        return result;
    } catch (const Standard_Failure& failure) {
        return Error{std::string("Ошибка сдвига: ") + failure.GetMessageString()};
    }
}

} // namespace bz::kernel::occt
