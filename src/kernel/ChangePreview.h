#pragma once
// What the preview of the window «Обнаружено изменение, затрагивающее другие
// детали» shows (ADR-0009): a part as triangles and edges, and the volume
// that changed between two versions of it. OCCT-free, for the UI.
//
// Everything here works on models of its own (rebuilt from document copies
// on a background thread): meshing writes into the shapes, so shapes the
// window's view shows are never meshed here.

#include "core/Math.h"
#include "kernel/Model.h"

#include <vector>

namespace bz::kernel {

struct PreviewMesh {
    std::vector<Vec3> triangles;   ///< three points per triangle, outward counter-clockwise
    std::vector<Vec3> segments;    ///< two points per edge segment
    bool Empty() const { return triangles.empty() && segments.empty(); }
};

/// The bodies of `model` as triangles (to within `deflection`, a share of
/// their size: 0.002 is fine for a preview) and their edges.
PreviewMesh MeshOf(const Model& model, double deflection);

/// Material `before` had and `after` has not, and the other way round
/// (BRepAlgoAPI_Cut both ways), meshed. `volume` (optional) gets its size, mm³.
PreviewMesh ChangedVolumeOf(const Model& before, const Model& after, double deflection, double* volume = nullptr);

} // namespace bz::kernel
