# Audit probes (2026-10-13): robustness of file loading, expressions and the
# sketch solver, plus a solver benchmark. Standalone on purpose: needs no
# OCCT, builds in a minute, and is meant to run under ASan/UBSan.
#
#   cmake -S tests/audit -B build/audit -G Ninja
#   cmake --build build/audit
#   ASAN_OPTIONS=detect_leaks=0 build/audit/robustness_probes   # [CRASH]/[THROWS] = a bug (docs/audit/01-bugs.md)
#   build/audit/solver_bench                                    # release-like timings (O-01)
cmake_minimum_required(VERSION 3.24)
project(bz_audit C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(R ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
set(PROJECT_SOURCE_DIR ${R})
function(bz_set_warnings t)
endfunction()
find_package(Threads REQUIRED)   # bz_core links Threads::Threads
include(${R}/cmake/ThirdParty.cmake)

set(SAN -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
add_compile_options(-g -O1 ${SAN})
add_link_options(${SAN})
add_subdirectory(${R}/src/core core)
add_subdirectory(${R}/src/sketch sketch)
add_subdirectory(${R}/src/document document)
add_executable(robustness_probes robustness_probes.cpp)
target_link_libraries(robustness_probes bz::document bz::json bz::miniz)

# The benchmark is built without sanitizers and with -O2 from the sources directly.
file(GLOB BENCH_SKETCH ${R}/src/sketch/*.cpp)
add_executable(solver_bench solver_bench.cpp ${BENCH_SKETCH} ${R}/src/core/Units.cpp ${R}/src/core/Expression.cpp)
target_include_directories(solver_bench PRIVATE ${R}/src)
set_target_properties(solver_bench PROPERTIES COMPILE_OPTIONS "-O2;-DNDEBUG" LINK_OPTIONS "")
