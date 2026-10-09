#pragma once

#include "document/Document.h"
#include "document/SketchFeature.h"

namespace bz::doc {

/// The document-side part of a rebuild: evaluates all parameters and
/// solves every active sketch, setting statuses of sketches. Solid
/// geometry is built afterwards by the kernel (kernel/Rebuild.h).
void RecomputeModel(Document& doc);

/// Solves one sketch and sets its analysis and status (RecomputeModel does
/// this for every sketch; the kernel again after moving projected geometry).
void SolveSketch(SketchFeature& sketch);

} // namespace bz::doc
