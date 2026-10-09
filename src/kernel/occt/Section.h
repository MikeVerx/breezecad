#pragma once

#include "core/Expected.h"
#include "core/Math.h"

#include <TopoDS_Shape.hxx>

#include <optional>

namespace bz::kernel::occt {

/// What is left of `body` on one side of `plane`: behind it (against the
/// normal), or in front when `keepFront`. Nullopt when nothing is left.
Expected<std::optional<TopoDS_Shape>> CutByPlane(const TopoDS_Shape& body, const Frame& plane, bool keepFront);

} // namespace bz::kernel::occt
