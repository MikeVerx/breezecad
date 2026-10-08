#pragma once
// Sketch -> OCCT: edges in model space and the closed regions they bound.

#include "core/Expected.h"
#include "document/SketchSolidFeature.h"
#include "document/SketchFeature.h"
#include "kernel/Model.h"

#include <TopoDS_Face.hxx>

#include <vector>

namespace bz::kernel::occt {

/// Closed regions of one sketch. `faces[i]` and `regions[i]` describe the
/// same region: an OCCT face in model space and its 2D outline.
struct SketchProfile {
    Frame                     frame;
    std::vector<TopoDS_Face>  faces;
    std::vector<SketchRegion> regions;
};

/// Finds every minimal closed region formed by the sketch's (non-construction)
/// curves, including regions with holes. Intersecting curves are split, so a
/// line across a rectangle makes two regions.
Expected<SketchProfile> BuildProfile(const doc::SketchFeature& sketch);

/// Faces chosen by a profile selection: the automatic set, or the regions
/// that contain the selection's sample points.
std::vector<TopoDS_Face> SelectFaces(const SketchProfile& profile, const doc::ProfileSelection& selection);

} // namespace bz::kernel::occt
