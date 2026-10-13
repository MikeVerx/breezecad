add_library(bz_kernel STATIC
    AssemblyModel.cpp
    AssemblyReadApi.cpp
    AutoDrop.cpp
    ComponentPattern.cpp
    Model.cpp
    occt/Coil.cpp
    occt/Section.cpp
    occt/EdgeMatch.cpp
    occt/Export.cpp
    occt/Extrude.cpp
    occt/FaceMatch.cpp
    occt/Fillet.cpp
    occt/Hole.cpp
    occt/Loft.cpp
    occt/Measure.cpp
    occt/Points.cpp
    occt/Project.cpp
    occt/Revolve.cpp
    occt/Shell.cpp
    occt/SketchGeometry.cpp
    occt/Sweep.cpp
    occt/Text.cpp
    occt/Thread.cpp
    occt/Topology.cpp)
target_link_libraries(bz_kernel
    PUBLIC  bz::document bz::assembly bz::occt_modeling
    PRIVATE bz::occt_exchange bz::miniz TKService TKV3d)   # TKV3d: text outlines on OCCT 7.x
bz_set_warnings(bz_kernel)
add_library(bz::kernel ALIAS bz_kernel)
