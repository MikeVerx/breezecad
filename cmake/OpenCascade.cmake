# Open CASCADE Technology. Supported: 7.6 … 8.0.
# Point CMake at an install with -DOpenCASCADE_DIR=<prefix>/cmake
# (Windows/MSYS2 example: C:/occt/install/cmake).

find_package(OpenCASCADE CONFIG REQUIRED)
message(STATUS "OpenCASCADE ${OpenCASCADE_VERSION}: ${OpenCASCADE_INCLUDE_DIR}")

# Modeling: everything the geometry kernel needs, no visualization.
add_library(bz_occt_modeling INTERFACE)
target_include_directories(bz_occt_modeling SYSTEM INTERFACE ${OpenCASCADE_INCLUDE_DIR})
# OCCT headers use M_PI. With strict C++20 (no GNU extensions) MinGW and MSVC
# declare it only when _USE_MATH_DEFINES is set; OCCT's own build sets it too.
target_compile_definitions(bz_occt_modeling INTERFACE _USE_MATH_DEFINES)
target_link_libraries(bz_occt_modeling INTERFACE
    TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo
    TKPrim TKBO TKBool TKFeat TKFillet TKOffset TKShHealing TKMesh)
add_library(bz::occt_modeling ALIAS bz_occt_modeling)

# Data exchange: STEP and STL. OCCT 7.8 renamed the toolkits to TKDE*.
add_library(bz_occt_exchange INTERFACE)
if(TARGET TKDESTEP)
    target_link_libraries(bz_occt_exchange INTERFACE TKDESTEP TKDESTL TKXSBase TKDE)
else()
    target_link_libraries(bz_occt_exchange INTERFACE TKSTEP TKSTEPBase TKSTEPAttr TKSTEP209 TKSTL TKXSBase)
endif()
target_link_libraries(bz_occt_exchange INTERFACE bz_occt_modeling)
add_library(bz::occt_exchange ALIAS bz_occt_exchange)

# Visualization: V3d viewer and the OpenGL driver.
add_library(bz_occt_visual INTERFACE)
target_link_libraries(bz_occt_visual INTERFACE TKService TKV3d TKOpenGl TKHLR bz_occt_modeling)
add_library(bz::occt_visual ALIAS bz_occt_visual)
