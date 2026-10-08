add_library(bz_sketch STATIC
    EquationSystem.cpp
    Sketch.cpp
    Solver.cpp)
target_link_libraries(bz_sketch PUBLIC bz::core)
bz_set_warnings(bz_sketch)
add_library(bz::sketch ALIAS bz_sketch)
