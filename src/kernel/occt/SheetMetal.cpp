#include "kernel/occt/SheetMetal.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Standard_Failure.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax3.hxx>
#include <gp_Circ.hxx>
#include <gp_Trsf.hxx>

#include <cmath>
#include <vector>

namespace bz::kernel::occt {
namespace {

gp_Pnt P(Vec2 p, double z = 0.0) { return {p.x, p.y, z}; }

/// One edge of the face as outline segments, in the frame's 2D coordinates.
void AddEdge(const TopoDS_Edge& edge, const Frame& frame, sheet::Loop& loop) {
    BRepAdaptor_Curve c(edge);
    const bool forward = edge.Orientation() != TopAbs_REVERSED;
    const double t0 = forward ? c.FirstParameter() : c.LastParameter();
    const double t1 = forward ? c.LastParameter() : c.FirstParameter();
    const auto local = [&](double t) {
        const gp_Pnt p = c.Value(t);
        return frame.ToLocal({p.X(), p.Y(), p.Z()});
    };
    const Vec2 a = local(t0), b = local(t1);
    if (c.GetType() == GeomAbs_Line) {
        loop.push_back(sheet::Segment::Line(a, b));
        return;
    }
    if (c.GetType() == GeomAbs_Circle) {
        const gp_Circ circ = c.Circle();
        const gp_Pnt o = circ.Location();
        const Vec2 center = frame.ToLocal({o.X(), o.Y(), o.Z()});
        const double r = circ.Radius();
        if (std::abs(c.LastParameter() - c.FirstParameter() - 2 * kPi) < 1e-9) {
            sheet::Segment s = sheet::Segment::Circle(center, r);
            // Its direction in the frame: the circle's axis against the frame normal.
            const gp_Dir ax = circ.Axis().Direction();
            const bool ccw = (ax.X() * frame.normal.x + ax.Y() * frame.normal.y + ax.Z() * frame.normal.z > 0) == forward;
            s.ccw = ccw;
            loop.push_back(s);
            return;
        }
        // Direction of travel from the middle point.
        const Vec2 m = local((t0 + t1) / 2);
        const bool ccw = (a - center).Cross(m - center) > 0;
        loop.push_back(sheet::Segment::Arc(a, b, center, ccw));
        return;
    }
    // Splines, ellipses: short lines.
    GCPnts_QuasiUniformDeflection points(c, 0.01);
    if (!points.IsDone() || points.NbPoints() < 2) {
        loop.push_back(sheet::Segment::Line(a, b));
        return;
    }
    std::vector<Vec2> pts;
    for (int i = 1; i <= points.NbPoints(); ++i) {
        const gp_Pnt p = points.Value(i);
        pts.push_back(frame.ToLocal({p.X(), p.Y(), p.Z()}));
    }
    if (!forward) std::reverse(pts.begin(), pts.end());
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) loop.push_back(sheet::Segment::Line(pts[i], pts[i + 1]));
}

TopoDS_Edge MakeEdge(const sheet::Segment& s, double z) {
    if (!s.arc) return BRepBuilderAPI_MakeEdge(P(s.a, z), P(s.b, z)).Edge();
    if (s.full) return BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(P(s.center, z), s.ccw ? gp::DZ() : -gp::DZ()), s.Radius())).Edge();
    GC_MakeArcOfCircle arc(P(s.a, z), P(s.Middle(), z), P(s.b, z));
    return BRepBuilderAPI_MakeEdge(arc.Value()).Edge();
}

TopoDS_Wire MakeWire(const sheet::Loop& loop, double z) {
    BRepBuilderAPI_MakeWire w;
    for (const sheet::Segment& s : loop) w.Add(MakeEdge(s, z));
    return w.Wire();
}

Expected<TopoDS_Shape> Fuse(const TopoDS_Shape& a, const TopoDS_Shape& b) {
    BRepAlgoAPI_Fuse fuse(a, b);
    if (!fuse.IsDone()) return Error{"Не удалось соединить сгиб с листом"};
    return fuse.Shape();
}

} // namespace

Expected<sheet::Region> RegionFromFace(const TopoDS_Face& face, const Frame& frame) {
    sheet::Region region;
    const TopoDS_Wire outer = BRepTools::OuterWire(face);
    for (TopExp_Explorer w(face, TopAbs_WIRE); w.More(); w.Next()) {
        const TopoDS_Wire wire = TopoDS::Wire(w.Current());
        sheet::Loop loop;
        for (BRepTools_WireExplorer e(wire, face); e.More(); e.Next()) AddEdge(e.Current(), frame, loop);
        if (loop.empty()) continue;
        if (wire.IsSame(outer)) region.outer = std::move(loop);
        else region.holes.push_back(std::move(loop));
    }
    if (region.outer.empty()) return Error{"У контура листа нет наружной границы"};
    region.Orient();
    return region;
}

Expected<TopoDS_Shape> MakeSheetBody(const sheet::Part& part, const sheet::Layout& layout, const Frame& frame,
                                     std::vector<std::size_t>* clashes) {
    const double s = part.rules.thickness, r = part.rules.radius;
    if (!layout.flanges.empty() && r < 1e-6) return Error{"Радиус гиба 0 пока не поддерживается: задайте радиус больше нуля"};
    try {
        // Local coordinates: the sketch plane is z = 0, its normal +z.
        const double z0 = part.reversed ? -s : 0.0;
        BRepBuilderAPI_MakeFace makeFace(MakeWire(layout.base.outer, z0), true);
        for (const sheet::Loop& hole : layout.base.holes) makeFace.Add(MakeWire(hole, z0));
        if (!makeFace.IsDone()) return Error{"Контур листа не удалось превратить в грань"};
        TopoDS_Shape body = BRepPrimAPI_MakePrism(makeFace.Face(), gp_Vec(0, 0, s)).Shape();

        const auto pnt = [](Vec3 v) { return gp_Pnt(v.x, v.y, v.z); };
        const auto vec = [](Vec3 v) { return gp_Vec(v.x, v.y, v.z); };
        for (const sheet::BentFlange& f : layout.flanges) {
            // The end of the face where the bend starts: the edge times the thickness.
            const Vec3 a = f.start, b = f.start + f.along * f.width, up = f.normal * s;
            BRepBuilderAPI_MakePolygon end(pnt(a), pnt(b), pnt(b + up), pnt(a + up), true);
            const TopoDS_Face endFace = BRepBuilderAPI_MakeFace(end.Wire(), true).Face();
            const gp_Ax1 axis(pnt(f.axisPoint), gp_Dir(vec(f.axisDir)));
            const TopoDS_Shape bend = BRepPrimAPI_MakeRevol(endFace, axis, f.angle).Shape();
            gp_Trsf turn;
            turn.SetRotation(axis, f.angle);
            const TopoDS_Shape bentEnd = BRepBuilderAPI_Transform(endFace, turn, true).Shape();
            const TopoDS_Shape strip = BRepPrimAPI_MakePrism(bentEnd, vec(f.stripOut) * f.straight).Shape();
            // Does the new flange go into what is there already? Touching (its own seam) has no volume.
            if (clashes) {
                double shared = 0.0;
                for (const TopoDS_Shape* piece : {&bend, &strip}) {
                    BRepAlgoAPI_Common common(body, *piece);
                    if (!common.IsDone()) continue;
                    GProp_GProps props;
                    BRepGProp::VolumeProperties(common.Shape(), props);
                    shared += std::abs(props.Mass());
                }
                if (shared > 1e-3 * std::max(1.0, s * s * s)) clashes->push_back(static_cast<std::size_t>(&f - layout.flanges.data()));
            }

            Expected<TopoDS_Shape> joined = Fuse(body, bend);
            if (!joined) return joined;
            joined = Fuse(*joined, strip);
            if (!joined) return joined;
            body = *joined;
        }
        // Cuts through flanges' strips.
        for (const sheet::Cut& cut : layout.stripCuts) {
            const sheet::BentFlange& f = layout.flanges[static_cast<std::size_t>(cut.face)];
            const auto onStrip = [&](Vec2 p) { return pnt(f.stripStart + f.along * p.x + f.stripOut * p.y); };
            BRepBuilderAPI_MakeWire wire;
            for (const sheet::Segment& sg : cut.loop) {
                if (!sg.arc) wire.Add(BRepBuilderAPI_MakeEdge(onStrip(sg.a), onStrip(sg.b)).Edge());
                else if (sg.full)
                    wire.Add(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(onStrip(sg.center), gp_Dir(vec(f.stripNormal))), sg.Radius())).Edge());
                else wire.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(onStrip(sg.a), onStrip(sg.Middle()), onStrip(sg.b)).Value()).Edge());
            }
            if (!wire.IsDone()) return Error{"Контур выреза не замкнулся"};
            const TopoDS_Face face = BRepBuilderAPI_MakeFace(wire.Wire(), true).Face();
            // Through the strip and a little past both sides.
            gp_Trsf back;
            back.SetTranslation(vec(f.stripNormal * -0.1));
            const TopoDS_Shape start = BRepBuilderAPI_Transform(face, back, true).Shape();
            const TopoDS_Shape tool = BRepPrimAPI_MakePrism(start, vec(f.stripNormal * (s + 0.2))).Shape();
            BRepAlgoAPI_Cut cutter(body, tool);
            if (!cutter.IsDone()) return Error{"Вырез в полке не удался"};
            body = cutter.Shape();
        }
        ShapeUpgrade_UnifySameDomain unify(body, true, true, false);
        unify.Build();
        body = unify.Shape();

        gp_Trsf place;
        place.SetDisplacement(gp_Ax3(gp::XOY()), gp_Ax3(gp_Pnt(frame.origin.x, frame.origin.y, frame.origin.z),
                                                       gp_Dir(frame.normal.x, frame.normal.y, frame.normal.z),
                                                       gp_Dir(frame.xDir.x, frame.xDir.y, frame.xDir.z)));
        return BRepBuilderAPI_Transform(body, place, true).Shape();
    } catch (const Standard_Failure& e) {
        return Error{std::string("Лист не построился: ") + (e.GetMessageString() ? e.GetMessageString() : "ошибка ядра")};
    }
}

} // namespace bz::kernel::occt
