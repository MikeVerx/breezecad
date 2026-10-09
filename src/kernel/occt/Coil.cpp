#include "kernel/occt/Coil.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepTools.hxx>
#include <GCE2d_MakeSegment.hxx>
#include <GProp_GProps.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Ax3.hxx>

#include <algorithm>
#include <cmath>

namespace bz::kernel::occt {
namespace {

/// A solid from sweeping `profile` along `helix` with a fixed axis direction.
Expected<TopoDS_Shape> SweepWire(const TopoDS_Wire& profile, const TopoDS_Wire& helix, const gp_Dir& axis) {
    BRepOffsetAPI_MakePipeShell sweep(helix);
    sweep.SetMode(axis);   // the section keeps its tilt to the axis, as a coil does
    sweep.Add(profile, false, false);
    sweep.Build();
    if (!sweep.IsDone() || !sweep.MakeSolid()) return Error{"Пружина не строится с такими размерами"};
    return sweep.Shape();
}

} // namespace

Expected<TopoDS_Shape> MakeCoil(const std::vector<TopoDS_Face>& faces, const doc::CoilFeature& f, const sketch::Sketch& sk,
                                const Frame& frame) {
    for (const Parameter* p : {&f.pitch, &f.turns})
        if (p->HasError()) return Error{p->error};
    if (f.pitch.value <= 1e-6 || f.turns.value <= 1e-6) return Error{"Шаг и число витков должны быть больше нуля"};
    if (f.turns.value > 500) return Error{"Слишком много витков (больше 500)"};
    const sketch::Line* line = sk.FindLine(f.axisLine);
    if (!f.axisLine) return Error{"Нарисуйте в эскизе осевую линию и щёлкните её — вокруг неё навивается пружина"};
    if (!line) return Error{"Ось пружины удалена из эскиза"};
    if (faces.empty()) return Error{"В эскизе нет замкнутого контура для профиля"};

    const Vec3 a = frame.ToWorld(sk.PositionOf(line->start)), b = frame.ToWorld(sk.PositionOf(line->end));
    if ((b - a).Length() < 1e-9) return Error{"Ось пружины нулевой длины"};
    const Vec3 dir = (b - a).Normalized();

    try {
        // Where the profile sits relative to the axis: its centre gives the
        // helix radius and starting height; its extent along the axis must
        // stay below the pitch or the turns run into each other.
        GProp_GProps props;
        for (const TopoDS_Face& face : faces) BRepGProp::SurfaceProperties(face, props);
        const gp_Pnt c = props.CentreOfMass();
        const Vec3 centre{c.X(), c.Y(), c.Z()};
        const double along = (centre - a).Dot(dir);
        const Vec3 foot = a + dir * along;
        const double radius = (centre - foot).Length();
        if (radius < 1e-6) return Error{"Профиль лежит на оси — отодвиньте его от осевой линии"};
        double lo = 1e300, hi = -1e300, nearest = 1e300;
        for (const TopoDS_Face& face : faces)
            for (TopExp_Explorer e(face, TopAbs_EDGE); e.More(); e.Next()) {
                const BRepAdaptor_Curve curve(TopoDS::Edge(e.Current()));
                for (int i = 0; i <= 24; ++i) {
                    const gp_Pnt p = curve.Value(curve.FirstParameter() + (curve.LastParameter() - curve.FirstParameter()) * i / 24.0);
                    const Vec3 v = Vec3{p.X(), p.Y(), p.Z()} - a;
                    const double h = v.Dot(dir);
                    lo = std::min(lo, h);
                    hi = std::max(hi, h);
                    nearest = std::min(nearest, (v - dir * h).Length());
                }
            }
        if (nearest < 1e-6) return Error{"Профиль касается оси или пересекает её — отодвиньте его от осевой линии"};
        if (hi - lo >= f.pitch.value - 1e-6) return Error{"Шаг меньше высоты профиля: витки пересекаются"};

        // The helix: one edge per half turn on the cylinder through the profile centre.
        const gp_Dir axis(dir.x, dir.y, dir.z);
        const gp_Dir across((centre - foot).x, (centre - foot).y, (centre - foot).z);
        const gp_Ax3 helixFrame(gp_Pnt(foot.x, foot.y, foot.z), axis, across);
        Handle(Geom_CylindricalSurface) cylinder = new Geom_CylindricalSurface(helixFrame, radius);
        const double angle = 2 * kPi * f.turns.value, height = f.pitch.value * f.turns.value;
        const int pieces = std::max(1, static_cast<int>(std::ceil(f.turns.value * 2 - 1e-9)));
        const double sign = f.leftHanded ? -1.0 : 1.0;
        BRepBuilderAPI_MakeWire helixWire;
        for (int i = 0; i < pieces; ++i) {
            const double t0 = static_cast<double>(i) / pieces, t1 = static_cast<double>(i + 1) / pieces;
            const Handle(Geom2d_TrimmedCurve) segment =
                GCE2d_MakeSegment(gp_Pnt2d(sign * angle * t0, height * t0), gp_Pnt2d(sign * angle * t1, height * t1));
            TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(segment, cylinder);
            BRepLib::BuildCurves3d(edge);
            helixWire.Add(edge);
        }
        if (!helixWire.IsDone()) return Error{"Пружина: не удалось построить винтовую линию"};
        const TopoDS_Wire helix = helixWire.Wire();

        // Each region: its outer boundary swept, its holes swept and cut out.
        TopoDS_Shape result;
        for (const TopoDS_Face& face : faces) {
            const TopoDS_Wire outer = BRepTools::OuterWire(face);
            Expected<TopoDS_Shape> solid = SweepWire(outer, helix, axis);
            if (!solid) return solid;
            TopoDS_Shape piece = *solid;
            for (TopExp_Explorer w(face, TopAbs_WIRE); w.More(); w.Next()) {
                const TopoDS_Wire wire = TopoDS::Wire(w.Current());
                if (wire.IsSame(outer)) continue;
                Expected<TopoDS_Shape> hole = SweepWire(wire, helix, axis);
                if (!hole) return hole;
                piece = BRepAlgoAPI_Cut(piece, *hole).Shape();
            }
            result = result.IsNull() ? piece : BRepAlgoAPI_Fuse(result, piece).Shape();
        }
        if (!BRepCheck_Analyzer(result).IsValid()) return Error{"Пружина получилась некорректной"};
        return result;
    } catch (const Standard_Failure& failure) {
        return Error{std::string("Ошибка пружины: ") + failure.GetMessageString()};
    }
}

} // namespace bz::kernel::occt
