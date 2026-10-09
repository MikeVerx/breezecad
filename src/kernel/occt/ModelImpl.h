#pragma once
// OCCT side of kernel::Model. Included by kernel sources and the renderer.

#include "document/SketchSolidFeature.h"
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

/// What an operation added to or cut from the body, kept so patterns and
/// mirrors can copy it.
struct Tool {
    TopoDS_Shape   shape;
    doc::BooleanOp op = doc::BooleanOp::Join;   ///< Join or Cut
};

struct Model::Impl {
    /// Feature ids in the order the cache was built for.
    std::vector<ObjectId> cachedOrder;
    /// bodiesAfter[i]: bodies after feature i was applied.
    std::vector<std::vector<Body>> bodiesAfter;
    /// Regions of every sketch feature.
    std::unordered_map<ObjectId, occt::SketchProfile> sketches;
    /// Tools of the operations that can be copied, by feature id.
    std::unordered_map<ObjectId, Tool> tools;

    std::vector<Body>     bodies;   ///< final result (at the end-of-part marker)
    std::vector<BodyInfo> infos;
    std::uint64_t         revision = 0;

    /// Interferences of `bodies` and the shared solids, computed for `interferenceRevision`.
    std::vector<Interference> interferences;
    std::vector<TopoDS_Shape> overlaps;
    std::uint64_t             interferenceRevision = ~std::uint64_t{0};
};

} // namespace bz::kernel
