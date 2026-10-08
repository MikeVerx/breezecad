#pragma once

#include "document/Document.h"

namespace bz::doc {

/// The document-side part of a rebuild: evaluates all parameters and
/// solves every active sketch, setting statuses of sketches. Solid
/// geometry is built afterwards by the kernel (kernel/Rebuild.h).
void RecomputeModel(Document& doc);

} // namespace bz::doc
