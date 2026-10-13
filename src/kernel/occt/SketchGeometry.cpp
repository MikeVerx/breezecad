#include "kernel/occt/SketchGeometry.h"

#include "kernel/occt/Text.h"
#include "sketch/SplineGeometry.h"

#include <BOPTools_AlgoTools3D.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <Geom_BSplineCurve.hxx>
#include <IntTools_Context.hxx>
#include <NCollection_List.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <Standard_Failure.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Wire.hxx>
#include <format>
#include <gp.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>

namespace bz::kernel::occt {
namespace {

gp_Pnt ToPnt(Vec3 v) { return {v.x, v.y, v.z}; }
gp_Dir ToDir(Vec3 v) { return {v.x, v.y, v.z}; }

/// Edges of all non-construction curves, in model space.
std::vector<TopoDS_Edge> MakeEdges(const doc::SketchFeature& feature) {
    const sketch::Sketch& sk = feature.sketch;
    const Frame& frame = feature.placement.frame;
    const auto world = [&](ObjectId point) { return ToPnt(frame.ToWorld(sk.PositionOf(point))); };
    const gp_Ax2 axes(ToPnt(frame.origin), ToDir(frame.normal), ToDir(frame.xDir));

    std::vector<TopoDS_Edge> edges;
    for (const auto& l : sk.lines) {
        if (l.construction) continue;   // an осевая bounds profiles too, as in Inventor
        const gp_Pnt a = world(l.start), b = world(l.end);
        if (a.Distance(b) < 1e-6) continue;
        edges.push_back(BRepBuilderAPI_MakeEdge(a, b).Edge());
    }
    for (const auto& c : sk.circles) {
        if (c.construction || c.radius < 1e-6) continue;
        gp_Ax2 ax = axes;
        ax.SetLocation(world(c.center));
        edges.push_back(BRepBuilderAPI_MakeEdge(gp_Circ(ax, c.radius)).Edge());
    }
    for (const auto& a : sk.arcs) {
        if (a.construction) continue;
        const Vec2 center = sk.PositionOf(a.center);
        const Vec2 s = sk.PositionOf(a.start) - center;
        const Vec2 e = sk.PositionOf(a.end) - center;
        const double r = s.Length();
        if (r < 1e-6) continue;
        double a1 = std::atan2(s.y, s.x);
        double a2 = std::atan2(e.y, e.x);
        if (a2 <= a1) a2 += 2.0 * kPi;
        gp_Ax2 ax = axes;
        ax.SetLocation(world(a.center));
        edges.push_back(BRepBuilderAPI_MakeEdge(gp_Circ(ax, r), a1, a2).Edge());
    }
    for (const auto& s : sk.splines) {
        if (s.construction) continue;
        const sketch::BSpline b = sketch::SplineShape(sk, s);
        if (b.poles.size() < 2) continue;
        TColgp_Array1OfPnt poles(1, static_cast<int>(b.poles.size()));
        for (std::size_t i = 0; i < b.poles.size(); ++i) poles.SetValue(static_cast<int>(i) + 1, ToPnt(frame.ToWorld(b.poles[i])));
        TColStd_Array1OfReal knots(1, static_cast<int>(b.knots.size()));
        TColStd_Array1OfInteger mults(1, static_cast<int>(b.knots.size()));
        for (std::size_t i = 0; i < b.knots.size(); ++i) {
            knots.SetValue(static_cast<int>(i) + 1, b.knots[i]);
            mults.SetValue(static_cast<int>(i) + 1, b.mults[i]);
        }
        try {
            const Handle(Geom_BSplineCurve) curve = new Geom_BSplineCurve(poles, knots, mults, b.degree);
            BRepBuilderAPI_MakeEdge make(curve);
            if (make.IsDone()) edges.push_back(make.Edge());
        } catch (const Standard_Failure&) {
            // A degenerate spline (points on top of each other) makes no edge.
        }
    }
    for (const auto& e : sk.ellipses) {
        if (e.construction) continue;
        const Vec2 c = sk.PositionOf(e.center), u = sk.PositionOf(e.major) - c, v = sk.PositionOf(e.minor) - c;
        double a = u.Length(), b = v.Length();
        if (a < 1e-6 || b < 1e-6) continue;
        // OCCT wants the major radius first: the longer axis leads.
        Vec2 dir = u * (1.0 / a);
        if (b > a) {
            std::swap(a, b);
            dir = v * (1.0 / a);
        }
        const Vec3 xAxis = frame.xDir * dir.x + frame.YDir() * dir.y;
        const gp_Ax2 ax(world(e.center), ToDir(frame.normal), ToDir(xAxis));
        edges.push_back(BRepBuilderAPI_MakeEdge(gp_Elips(ax, a, b)).Edge());
    }
    for (const auto& t : sk.texts) {
        if (t.construction) continue;
        // The box: its bottom side gives the direction, the left side the height.
        const Vec2 bl = sk.PositionOf(t.corners[0]), br = sk.PositionOf(t.corners[1]), tl = sk.PositionOf(t.corners[3]);
        const double w = (br - bl).Length(), h = (tl - bl).Length();
        if (w < 1e-9) continue;
        const Vec2 u = (br - bl) * (1.0 / w);
        const TopoDS_Shape letters = TextFaces({t.text, t.height, t.font, t.bold, t.italic, t.hAlign, t.vAlign}, w, h);
        if (letters.IsNull()) continue;
        const Vec3 along = frame.xDir * u.x + frame.YDir() * u.y;
        gp_Trsf place;
        place.SetDisplacement(gp_Ax3(gp::XOY()), gp_Ax3(world(t.corners[0]), ToDir(frame.normal), ToDir(along)));
        const TopoDS_Shape placed = BRepBuilderAPI_Transform(letters, place, true).Shape();
        for (TopExp_Explorer e(placed, TopAbs_EDGE); e.More(); e.Next()) edges.push_back(TopoDS::Edge(e.Current()));
    }
    return edges;
}

/// Points along an edge in its orientation, mapped to sketch coordinates.
std::vector<Vec2> SampleEdge(const TopoDS_Edge& edge, const Frame& frame) {
    BRepAdaptor_Curve curve(edge);
    GCPnts_TangentialDeflection sampler(curve, 0.08, 0.005);
    std::vector<Vec2> pts;
    for (int i = 1; i <= sampler.NbPoints(); ++i) {
        const gp_Pnt p = sampler.Value(i);
        pts.push_back(frame.ToLocal({p.X(), p.Y(), p.Z()}));
    }
    if (edge.Orientation() == TopAbs_REVERSED) std::reverse(pts.begin(), pts.end());
    return pts;
}

std::vector<Vec2> SampleWire(const TopoDS_Wire& wire, const TopoDS_Face& face, const Frame& frame) {
    std::vector<Vec2> loop;
    for (BRepTools_WireExplorer it(wire, face); it.More(); it.Next()) {
        std::vector<Vec2> pts = SampleEdge(it.Current(), frame);
        if (!loop.empty() && !pts.empty()) pts.erase(pts.begin());   // shared vertex
        loop.insert(loop.end(), pts.begin(), pts.end());
    }
    if (loop.size() > 1 && (loop.front() - loop.back()).Length() < 1e-9) loop.pop_back();
    return loop;
}

/// Even-odd rule: how many boundary segments a ray from `p` to +X crosses.
bool InsideEvenOdd(Vec2 p, const std::vector<std::vector<Vec2>>& polylines) {
    bool inside = false;
    for (const auto& poly : polylines) {
        for (std::size_t i = 0; i + 1 < poly.size(); ++i) {
            const Vec2 a = poly[i], b = poly[i + 1];
            if ((a.y > p.y) == (b.y > p.y)) continue;
            const double x = a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y);
            if (x > p.x) inside = !inside;
        }
    }
    return inside;
}

bool TouchesBox(const TopoDS_Face& face, const Frame& frame, double limit) {
    for (TopExp_Explorer it(face, TopAbs_VERTEX); it.More(); it.Next()) {
        const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(it.Current()));
        const Vec2 local = frame.ToLocal({p.X(), p.Y(), p.Z()});
        if (std::abs(local.x) > limit || std::abs(local.y) > limit) return true;
    }
    return false;
}

} // namespace

Expected<TopoDS_Wire> BuildPath(const doc::SketchFeature& feature) {
    try {
        const std::vector<TopoDS_Edge> edges = MakeEdges(feature);
        if (edges.empty()) return Error{"В эскизе траектории нет линий"};
        Handle(TopTools_HSequenceOfShape) loose = new TopTools_HSequenceOfShape();
        for (const TopoDS_Edge& e : edges) loose->Append(e);
        Handle(TopTools_HSequenceOfShape) wires;
        ShapeAnalysis_FreeBounds::ConnectEdgesToWires(loose, 1e-6, false, wires);
        if (wires.IsNull() || wires->Length() != 1)
            return Error{"Траектория должна быть одной цепочкой линий и дуг без разветвлений"};
        return TopoDS::Wire(wires->Value(1));
    } catch (const Standard_Failure& failure) {
        return Error{std::format("Ошибка траектории: {}", failure.GetMessageString())};
    }
}

Expected<SketchProfile> BuildProfile(const doc::SketchFeature& feature) {
    SketchProfile profile;
    profile.frame = feature.placement.frame;
    const Frame& frame = profile.frame;

    try {
        const std::vector<TopoDS_Edge> edges = MakeEdges(feature);
        if (edges.empty()) return profile;

        // A large face on the sketch plane, split by all curves. The pieces
        // that do not touch its border are the closed regions.
        double extent = 10.0;
        for (const auto& p : feature.sketch.points)
            extent = std::max({extent, std::abs(p.pos.x), std::abs(p.pos.y)});
        for (const auto& c : feature.sketch.circles)
            extent = std::max(extent, (feature.sketch.PositionOf(c.center)).Length() + c.radius);
        const double half = extent * 4.0 + 100.0;
        const gp_Pln plane(gp_Ax3(ToPnt(frame.origin), ToDir(frame.normal), ToDir(frame.xDir)));
        const TopoDS_Face sheet = BRepBuilderAPI_MakeFace(plane, -half, half, -half, half).Face();

        NCollection_List<TopoDS_Shape> objects, tools;
        objects.Append(sheet);
        for (const auto& e : edges) tools.Append(e);
        BRepAlgoAPI_Splitter splitter;
        splitter.SetArguments(objects);
        splitter.SetTools(tools);
        splitter.Build();
        if (!splitter.IsDone()) return Error{"Не удалось найти замкнутые контуры эскиза"};

        Handle(IntTools_Context) context = new IntTools_Context();
        std::vector<std::vector<Vec2>> boundary;   // every distinct region edge once
        std::vector<TopoDS_Edge>       seenEdges;

        for (TopExp_Explorer it(splitter.Shape(), TopAbs_FACE); it.More(); it.Next()) {
            const TopoDS_Face face = TopoDS::Face(it.Current());
            if (TouchesBox(face, frame, half * 0.999)) continue;

            SketchRegion region;
            gp_Pnt inside;
            gp_Pnt2d insideUV;
            if (BOPTools_AlgoTools3D::PointInFace(face, inside, insideUV, context) != 0) continue;
            region.sample = frame.ToLocal({inside.X(), inside.Y(), inside.Z()});

            const TopoDS_Wire outer = BRepTools::OuterWire(face);
            region.loops.push_back(SampleWire(outer, face, frame));
            for (TopExp_Explorer w(face, TopAbs_WIRE); w.More(); w.Next())
                if (!w.Current().IsSame(outer)) region.loops.push_back(SampleWire(TopoDS::Wire(w.Current()), face, frame));

            for (TopExp_Explorer e(face, TopAbs_EDGE); e.More(); e.Next()) {
                const TopoDS_Edge edge = TopoDS::Edge(e.Current());
                const bool seen = std::any_of(seenEdges.begin(), seenEdges.end(),
                                              [&](const TopoDS_Edge& s) { return s.IsSame(edge); });
                if (seen) continue;
                seenEdges.push_back(edge);
                boundary.push_back(SampleEdge(edge, frame));
            }
            profile.faces.push_back(face);
            profile.regions.push_back(std::move(region));
        }

        for (auto& region : profile.regions) region.automatic = InsideEvenOdd(region.sample, boundary);
    } catch (const Standard_Failure& failure) {
        return Error{std::format("Ошибка геометрии эскиза: {}", failure.GetMessageString())};
    }
    return profile;
}

std::vector<TopoDS_Face> SelectFaces(const SketchProfile& profile, const doc::ProfileSelection& selection) {
    std::vector<TopoDS_Face> chosen;
    if (selection.automatic) {
        for (std::size_t i = 0; i < profile.faces.size(); ++i)
            if (profile.regions[i].automatic) chosen.push_back(profile.faces[i]);
        return chosen;
    }
    for (const Vec2 p : selection.regions) {
        const Vec3 w = profile.frame.ToWorld(p);
        for (const auto& face : profile.faces) {
            BRepClass_FaceClassifier classifier(face, gp_Pnt(w.x, w.y, w.z), 1e-6);
            if (classifier.State() != TopAbs_IN) continue;
            const bool already = std::any_of(chosen.begin(), chosen.end(), [&](const TopoDS_Face& f) { return f.IsSame(face); });
            if (!already) chosen.push_back(face);
            break;
        }
    }
    return chosen;
}

} // namespace bz::kernel::occt
