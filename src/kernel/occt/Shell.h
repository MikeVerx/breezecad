#pragma once

#include "core/Expected.h"
#include "document/ShellFeature.h"

#include <TopoDS_Shape.hxx>

namespace bz::kernel::occt {

/// `body` hollowed out as `feature` says; its open faces are found again by
/// normal and point (doc::FaceReference).
Expected<TopoDS_Shape> MakeShell(const TopoDS_Shape& body, const doc::ShellFeature& feature);

} // namespace bz::kernel::occt
