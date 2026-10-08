#include "kernel/occt/Fillet.h"

#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <Standard_Failure.hxx>

namespace bz::kernel::occt {
namespace {

/// Runs a fillet-like builder and checks the result.
template <class Builder, class AddEdge>
Expected<TopoDS_Shape> Build(const TopoDS_Shape& body, const std::vector<TopoDS_Edge>& edges, double size, AddEdge add,
                             const char* what, const char* tooBig) {
    if (edges.empty()) return Error{std::string("Выберите рёбра для операции «") + what + "»"};
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

} // namespace

Expected<TopoDS_Shape> MakeFillet(const TopoDS_Shape& body, const std::vector<TopoDS_Edge>& edges, double radius) {
    return Build<BRepFilletAPI_MakeFillet>(
        body, edges, radius, [radius](BRepFilletAPI_MakeFillet& b, const TopoDS_Edge& e) { b.Add(radius, e); }, "Скругление",
        "Не удалось построить скругление: радиус слишком большой для этих рёбер?");
}

Expected<TopoDS_Shape> MakeChamfer(const TopoDS_Shape& body, const std::vector<TopoDS_Edge>& edges, double distance) {
    return Build<BRepFilletAPI_MakeChamfer>(
        body, edges, distance, [distance](BRepFilletAPI_MakeChamfer& b, const TopoDS_Edge& e) { b.Add(distance, e); },
        "Фаска", "Не удалось построить фаску: расстояние слишком большое для этих рёбер?");
}

} // namespace bz::kernel::occt
