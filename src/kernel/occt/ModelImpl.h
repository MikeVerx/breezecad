#pragma once
// OCCT side of kernel::Model. Included by kernel sources and the renderer.

#include "kernel/Model.h"
#include "kernel/occt/SketchGeometry.h"

#include <TopoDS_Shape.hxx>

#include <unordered_map>
#include <vector>

namespace bz::kernel {

struct Body {
    ObjectId     id;
    std::string  name;
    TopoDS_Shape shape;
};

struct Model::Impl {
    /// Feature ids in the order the cache was built for.
    std::vector<ObjectId> cachedOrder;
    /// bodiesAfter[i]: bodies after feature i was applied.
    std::vector<std::vector<Body>> bodiesAfter;
    /// Regions of every sketch feature.
    std::unordered_map<ObjectId, occt::SketchProfile> sketches;

    std::vector<Body>     bodies;   ///< final result (at the end-of-part marker)
    std::vector<BodyInfo> infos;
    std::uint64_t         revision = 0;
};

} // namespace bz::kernel
