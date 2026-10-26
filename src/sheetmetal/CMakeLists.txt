# Sheet metal: bend rules, flange layout, flat pattern, DXF. Core only: no
# geometry kernel, so it survives a change of kernel (docs/ADR/0007-sheet-metal.md).
add_library(bz_sheetmetal STATIC
    Dxf.cpp
    DxfRead.cpp
    DxfTools.cpp
    DxfWrite.cpp
    SheetPart.cpp
    SheetRules.cpp)
target_link_libraries(bz_sheetmetal PUBLIC bz::core)
bz_set_warnings(bz_sheetmetal)
add_library(bz::sheetmetal ALIAS bz_sheetmetal)
