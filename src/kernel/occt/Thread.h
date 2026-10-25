#pragma once

#include "core/Expected.h"
#include "document/ThreadFeature.h"

#include <TopoDS_Shape.hxx>

#include <optional>

namespace bz::kernel::occt {

/// What a thread on a cylindrical face of `body` would be.
struct ThreadSite {
    bool   external = true;   ///< a shaft (the groove goes in), not a hole
    double nominal = 0;       ///< nominal diameter: the shaft, or the hole plus the thread depth
    double length = 0;        ///< length of the face along its axis
};

/// The cylindrical face `ref` refers to on `body`, described.
/// `standard` says how deep a hole's thread is (its nominal size from the hole).
Expected<ThreadSite> FindThreadSite(const TopoDS_Shape& body, const doc::CylinderTouch& ref,
                                    doc::ThreadStandard standard = doc::ThreadStandard::Metric);

/// What a thread does to the body: material added first (a chosen size
/// bigger than the shaft or smaller than the hole: the thread comes out
/// whole, not cut short), then the groove cut out.
struct ThreadShapes {
    std::optional<TopoDS_Shape> fill;
    /// The lead-in chamfers, cut before the groove (two simple booleans hold
    /// where one fused tool of a helix and a cone did not).
    std::optional<TopoDS_Shape> chamfer;
    TopoDS_Shape                cut;
};

/// The shapes of the thread of `feature` on `body`.
Expected<ThreadShapes> ThreadTool(const TopoDS_Shape& body, const doc::ThreadFeature& feature);

/// The groove of an internal thread `cut` (doc::CutOf) for a hole drilled
/// from `top` along `down` to the cut's surface, from above the face to
/// `length` below it. `throughEnd`: the hole goes on past `length` (the
/// groove runs out there), else it stops short of the bottom. The lead-in
/// chamfer of a hole is a countersink of its drill (occt/Hole.cpp).
Expected<TopoDS_Shape> HoleThreadGroove(Vec3 top, Vec3 down, const doc::ThreadCut& cut, double pitch, double length,
                                        bool leftHanded, bool throughEnd);

/// A cosmetic thread: the helix drawn on the face, nothing cut.
Expected<TopoDS_Shape> CosmeticThread(const TopoDS_Shape& body, const doc::ThreadFeature& feature);

} // namespace bz::kernel::occt
