#include "kernel/occt/Section.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <Bnd_Box.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <format>
#include <gp_Ax2.hxx>

#include <algorithm>
#include <cmath>

namespace bz::kernel::occt {

Expected<std::optional<TopoDS_Shape>> CutByPlane(const TopoDS_Shape& body, const Frame& plane, bool keepFront) {
    Bnd_Box box;
    BRepBndLib::Add(body, box);
    if (box.IsVoid()) return std::optional<TopoDS_Shape>{};
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    // Big enough to swallow the whole body on its side, wherever the plane is.
    const Vec3 corner{x0, y0, z0}, opposite{x1, y1, z1};
    double reach = (opposite - corner).Length();
    for (const Vec3 c : {corner, opposite}) reach = std::max(reach, (c - plane.origin).Length());
    const double size = 2 * reach + 10.0;

    try {
        const gp_Dir n(plane.normal.x, plane.normal.y, plane.normal.z);
        const gp_Dir x(plane.xDir.x, plane.xDir.y, plane.xDir.z);
        // A block standing on the plane on the side that goes away.
        const double side = keepFront ? -1.0 : 1.0;
        const Vec3 base = plane.origin - plane.xDir * (size / 2) - plane.YDir() * (size / 2) + plane.normal * (side < 0 ? -size : 0.0);
        const TopoDS_Shape cutter = BRepPrimAPI_MakeBox(gp_Ax2(gp_Pnt(base.x, base.y, base.z), n, x), size, size, size).Shape();
        BRepAlgoAPI_Cut cut(body, cutter);
        if (!cut.IsDone()) return Error{"Сечение: не удалось рассечь тело"};
        const TopoDS_Shape result = cut.Shape();
        if (!TopExp_Explorer(result, TopAbs_SOLID).More()) return std::optional<TopoDS_Shape>{};
        return std::optional<TopoDS_Shape>{result};
    } catch (const Standard_Failure& failure) {
        return Error{std::format("Ошибка сечения: {}", failure.GetMessageString())};
    }
}

} // namespace bz::kernel::occt
