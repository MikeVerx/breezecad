#pragma once
// The flat pattern as a drawing for laser and punch shops: layers CUT
// (outline and holes) and BEND (bend centre lines and their notes), saved as
// DXF R12 that AutoCAD opens (DxfDrawing.h).

#include "sheetmetal/DxfDrawing.h"
#include "sheetmetal/SheetPart.h"

#include <string>

namespace bz::sheet {

dxf::Drawing FlatPatternDrawing(const FlatPattern& flat);
std::string  FlatPatternDxf(const FlatPattern& flat);

} // namespace bz::sheet
