#include "kernel/occt/ShareSurfaces.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepTools.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepTools_Modification.hxx>
#include <BRepTools_Modifier.hxx>
#include <BRep_Tool.hxx>
#include <ElSLib.hxx>
#include <Geom2d_Curve.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <NCollection_DataMap.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2d.hxx>
#include <gp_Lin.hxx>
#include <gp_Trsf2d.hxx>

#include <cmath>
#include <numbers>
#include <vector>

namespace bz::kernel::occt {
namespace {

constexpr double kTolerance = 1e-6;   // mm: one axis, one radius

/// How a face's own parameters go over to the shared surface's:
/// u' = ±u + a, v' = ±v + b. One sign reversed and not the other: the
/// shared surface's normal is the face's turned round (its frame is of the
/// other hand) — the face is turned over to keep its side, and its wires.
struct Remap {
    Handle(Geom_Surface) surface;
    gp_Trsf2d            uv;
    bool                 over = false;
};

class ShareModification final : public BRepTools_Modification {
public:
    NCollection_DataMap<TopoDS_Shape, Remap, TopTools_ShapeMapHasher> faces;

    Standard_Boolean NewSurface(const TopoDS_Face& f, Handle(Geom_Surface)& s, TopLoc_Location& l, Standard_Real& tol,
                                Standard_Boolean& revWires, Standard_Boolean& revFace) override {
        const Remap* r = faces.Seek(f);
        if (!r) return Standard_False;
        s = r->surface;
        l = TopLoc_Location();
        tol = BRep_Tool::Tolerance(f);
        revWires = revFace = r->over ? Standard_True : Standard_False;
        return Standard_True;
    }
    Standard_Boolean NewCurve(const TopoDS_Edge&, Handle(Geom_Curve)&, TopLoc_Location&, Standard_Real&) override {
        return Standard_False;
    }
    Standard_Boolean NewPoint(const TopoDS_Vertex&, gp_Pnt&, Standard_Real&) override { return Standard_False; }
    Standard_Boolean NewCurve2d(const TopoDS_Edge& e, const TopoDS_Face& f, const TopoDS_Edge&, const TopoDS_Face&,
                                Handle(Geom2d_Curve)& c, Standard_Real& tol) override {
        const Remap* r = faces.Seek(f);
        if (!r) return Standard_False;
        double first = 0, last = 0;
        const Handle(Geom2d_Curve) own = BRep_Tool::CurveOnSurface(e, f, first, last);
        if (own.IsNull()) return Standard_False;
        c = Handle(Geom2d_Curve)::DownCast(own->Copy());
        c->Transform(r->uv);
        tol = BRep_Tool::Tolerance(e);
        return Standard_True;
    }
    Standard_Boolean NewParameter(const TopoDS_Vertex&, const TopoDS_Edge&, Standard_Real&, Standard_Real&) override {
        return Standard_False;
    }
    GeomAbs_Shape Continuity(const TopoDS_Edge& e, const TopoDS_Face& f1, const TopoDS_Face& f2, const TopoDS_Edge&,
                             const TopoDS_Face&, const TopoDS_Face&) override {
        return BRep_Tool::HasContinuity(e, f1, f2) ? BRep_Tool::Continuity(e, f1, f2) : GeomAbs_C0;
    }
};

double Wrapped(double a) {
    constexpr double pi = std::numbers::pi;
    while (a > pi) a -= 2 * pi;
    while (a <= -pi) a += 2 * pi;
    return a;
}

bool ClosesOnItself(const TopoDS_Face& face) {
    for (TopExp_Explorer e(face, TopAbs_EDGE); e.More(); e.Next())
        if (BRep_Tool::IsClosed(TopoDS::Edge(e.Current()), face)) return true;
    return false;
}

/// The map from `own` parameters to `shared` ones, for a face whose own
/// angles run `faceU0`..`faceU1`: shifted by whole turns so that the face's angles
/// stay within one turn from 0 (past 2π OCCT takes the face as broken).
bool MapOf(const gp_Cylinder& own, const gp_Cylinder& shared, double faceU0, double faceU1, Remap& out) {
    double a = 0, b = 0, u90 = 0, v90 = 0, u1 = 0, v1 = 0;
    ElSLib::Parameters(shared, ElSLib::Value(0, 0, own), a, b);
    ElSLib::Parameters(shared, ElSLib::Value(std::numbers::pi / 2, 0, own), u90, v90);
    ElSLib::Parameters(shared, ElSLib::Value(0, 1, own), u1, v1);
    const double du = Wrapped(u90 - a), dv = v1 - b;
    if (std::abs(std::abs(du) - std::numbers::pi / 2) > 1e-6 || std::abs(std::abs(dv) - 1) > 1e-6) return false;
    const bool flipU = du < 0, flipV = dv < 0;
    const double turn = 2 * std::numbers::pi;
    const double lowest = flipU ? a - faceU1 : a + faceU0;   // the face's smallest angle on the shared surface
    a -= std::floor((lowest + 1e-9) / turn) * turn;
    gp_Trsf2d map;
    map.SetTranslation(gp_Vec2d(a, b));
    gp_Trsf2d first;   // done before the move
    if (flipU && flipV) first.SetRotation(gp_Pnt2d(0, 0), std::numbers::pi);
    else if (flipU) first.SetMirror(gp_Ax2d(gp_Pnt2d(0, 0), gp_Dir2d(0, 1)));   // u -> -u
    else if (flipV) first.SetMirror(gp_Ax2d(gp_Pnt2d(0, 0), gp_Dir2d(1, 0)));   // v -> -v
    map.Multiply(first);
    out.uv = map;
    out.over = flipU != flipV;
    return true;
}

} // namespace

TopoDS_Shape ShareCylinders(const TopoDS_Shape& shape) {
    try {
        struct Group {
            gp_Cylinder               cylinder;
            Handle(Geom_Surface)      surface;
            std::vector<TopoDS_Face>  faces;
            bool                      differ = false;
        };
        std::vector<Group> groups;
        TopTools_IndexedMapOfShape all;
        TopExp::MapShapes(shape, TopAbs_FACE, all);
        for (int i = 1; i <= all.Extent(); ++i) {
            const TopoDS_Face face = TopoDS::Face(all(i));
            const BRepAdaptor_Surface a(face, Standard_False);
            if (a.GetType() != GeomAbs_Cylinder || ClosesOnItself(face)) continue;
            const gp_Cylinder c = a.Cylinder();
            const Handle(Geom_Surface) s = BRep_Tool::Surface(face);   // with the face's location
            Group* home = nullptr;
            for (Group& g : groups)
                if (std::abs(g.cylinder.Radius() - c.Radius()) < kTolerance &&
                    g.cylinder.Axis().Direction().IsParallel(c.Axis().Direction(), 1e-9) &&
                    gp_Lin(g.cylinder.Axis()).Distance(c.Location()) < kTolerance) {
                    home = &g;
                    break;
                }
            if (!home) {
                groups.push_back({c, s, {}, false});
                home = &groups.back();
            } else if (s != home->surface) {
                home->differ = true;
            }
            home->faces.push_back(face);
        }
        ShareModification* modification = new ShareModification;
        Handle(BRepTools_Modification) keep = modification;
        for (const Group& g : groups) {
            if (!g.differ || g.faces.size() < 2) continue;
            const Handle(Geom_Surface) shared = new Geom_CylindricalSurface(g.cylinder);
            for (const TopoDS_Face& face : g.faces) {
                Remap r{shared, {}, false};
                double u0 = 0, u1 = 0, v0 = 0, v1 = 0;
                BRepTools::UVBounds(face, u0, u1, v0, v1);
                if (MapOf(BRepAdaptor_Surface(face, Standard_False).Cylinder(), g.cylinder, u0, u1, r)) modification->faces.Bind(face, r);
            }
        }
        if (modification->faces.Extent() < 2) return shape;
        BRepTools_Modifier modifier(shape, keep);
        if (!modifier.IsDone()) return shape;
        // Each face given another surface is checked: one that came out
        // broken leaves the whole shape as it was (merely not merged).
        for (NCollection_DataMap<TopoDS_Shape, Remap, TopTools_ShapeMapHasher>::Iterator it(modification->faces); it.More(); it.Next())
            if (!BRepCheck_Analyzer(modifier.ModifiedShape(it.Key())).IsValid()) return shape;
        return modifier.ModifiedShape(shape);
    } catch (const Standard_Failure&) {
        return shape;
    }
}

} // namespace bz::kernel::occt
