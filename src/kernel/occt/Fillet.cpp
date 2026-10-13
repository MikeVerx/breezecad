#include "kernel/occt/Fillet.h"

#include <BRepCheck_Analyzer.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <Geom2d_Curve.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopoDS.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <Standard_Failure.hxx>
#include <format>

#include <algorithm>
#include <array>
#include <cmath>

namespace bz::kernel::occt {
namespace {

/// Runs a fillet-like builder and checks the result.
template <class Builder, class AddEdge>
Expected<TopoDS_Shape> Build(const TopoDS_Shape& body, const std::vector<TopoDS_Edge>& edges, double size, AddEdge add,
                             const char* what, const char* tooBig) {
    if (edges.empty()) return Error{std::format("Выберите рёбра для операции «{}»", what)};
    if (size <= 1e-6) return Error{"Размер должен быть больше нуля"};
    try {
        Builder builder(body);
        for (const TopoDS_Edge& e : edges) add(builder, e);
        builder.Build();
        if (!builder.IsDone() || builder.Shape().IsNull()) return Error{tooBig};
        const TopoDS_Shape result = builder.Shape();
        if (!BRepCheck_Analyzer(result).IsValid()) return Error{tooBig};
        return result;
    } catch (const Standard_Failure&) {
        return Error{tooBig};
    }
}

/// Outward normal of `face` where `edge` runs along it, at the edge's middle.
gp_Dir NormalAlong(const TopoDS_Edge& edge, const TopoDS_Face& face) {
    double first = 0, last = 0;
    const Handle(Geom2d_Curve) pcurve = BRep_Tool::CurveOnSurface(edge, face, first, last);
    if (pcurve.IsNull()) return gp_Dir(0, 0, 1);
    const gp_Pnt2d uv = pcurve->Value((first + last) / 2);
    const BRepAdaptor_Surface surface(face);
    BRepLProp_SLProps props(surface, uv.X(), uv.Y(), 1, 1e-7);
    if (!props.IsNormalDefined()) return gp_Dir(0, 0, 1);
    gp_Dir n = props.Normal();
    if (face.Orientation() == TopAbs_REVERSED) n.Reverse();
    return n;
}

/// The reference face of `edge` for unequal chamfer sizes (see MakeChamfer).
TopoDS_Face ReferenceFace(const TopoDS_Edge& edge, const TopTools_IndexedDataMapOfShapeListOfShape& faces, bool flipped) {
    const TopTools_ListOfShape& around = faces.FindFromKey(edge);
    std::vector<TopoDS_Face> two;
    for (const TopoDS_Shape& f : around)
        if (std::ranges::none_of(two, [&](const TopoDS_Face& t) { return t.IsSame(f); })) two.push_back(TopoDS::Face(f));
    if (two.size() < 2) return two.empty() ? TopoDS_Face() : two.front();
    const auto key = [&](const TopoDS_Face& f) {
        const gp_Dir n = NormalAlong(edge, f);
        return std::array<double, 3>{n.Z(), n.Y(), n.X()};
    };
    const auto a = key(two[0]), b = key(two[1]);
    bool firstIsUpper = true;
    for (std::size_t i = 0; i < 3; ++i)
        if (std::abs(a[i] - b[i]) > 1e-6) {
            firstIsUpper = a[i] > b[i];
            break;
        }
    return firstIsUpper != flipped ? two[0] : two[1];
}

} // namespace

Expected<TopoDS_Shape> MakeFillet(const TopoDS_Shape& body, const std::vector<TopoDS_Edge>& edges, double radius) {
    return Build<BRepFilletAPI_MakeFillet>(
        body, edges, radius, [radius](BRepFilletAPI_MakeFillet& b, const TopoDS_Edge& e) { b.Add(radius, e); }, "Сопряжение",
        "Не удалось построить сопряжение: радиус слишком большой для этих рёбер?");
}

Expected<TopoDS_Shape> MakeChamfer(const TopoDS_Shape& body, const std::vector<TopoDS_Edge>& edges, double distance) {
    return Build<BRepFilletAPI_MakeChamfer>(
        body, edges, distance, [distance](BRepFilletAPI_MakeChamfer& b, const TopoDS_Edge& e) { b.Add(distance, e); },
        "Фаска", "Не удалось построить фаску: расстояние слишком большое для этих рёбер?");
}

Expected<TopoDS_Shape> MakeChamfer(const TopoDS_Shape& body, const std::vector<TopoDS_Edge>& edges, const ChamferSize& size) {
    using doc::ChamferMode;
    if (size.mode == ChamferMode::EqualDistance) return MakeChamfer(body, edges, size.distance);
    if (size.mode == ChamferMode::TwoDistances && size.distance2 <= 1e-6) return Error{"Размер должен быть больше нуля"};
    if (size.mode == ChamferMode::DistanceAngle && (size.angle <= 1e-6 || size.angle >= kPi / 2 - 1e-6))
        return Error{"Угол фаски должен быть больше 0° и меньше 90°"};
    TopTools_IndexedDataMapOfShapeListOfShape faces;
    TopExp::MapShapesAndAncestors(body, TopAbs_EDGE, TopAbs_FACE, faces);
    return Build<BRepFilletAPI_MakeChamfer>(
        body, edges, size.distance,
        [&](BRepFilletAPI_MakeChamfer& b, const TopoDS_Edge& e) {
            if (!faces.Contains(e)) return;
            const TopoDS_Face face = ReferenceFace(e, faces, size.flipped);
            if (face.IsNull()) return;
            if (size.mode == ChamferMode::TwoDistances) b.Add(size.distance, size.distance2, e, face);
            else b.AddDA(size.distance, size.angle, e, face);
        },
        "Фаска", "Не удалось построить фаску: размер слишком большой для этих рёбер?");
}

} // namespace bz::kernel::occt
