#pragma once

#include "core/Expected.h"

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>

#include <vector>

namespace bz::kernel::occt {

/// The solid swept by `faces` along `path` (before any boolean).
Expected<TopoDS_Shape> MakeSweep(const std::vector<TopoDS_Face>& faces, const TopoDS_Wire& path);

} // namespace bz::kernel::occt
