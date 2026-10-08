add_library(bz_kernel STATIC
    Model.cpp
    occt/Export.cpp
    occt/Extrude.cpp
    occt/SketchGeometry.cpp)
target_link_libraries(bz_kernel
    PUBLIC  bz::document bz::occt_modeling
    PRIVATE bz::occt_exchange bz::miniz)
bz_set_warnings(bz_kernel)
add_library(bz::kernel ALIAS bz_kernel)
