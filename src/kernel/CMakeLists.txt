add_library(bz_kernel STATIC
    Model.cpp
    occt/Draft.cpp
    occt/EdgeMatch.cpp
    occt/Export.cpp
    occt/Extrude.cpp
    occt/FaceMatch.cpp
    occt/Fillet.cpp
    occt/Hole.cpp
    occt/Loft.cpp
    occt/Measure.cpp
    occt/Project.cpp
    occt/Revolve.cpp
    occt/Shell.cpp
    occt/SketchGeometry.cpp
    occt/Sweep.cpp)
target_link_libraries(bz_kernel
    PUBLIC  bz::document bz::occt_modeling
    PRIVATE bz::occt_exchange bz::miniz)
bz_set_warnings(bz_kernel)
add_library(bz::kernel ALIAS bz_kernel)
