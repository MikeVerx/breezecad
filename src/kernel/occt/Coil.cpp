#include "kernel/occt/Coil.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepLib.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <GCE2d_MakeSegment.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Standard_Failure.hxx>
#include <gp_Ax3.hxx>
#include <gp_Circ.hxx>

#include <cmath>

namespace bz::kernel::occt {

Expected<TopoDS_Shape> MakeCoil(const doc::CoilFeature& f) {
    for (const Parameter* p : {&f.radius, &f.pitch, &f.turns, &f.wire, &f.start})
        if (p->HasError()) return Error{p->error};
    if (f.radius.value <= 1e-6 || f.pitch.value <= 1e-6 || f.turns.value <= 1e-6 || f.wire.value <= 1e-6)
        return Error{"Радиус, шаг, число витков и диаметр проволоки должны быть больше нуля"};
    if (f.wire.value >= f.pitch.value) return Error{"Проволока толще шага: витки слипаются"};
    if (f.wire.value / 2 >= f.radius.value) return Error{"Проволока толще радиуса пружины"};
    if (f.turns.value > 500) return Error{"Слишком много витков (больше 500)"};

    const Vec3 a = doc::AxisDirection(f.axis);
    const gp_Dir axis(a.x, a.y, a.z);
    const gp_Pnt base = gp_Pnt(0, 0, 0).Translated(gp_Vec(axis) * f.start.value);
    // Any direction across the axis for the coil's start.
    const gp_Dir across = std::abs(a.z) < 0.9 ? gp_Dir(gp_Vec(axis).Crossed(gp_Vec(0, 0, 1))) : gp_Dir(1, 0, 0);
    try {
        // The helix: a straight line on the unrolled cylinder (u = angle, v = height).
        const gp_Ax3 frame(base, axis, across);
        Handle(Geom_CylindricalSurface) cylinder = new Geom_CylindricalSurface(frame, f.radius.value);
        const double angle = 2 * kPi * f.turns.value;
        const double height = f.pitch.value * f.turns.value;
        const gp_Pnt2d from(0, 0), to(f.leftHanded ? -angle : angle, height);
        const Handle(Geom2d_TrimmedCurve) line = GCE2d_MakeSegment(from, to);
        TopoDS_Edge helixEdge = BRepBuilderAPI_MakeEdge(line, cylinder);
        BRepLib::BuildCurves3d(helixEdge);
        const TopoDS_Wire helix = BRepBuilderAPI_MakeWire(helixEdge);

        // The wire's cross-section: a circle at the helix start, square to it.
        const gp_Pnt startPoint = base.Translated(gp_Vec(across) * f.radius.value);
        gp_Vec tangent = gp_Vec(axis).Crossed(gp_Vec(across)) * (f.radius.value * (f.leftHanded ? -1 : 1)) +
                         gp_Vec(axis) * (f.pitch.value / (2 * kPi));
        const gp_Circ section(gp_Ax2(startPoint, gp_Dir(tangent)), f.wire.value / 2);
        const TopoDS_Wire profile = BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(section));

        BRepOffsetAPI_MakePipeShell sweep(helix);
        sweep.SetMode(gp_Dir(axis));   // keep the section's tilt constant around the axis (a binormal mode)
        sweep.Add(profile);
        sweep.Build();
        if (!sweep.IsDone() || !sweep.MakeSolid()) return Error{"Пружина не строится с такими размерами"};
        const TopoDS_Shape result = sweep.Shape();
        if (!BRepCheck_Analyzer(result).IsValid()) return Error{"Пружина получилась некорректной"};
        return result;
    } catch (const Standard_Failure& failure) {
        return Error{std::string("Ошибка пружины: ") + failure.GetMessageString()};
    }
}

} // namespace bz::kernel::occt
