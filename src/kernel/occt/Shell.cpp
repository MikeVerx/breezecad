#include "kernel/occt/Shell.h"

#include "kernel/occt/Extrude.h"

#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp_Face.hxx>
#include <BRep_Tool.hxx>
#include <Geom2d_Curve.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt2d.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepOffset_Mode.hxx>
#include <GeomAbs_JoinType.hxx>
#include <Standard_Failure.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <optional>

namespace bz::kernel::occt {
namespace {

constexpr double kTolerance = 1e-4;
constexpr const char* kFailed = "Не удалось построить оболочку: толщина слишком большая для этого тела?";
constexpr const char* kMixedEdges =
    "Оболочка не строится: у тела есть рёбра, вдоль которых грани переходят от угла к касанию "
    "(как у лофта квадрат→круг) — ядро OCCT такие рёбра не смещает. Обход: два лофта, "
    "внешний и внутренний (сечения меньше на толщину), и вырез внутреннего";

/// Angle between the normals of the two faces of `edge` at its parameter `t`.
double DihedralAt(const TopoDS_Edge& edge, const TopoDS_Face& a, const TopoDS_Face& b, double t) {
    const auto normal = [&](const TopoDS_Face& face) -> std::optional<gp_Dir> {
        double first = 0, last = 0;
        const Handle(Geom2d_Curve) uv = BRep_Tool::CurveOnSurface(edge, face, first, last);
        if (uv.IsNull()) return std::nullopt;
        const gp_Pnt2d p = uv->Value(t);
        BRepGProp_Face props(face);
        gp_Pnt at;
        gp_Vec n;
        props.Normal(p.X(), p.Y(), at, n);
        if (n.Magnitude() < 1e-12) return std::nullopt;
        return gp_Dir(n);
    };
    const auto na = normal(a), nb = normal(b);
    return na && nb ? na->Angle(*nb) : 0.0;
}

/// Whether the body has an edge whose faces meet at an angle at one place
/// and smoothly (tangent) at another: the inner offset of such an edge
/// needs a varying transition the OCCT offset cannot make.
bool HasMixedEdges(const TopoDS_Shape& body) {
    TopTools_IndexedDataMapOfShapeListOfShape facesOf;
    TopExp::MapShapesAndAncestors(body, TopAbs_EDGE, TopAbs_FACE, facesOf);
    constexpr double kSmooth = 1.0 * 3.14159265358979 / 180, kSharp = 10.0 * 3.14159265358979 / 180;
    for (int i = 1; i <= facesOf.Extent(); ++i) {
        const TopoDS_Edge& edge = TopoDS::Edge(facesOf.FindKey(i));
        const TopTools_ListOfShape& faces = facesOf.FindFromIndex(i);
        if (faces.Extent() != 2 || BRep_Tool::Degenerated(edge)) continue;
        const TopoDS_Face& a = TopoDS::Face(faces.First());
        const TopoDS_Face& b = TopoDS::Face(faces.Last());
        double first = 0, last = 0;
        if (BRep_Tool::Curve(edge, first, last).IsNull()) continue;
        double smallest = 10, largest = 0;
        for (int k = 0; k <= 8; ++k) {
            const double angle = DihedralAt(edge, a, b, first + (last - first) * k / 8);
            smallest = std::min(smallest, angle);
            largest = std::max(largest, angle);
        }
        if (smallest < kSmooth && largest > kSharp) return true;
    }
    return false;
}

/// The whole body offset by `distance` (negative: inside) with sharp corners.
Expected<TopoDS_Shape> Offset(const TopoDS_Shape& body, double distance) {
    BRepOffsetAPI_MakeOffsetShape offset;
    offset.PerformByJoin(body, distance, kTolerance, BRepOffset_Skin, false, false, GeomAbs_Intersection);
    if (!offset.IsDone() || offset.Shape().IsNull()) return Error{kFailed};
    return offset.Shape();
}

} // namespace

Expected<TopoDS_Shape> MakeShell(const TopoDS_Shape& body, const std::vector<TopoDS_Face>& open, double thickness, bool outward) {
    if (thickness <= 1e-6) return Error{"Толщина должна быть больше нуля"};
    // The OCCT offset cannot shift an edge whose faces go from a corner to
    // tangent along it (a square-to-circle loft). Its simple per-face offset
    // "succeeds" there but gives faces turned the wrong way, so it is not
    // used: the message says why and how to get the part another way.
    const auto failed = [&] { return Error{HasMixedEdges(body) ? kMixedEdges : kFailed}; };
    try {
        TopoDS_Shape result;
        if (open.empty()) {
            // Closed hollow: cut the inner offset from the body (or the body
            // from the outer offset).
            const Expected<TopoDS_Shape> other = Offset(body, outward ? thickness : -thickness);
            if (!other) return failed();
            const Expected<TopoDS_Shape> cut =
                outward ? ApplyBoolean(*other, body, doc::BooleanOp::Cut) : ApplyBoolean(body, *other, doc::BooleanOp::Cut);
            if (!cut) return Error{kFailed};
            result = *cut;
        } else {
            TopTools_ListOfShape faces;
            for (const TopoDS_Face& f : open) faces.Append(f);
            BRepOffsetAPI_MakeThickSolid solid;
            solid.MakeThickSolidByJoin(body, faces, outward ? thickness : -thickness, kTolerance, BRepOffset_Skin, false,
                                       false, GeomAbs_Intersection);
            if (!solid.IsDone() || solid.Shape().IsNull()) return failed();
            result = solid.Shape();
        }
        if (!BRepCheck_Analyzer(result).IsValid()) return Error{kFailed};
        return result;
    } catch (const Standard_Failure&) {
        return failed();
    }
}

} // namespace bz::kernel::occt
