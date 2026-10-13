#include "kernel/occt/Sweep.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_TransitionMode.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepTools.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <format>
#include <optional>

namespace bz::kernel::occt {

namespace {

/// One closed wire carried along the path into a solid. Sharp corners of
/// the path get mitred (RightCorner): BRepOffsetAPI_MakePipe stopped at the
/// first corner and kept only the first segment.
std::optional<TopoDS_Shape> PipeOf(const TopoDS_Wire& wire, const TopoDS_Wire& path) {
    BRepOffsetAPI_MakePipeShell pipe(path);
    pipe.SetMode(false);   // corrected Frenet: the profile does not twist
    pipe.SetTransitionMode(BRepBuilderAPI_RightCorner);
    pipe.Add(wire);
    pipe.Build();
    if (!pipe.IsDone() || !pipe.MakeSolid()) return std::nullopt;
    return pipe.Shape();
}

} // namespace

Expected<TopoDS_Shape> MakeSweep(const std::vector<TopoDS_Face>& faces, const TopoDS_Wire& path) {
    if (faces.empty()) return Error{"В эскизе профиля нет замкнутых контуров"};
    try {
        TopoDS_Shape result;
        for (const TopoDS_Face& face : faces) {
            const TopoDS_Wire outer = BRepTools::OuterWire(face);
            std::optional<TopoDS_Shape> piece = PipeOf(outer, path);
            if (!piece) return Error{"Сдвиг не строится вдоль этой траектории"};
            // Holes of the region become holes of the pipe.
            for (TopExp_Explorer it(face, TopAbs_WIRE); it.More(); it.Next()) {
                const TopoDS_Wire wire = TopoDS::Wire(it.Current());
                if (wire.IsSame(outer)) continue;
                const std::optional<TopoDS_Shape> hole = PipeOf(wire, path);
                if (!hole) return Error{"Сдвиг не строится вдоль этой траектории"};
                piece = BRepAlgoAPI_Cut(*piece, *hole).Shape();
            }
            result = result.IsNull() ? *piece : BRepAlgoAPI_Fuse(result, *piece).Shape();
        }
        if (!BRepCheck_Analyzer(result).IsValid())
            return Error{"Сдвиг получился некорректным: профиль слишком велик для изгибов траектории?"};
        return result;
    } catch (const Standard_Failure& failure) {
        return Error{std::format("Ошибка сдвига: {}", failure.GetMessageString())};
    }
}

} // namespace bz::kernel::occt
