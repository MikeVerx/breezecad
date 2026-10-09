add_library(bz_sketch STATIC
    EquationSystem.cpp
    Move.cpp
    Shapes.cpp
    Sketch.cpp
    Trim.cpp
    Solver.cpp)
target_link_libraries(bz_sketch PUBLIC bz::core)
bz_set_warnings(bz_sketch)
add_library(bz::sketch ALIAS bz_sketch)
