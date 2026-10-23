#pragma once
// Inside AssemblyModel: the parts it loaded (or holds from memory) and what
// it shows. Shared by AssemblyModel.cpp and AssemblyContext.cpp (projections
// across parts); OCCT types, so kernel only.

#include "kernel/AssemblyModel.h"
#include "kernel/occt/ModelImpl.h"

#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Trsf.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>

namespace bz::kernel {

/// A placement as an OCCT transformation (part coordinates to assembly).
inline gp_Trsf ToTrsf(const assembly::Placement& p) {
    const auto& c = p.rotation.col;
    gp_Trsf t;
    t.SetValues(c[0].x, c[1].x, c[2].x, p.translation.x,   //
                c[0].y, c[1].y, c[2].y, p.translation.y,   //
                c[0].z, c[1].z, c[2].z, p.translation.z);
    return t;
}

/// A seam: the edge where a cylinder, cone or sphere closes on itself (both
/// sides of it are the same face). Not a real edge of the part — the user
/// never sees it, so it is never picked or projected.
inline bool IsSeam(const TopoDS_Edge& edge, const TopTools_ListOfShape& faces) {
    if (BRep_Tool::Degenerated(edge)) return true;
    for (const TopoDS_Shape& f : faces)
        if (BRep_Tool::IsClosed(edge, TopoDS::Face(f))) return true;
    return false;
}

struct AssemblyModel::Impl {
    struct Part {
        std::filesystem::file_time_type time{};
        std::unique_ptr<LoadedPart>     loaded;
        std::string                     problem;
        bool                            reported = false;
        TopoDS_Shape                    shape;         ///< all bodies, in the part's coordinates
        TopoDS_Shape                    decorations;   ///< cosmetic threads
        std::uint64_t                   generation = 0;
        /// Edited in this program: shown from here, not from `loaded`.
        const doc::Document*            memoryDoc = nullptr;
        const Model*                    memoryModel = nullptr;
        std::uint64_t                   memoryRevision = ~std::uint64_t{0};

        const doc::Document* Doc() const { return memoryDoc ? memoryDoc : loaded ? &loaded->doc : nullptr; }
        const Model* PartModel() const { return memoryModel ? memoryModel : loaded ? &loaded->model : nullptr; }
        /// Takes the bodies of `model` as the part's shape.
        void TakeShape(const Model& model) {
            const auto& impl = model.GetImpl();
            BRep_Builder builder;
            TopoDS_Compound all, cosmetic;
            builder.MakeCompound(all);
            builder.MakeCompound(cosmetic);
            for (const Body& b : impl.bodies) builder.Add(all, b.shape);
            for (const auto& [id, d] : impl.decorations)
                if (!d.IsNull()) builder.Add(cosmetic, d);
            shape = all;
            decorations = cosmetic;
        }
    };
    std::map<std::string, Part> parts;   ///< by absolute path
    /// Parts edited in memory, by the same key: (document, model).
    std::map<std::string, std::pair<const doc::Document*, const Model*>> inMemory;
    double diskInterval = 0.0;   ///< the window sets ~1 s (AssemblyState)
    std::chrono::steady_clock::time_point lastDiskCheck{};
    std::unordered_map<ObjectId, std::string> partOf;   ///< component -> key in `parts`
    std::unordered_map<ObjectId, assembly::Placement> placed;   ///< where each was shown
    std::unordered_map<ObjectId, TopoDS_Shape>        sourceShown;   ///< the part shape each was shown from

    Model         own;
    Model*        shown = &own;
    std::uint64_t builtRevision = ~std::uint64_t{0};
    std::uint64_t partsGeneration = 0, builtGeneration = ~std::uint64_t{0};

    const Part* PartFor(ObjectId component) const {
        const auto k = partOf.find(component);
        if (k == partOf.end()) return nullptr;
        const auto p = parts.find(k->second);
        return p == parts.end() ? nullptr : &p->second;
    }
};

} // namespace bz::kernel
