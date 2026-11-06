// Meshes for the preview of the change window (kernel/ChangePreview.h).
#include "kernel/ChangePreview.h"

#include "kernel/occt/ModelImpl.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GProp_GProps.hxx>
#include <Poly_Triangulation.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>

#include <cmath>

namespace bz::kernel {
namespace {

Vec3 ToVec(const gp_Pnt& p) { return {p.X(), p.Y(), p.Z()}; }

TopoDS_Shape AllBodies(const Model& model) {
    TopoDS_Compound all;
    BRep_Builder builder;
    builder.MakeCompound(all);
    for (const Body& b : model.GetImpl().bodies)
        if (!b.shape.IsNull()) builder.Add(all, b.shape);
    return all;
}

void Mesh(const TopoDS_Shape& shape, double deflection, PreviewMesh& out) {
    if (shape.IsNull()) return;
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    if (box.IsVoid()) return;
    deflection *= std::sqrt(box.SquareExtent());   // a share of the size: as fine for a screw as for a frame
    BRepMesh_IncrementalMesh(shape, deflection, false, 0.5, false);
    for (TopExp_Explorer e(shape, TopAbs_FACE); e.More(); e.Next()) {
        const TopoDS_Face& face = TopoDS::Face(e.Current());
        TopLoc_Location where;
        const Handle(Poly_Triangulation) t = BRep_Tool::Triangulation(face, where);
        if (t.IsNull()) continue;
        const gp_Trsf trsf = where.Transformation();
        const bool reversed = face.Orientation() == TopAbs_REVERSED;
        for (int i = 1; i <= t->NbTriangles(); ++i) {
            int a = 0, b = 0, c = 0;
            t->Triangle(i).Get(a, b, c);
            if (reversed) std::swap(b, c);
            for (int n : {a, b, c}) out.triangles.push_back(ToVec(t->Node(n).Transformed(trsf)));
        }
    }
    for (TopExp_Explorer e(shape, TopAbs_EDGE); e.More(); e.Next()) {
        const TopoDS_Edge& edge = TopoDS::Edge(e.Current());
        if (BRep_Tool::Degenerated(edge)) continue;
        BRepAdaptor_Curve curve(edge);
        if (curve.GetType() == GeomAbs_Line) {
            out.segments.push_back(ToVec(curve.Value(curve.FirstParameter())));
            out.segments.push_back(ToVec(curve.Value(curve.LastParameter())));
            continue;
        }
        GCPnts_QuasiUniformDeflection points(curve, deflection);
        if (!points.IsDone()) continue;
        for (int i = 1; i < points.NbPoints(); ++i) {
            out.segments.push_back(ToVec(points.Value(i)));
            out.segments.push_back(ToVec(points.Value(i + 1)));
        }
    }
}

} // namespace

PreviewMesh MeshOf(const Model& model, double deflection) {
    PreviewMesh out;
    try {
        Mesh(AllBodies(model), deflection, out);
    } catch (const Standard_Failure&) {
        out = {};
    }
    return out;
}

PreviewMesh ChangedVolumeOf(const Model& before, const Model& after, double deflection, double* volume) {
    PreviewMesh out;
    double size = 0;
    try {
        const TopoDS_Shape was = AllBodies(before), is = AllBodies(after);
        for (const auto& [from, minus] : {std::pair{was, is}, std::pair{is, was}}) {
            BRepAlgoAPI_Cut cut(from, minus);
            if (!cut.IsDone() || cut.Shape().IsNull()) continue;
            GProp_GProps props;
            BRepGProp::VolumeProperties(cut.Shape(), props);
            if (props.Mass() < 1e-6) continue;   // the same where it was: nothing to show
            size += props.Mass();
            Mesh(cut.Shape(), deflection, out);
        }
    } catch (const Standard_Failure&) {
        out = {};
        size = 0;
    }
    if (volume) *volume = size;
    return out;
}

} // namespace bz::kernel
