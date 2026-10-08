// STEP, STL and 3MF export of Model bodies.

#include "core/PathText.h"
#include "kernel/occt/ModelImpl.h"

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Static.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_Writer.hxx>
#include <Standard_Version.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>

#include <miniz.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <map>

namespace bz::kernel {
namespace {

struct Triangle {
    std::array<Vec3, 3> v;
};

/// All bodies as one compound.
TopoDS_Compound Compound(const std::vector<Body>& bodies) {
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    for (const Body& b : bodies) builder.Add(compound, b.shape);
    return compound;
}

/// Triangulates the bodies; triangles are outward-oriented.
std::vector<Triangle> Triangulate(const std::vector<Body>& bodies, MeshQuality quality) {
    std::vector<Triangle> triangles;
    for (const Body& body : bodies) {
        BRepMesh_IncrementalMesh mesher(body.shape, quality.chordal, false, quality.angular, true);
        for (TopExp_Explorer it(body.shape, TopAbs_FACE); it.More(); it.Next()) {
            const TopoDS_Face face = TopoDS::Face(it.Current());
            TopLoc_Location location;
            const Handle(Poly_Triangulation) mesh = BRep_Tool::Triangulation(face, location);
            if (mesh.IsNull()) continue;
            const gp_Trsf trsf = location.Transformation();
            const bool reversed = face.Orientation() == TopAbs_REVERSED;
            for (int t = 1; t <= mesh->NbTriangles(); ++t) {
                int n1, n2, n3;
                mesh->Triangle(t).Get(n1, n2, n3);
                if (reversed) std::swap(n2, n3);
                Triangle tri;
                const int nodes[3] = {n1, n2, n3};
                for (std::size_t k = 0; k < 3; ++k) {
                    const gp_Pnt p = mesh->Node(nodes[k]).Transformed(trsf);
                    tri.v[k] = {p.X(), p.Y(), p.Z()};
                }
                triangles.push_back(tri);
            }
        }
    }
    return triangles;
}

Status WriteBytes(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return Error{"Не удалось создать файл: " + ToUtf8(path)};
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) return Error{"Не удалось записать файл (диск заполнен?)"};
    return Status::Ok();
}

template <class T>
void AppendRaw(std::string& out, T value) {
    char buffer[sizeof(T)];
    std::memcpy(buffer, &value, sizeof(T));
    out.append(buffer, sizeof(T));
}

} // namespace

Status Model::ExportStep(const std::filesystem::path& path) const {
    if (myImpl->bodies.empty()) return Error{"Нет тел для экспорта"};
    try {
        STEPControl_Writer writer;
        Interface_Static::SetCVal("write.step.schema", "AP214IS");
        Interface_Static::SetCVal("write.step.unit", "MM");
        if (writer.Transfer(Compound(myImpl->bodies), STEPControl_AsIs) != IFSelect_RetDone)
            return Error{"Не удалось подготовить STEP"};
#if OCC_VERSION_HEX >= 0x070700
        // Our own stream: opened with the wide path on Windows and closed
        // here, so the file is never left locked by the writer.
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return Error{"Не удалось создать файл: " + ToUtf8(path)};
        if (writer.WriteStream(out) != IFSelect_RetDone || !out.flush())
            return Error{"Не удалось записать STEP: " + ToUtf8(path)};
#else
        // Older OCCT writes by name only; it expects UTF-8 on every platform.
        const std::string utf8 = ToUtf8(path);
        if (writer.Write(utf8.c_str()) != IFSelect_RetDone) return Error{"Не удалось записать STEP: " + utf8};
#endif
    } catch (const Standard_Failure& failure) {
        return Error{std::string("Ошибка экспорта STEP: ") + failure.GetMessageString()};
    }
    return Status::Ok();
}

Status Model::ExportStl(const std::filesystem::path& path, MeshQuality quality) const {
    if (myImpl->bodies.empty()) return Error{"Нет тел для экспорта"};
    const std::vector<Triangle> triangles = Triangulate(myImpl->bodies, quality);

    // Binary STL: 80-byte header, count, then normal + 3 vertices + attribute per triangle.
    std::string bytes(80, '\0');
    std::memcpy(bytes.data(), "BreezeCAD binary STL, units: mm", 31);
    AppendRaw(bytes, static_cast<std::uint32_t>(triangles.size()));
    for (const Triangle& t : triangles) {
        const Vec3 n = (t.v[1] - t.v[0]).Cross(t.v[2] - t.v[0]).Normalized();
        for (double c : {n.x, n.y, n.z}) AppendRaw(bytes, static_cast<float>(c));
        for (const Vec3& v : t.v)
            for (double c : {v.x, v.y, v.z}) AppendRaw(bytes, static_cast<float>(c));
        AppendRaw(bytes, std::uint16_t{0});
    }
    return WriteBytes(path, bytes);
}

Status Model::Export3mf(const std::filesystem::path& path, MeshQuality quality) const {
    if (myImpl->bodies.empty()) return Error{"Нет тел для экспорта"};

    // 3MF core: a ZIP with a content-types file, a relationship and the model XML.
    const std::string contentTypes =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>)"
        R"(</Types>)";
    const std::string rels =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>)"
        R"(</Relationships>)";

    std::string model =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        "\n"
        R"(<model unit="millimeter" xml:lang="ru-RU" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">)"
        "\n<metadata name=\"Application\">BreezeCAD</metadata>\n<resources>\n";
    std::string build = "<build>\n";
    int objectId = 1;
    for (const Body& body : myImpl->bodies) {
        const std::vector<Triangle> triangles = Triangulate({body}, quality);
        // Vertices are shared between triangles of one body by exact position.
        std::vector<Vec3> vertices;
        std::map<std::array<double, 3>, std::size_t> indexOf;
        std::string tris;
        const auto vertexIndex = [&](const Vec3& v) {
            const auto [it, inserted] = indexOf.try_emplace({v.x, v.y, v.z}, vertices.size());
            if (inserted) vertices.push_back(v);
            return it->second;
        };
        for (const Triangle& t : triangles)
            tris += std::format("<triangle v1=\"{}\" v2=\"{}\" v3=\"{}\"/>\n", vertexIndex(t.v[0]),
                                vertexIndex(t.v[1]), vertexIndex(t.v[2]));
        model += std::format("<object id=\"{}\" name=\"{}\" type=\"model\"><mesh><vertices>\n", objectId, body.name);
        for (const Vec3& v : vertices) model += std::format("<vertex x=\"{:.6f}\" y=\"{:.6f}\" z=\"{:.6f}\"/>\n", v.x, v.y, v.z);
        model += "</vertices><triangles>\n" + tris + "</triangles></mesh></object>\n";
        build += std::format("<item objectid=\"{}\"/>\n", objectId);
        ++objectId;
    }
    model += "</resources>\n" + build + "</build>\n</model>\n";

    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 64 * 1024)) return Error{"Не удалось подготовить 3MF"};
    const bool added =
        mz_zip_writer_add_mem(&zip, "[Content_Types].xml", contentTypes.data(), contentTypes.size(), MZ_DEFAULT_COMPRESSION) &&
        mz_zip_writer_add_mem(&zip, "_rels/.rels", rels.data(), rels.size(), MZ_DEFAULT_COMPRESSION) &&
        mz_zip_writer_add_mem(&zip, "3D/3dmodel.model", model.data(), model.size(), MZ_DEFAULT_COMPRESSION);
    void* buffer = nullptr;
    std::size_t size = 0;
    const bool finalized = added && mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size);
    const std::string bytes = finalized ? std::string(static_cast<const char*>(buffer), size) : std::string{};
    mz_zip_writer_end(&zip);
    if (!finalized) return Error{"Не удалось упаковать 3MF"};
    return WriteBytes(path, bytes);
}

} // namespace bz::kernel
