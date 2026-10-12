#include "kernel/Topology.h"

#include "kernel/Model.h"
#include "kernel/occt/EdgeMatch.h"
#include "kernel/occt/FaceMatch.h"
#include "kernel/occt/Measure.h"
#include "kernel/occt/ModelImpl.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Sphere.hxx>
#include <gp_Torus.hxx>

#include <algorithm>
#include <cmath>

namespace bz::kernel {

namespace {

Vec3 ToVec(const gp_XYZ& v) { return {v.X(), v.Y(), v.Z()}; }

doc::SurfaceKind KindOf(GeomAbs_SurfaceType t) {
    switch (t) {
        case GeomAbs_Plane: return doc::SurfaceKind::Plane;
        case GeomAbs_Cylinder: return doc::SurfaceKind::Cylinder;
        case GeomAbs_Cone: return doc::SurfaceKind::Cone;
        case GeomAbs_Sphere: return doc::SurfaceKind::Sphere;
        case GeomAbs_Torus: return doc::SurfaceKind::Torus;
        default: return doc::SurfaceKind::Other;
    }
}

/// A point inside the face (not on a hole of it): the middle of its
/// parameter range when that is inside, otherwise the first inside point
/// of a grid over the range.
std::optional<Vec3> InsidePoint(const TopoDS_Face& face) {
    double u0, u1, v0, v1;
    BRepTools::UVBounds(face, u0, u1, v0, v1);
    const BRepAdaptor_Surface surface(face);
    const double tol = BRep_Tool::Tolerance(face);
    for (int n : {1, 5, 11}) {
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j) {
                const double u = u0 + (u1 - u0) * (i + 0.5) / n, v = v0 + (v1 - v0) * (j + 0.5) / n;
                BRepClass_FaceClassifier inside(face, gp_Pnt2d(u, v), tol);
                if (inside.State() == TopAbs_IN) return ToVec(surface.Value(u, v).XYZ());
            }
    }
    return std::nullopt;
}

/// Every face and edge of every body, in the order DescribeTopology numbers them.
struct Maps {
    std::vector<std::pair<const Body*, TopoDS_Face>> faces;
    std::vector<std::pair<const Body*, TopoDS_Edge>> edges;
};

Maps Collect(const Model& model) {
    Maps maps;
    for (const Body& body : model.GetImpl().bodies) {
        TopTools_IndexedMapOfShape faces, edges;
        TopExp::MapShapes(body.shape, TopAbs_FACE, faces);
        TopExp::MapShapes(body.shape, TopAbs_EDGE, edges);
        for (int i = 1; i <= faces.Extent(); ++i) maps.faces.emplace_back(&body, TopoDS::Face(faces(i)));
        for (int i = 1; i <= edges.Extent(); ++i) {
            const TopoDS_Edge& e = TopoDS::Edge(edges(i));
            if (!BRep_Tool::Degenerated(e)) maps.edges.emplace_back(&body, e);
        }
    }
    return maps;
}

FaceInfo DescribeFace(const Body& body, const TopoDS_Face& face) {
    FaceInfo f;
    f.body = body.id;
    const BRepAdaptor_Surface surface(face);
    f.kind = KindOf(surface.GetType());
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    f.area = props.Mass();
    Bnd_Box box;
    BRepBndLib::AddOptimal(face, box, false, false);
    if (!box.IsVoid()) {
        double x0, y0, z0, x1, y1, z1;
        box.Get(x0, y0, z0, x1, y1, z1);
        f.boxMin = {x0, y0, z0};
        f.boxMax = {x1, y1, z1};
    }
    f.point = InsidePoint(face).value_or(ToVec(props.CentreOfMass().XYZ()));
    if (const auto planar = occt::PlanarNormal(face)) {
        f.normal = *planar;
        f.ref = doc::FaceReference{*planar, f.point};
    } else {
        f.normal = occt::NormalAt(face, f.point).value_or(Vec3{0, 0, 1});
        f.ref = doc::FaceReference{f.normal, f.point, true};
    }
    const auto axisOf = [&](const gp_Ax1& ax, double radius) {
        const Vec3 o = ToVec(ax.Location().XYZ()), d = ToVec(ax.Direction().XYZ()).Normalized();
        const Vec3 at = f.point - o;
        Vec3 radial = at - d * at.Dot(d);
        radial = radial.Length() > 1e-9 ? radial.Normalized() : Vec3{};
        f.axis = doc::CylinderTouch{o, d, radius, radial};
        // A bore: the outward normal points at the axis.
        f.hole = radial.Length() > 0.5 && f.normal.Dot(radial) < 0;
    };
    switch (surface.GetType()) {
        case GeomAbs_Cylinder: {
            // The axis from where the face begins along it (not from the
            // surface's own origin, which a hole tool puts a little above the face).
            gp_Ax1 ax = surface.Cylinder().Axis();
            double u0 = 0, u1 = 0, v0 = 0, v1 = 0;
            BRepTools::UVBounds(face, u0, u1, v0, v1);
            if (std::isfinite(v0) && std::abs(v0) < 1e7) ax.SetLocation(ax.Location().Translated(gp_Vec(ax.Direction()) * v0));
            axisOf(ax, surface.Cylinder().Radius());
            break;
        }
        case GeomAbs_Cone: {
            const gp_Ax1 ax = surface.Cone().Axis();
            const Vec3 o = ToVec(ax.Location().XYZ()), d = ToVec(ax.Direction().XYZ()).Normalized();
            const Vec3 at = f.point - o;
            axisOf(ax, (at - d * at.Dot(d)).Length());
            break;
        }
        case GeomAbs_Sphere: {
            const gp_Sphere s = surface.Sphere();
            f.axis = doc::CylinderTouch{ToVec(s.Location().XYZ()), {0, 0, 1}, s.Radius(), {}};
            break;
        }
        case GeomAbs_Torus: axisOf(surface.Torus().Axis(), surface.Torus().MinorRadius()); break;
        default: break;
    }
    return f;
}

EdgeInfo DescribeEdge(const Body& body, const TopoDS_Edge& edge) {
    EdgeInfo e;
    e.body = body.id;
    e.ref = occt::DescribeEdge(edge, body.shape);
    e.kind = e.ref.curve;
    const BRepAdaptor_Curve curve(edge);
    e.length = GCPnts_AbscissaPoint::Length(curve);
    const double t0 = curve.FirstParameter(), t1 = curve.LastParameter();
    e.start = ToVec(curve.Value(t0).XYZ());
    e.end = ToVec(curve.Value(t1).XYZ());
    e.middle = ToVec(curve.Value((t0 + t1) / 2).XYZ());
    e.closed = BRep_Tool::IsClosed(edge) || (e.start - e.end).Length() < 1e-7;
    if (curve.GetType() == GeomAbs_Line) {
        e.direction = ToVec(curve.Line().Direction().XYZ()).Normalized();
    } else if (curve.GetType() == GeomAbs_Circle) {
        const gp_Circ c = curve.Circle();
        e.direction = ToVec(c.Axis().Direction().XYZ()).Normalized();
        e.centre = ToVec(c.Location().XYZ());
        e.radius = c.Radius();
    } else {
        e.direction = e.ref.direction;
    }
    return e;
}

} // namespace

double BoxDistance(const Vec3& lo, const Vec3& hi, const Vec3& p) {
    const auto out = [](double v, double a, double b) { return v < a ? a - v : v > b ? v - b : 0.0; };
    return Vec3{out(p.x, lo.x, hi.x), out(p.y, lo.y, hi.y), out(p.z, lo.z, hi.z)}.Length();
}

double EdgeDistance(const EdgeInfo& e, const Vec3& p) {
    double best = std::min({(e.start - p).Length(), (e.end - p).Length(), (e.middle - p).Length()});
    if (e.kind == doc::CurveKind::Line) {
        const Vec3 d = e.end - e.start;
        const double len2 = d.Dot(d);
        if (len2 > 1e-18) {
            const double t = std::clamp((p - e.start).Dot(d) / len2, 0.0, 1.0);
            best = std::min(best, (e.start + d * t - p).Length());
        }
    } else if (e.centre && e.radius > 0) {
        const Vec3 n = e.direction.Normalized();
        const Vec3 off = p - *e.centre;
        const double along = off.Dot(n);
        const Vec3 radial = off - n * along;
        if (radial.Length() > 1e-12) {
            // The nearest point of the whole circle; for an arc it counts
            // only when it lies on the arc's side (nearer the middle than the ends are).
            const Vec3 q = *e.centre + radial.Normalized() * e.radius;
            const double reach = std::max((e.start - e.middle).Length(), (e.end - e.middle).Length());
            if (e.closed || (q - e.middle).Length() <= reach + 1e-9) best = std::min(best, (q - p).Length());
        }
    }
    return best;
}

Topology DescribeTopology(const Model& model) {
    Topology t;
    try {
        const Maps maps = Collect(model);
        TopTools_IndexedMapOfShape faceIndex, edgeIndex;   // shape -> global index (1-based)
        for (const auto& [body, face] : maps.faces) {
            faceIndex.Add(face);
            FaceInfo f = DescribeFace(*body, face);
            f.index = static_cast<int>(t.faces.size());
            t.faces.push_back(std::move(f));
        }
        for (const auto& [body, edge] : maps.edges) {
            edgeIndex.Add(edge);
            EdgeInfo e = DescribeEdge(*body, edge);
            e.index = static_cast<int>(t.edges.size());
            t.edges.push_back(std::move(e));
        }
        // Which faces meet at each edge.
        for (const Body& body : model.GetImpl().bodies) {
            TopTools_IndexedDataMapOfShapeListOfShape ancestors;
            TopExp::MapShapesAndAncestors(body.shape, TopAbs_EDGE, TopAbs_FACE, ancestors);
            for (int i = 1; i <= ancestors.Extent(); ++i) {
                const int ei = edgeIndex.FindIndex(ancestors.FindKey(i));
                if (ei == 0) continue;
                for (const TopoDS_Shape& face : ancestors(i)) {
                    const int fi = faceIndex.FindIndex(face);
                    if (fi == 0) continue;
                    std::vector<int>& faces = t.edges[static_cast<std::size_t>(ei - 1)].faces;
                    if (std::find(faces.begin(), faces.end(), fi - 1) == faces.end()) faces.push_back(fi - 1);
                    std::vector<int>& edges = t.faces[static_cast<std::size_t>(fi - 1)].edges;
                    if (std::find(edges.begin(), edges.end(), ei - 1) == edges.end()) edges.push_back(ei - 1);
                }
            }
        }
        // A circle's axis points out of the flat face it bounds, as "Вставка"
        // takes it: a hole's rim up out of the top, a head's underside down.
        for (EdgeInfo& e : t.edges) {
            if (e.kind != doc::CurveKind::Circle) continue;
            for (int fi : e.faces) {
                const FaceInfo& f = t.faces[static_cast<std::size_t>(fi)];
                if (f.kind == doc::SurfaceKind::Plane && std::abs(f.normal.Dot(e.direction)) > 0.999) {
                    e.direction = f.normal;
                    break;
                }
            }
        }
    } catch (const Standard_Failure&) {
        return {};
    }
    return t;
}

std::optional<Measurement> MeasureBetween(const Model& model, bool aIsFace, int a, bool bIsFace, int b) {
    try {
        const Maps maps = Collect(model);
        const auto shapeOf = [&](bool isFace, int i) -> std::optional<TopoDS_Shape> {
            if (i < 0) return std::nullopt;
            const auto n = static_cast<std::size_t>(i);
            if (isFace) return n < maps.faces.size() ? std::optional<TopoDS_Shape>(maps.faces[n].second) : std::nullopt;
            return n < maps.edges.size() ? std::optional<TopoDS_Shape>(maps.edges[n].second) : std::nullopt;
        };
        const auto sa = shapeOf(aIsFace, a), sb = shapeOf(bIsFace, b);
        if (!sa || !sb) return std::nullopt;
        return occt::Measure(*sa, *sb);
    } catch (const Standard_Failure&) {
        return std::nullopt;
    }
}

} // namespace bz::kernel
