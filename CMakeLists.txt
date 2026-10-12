cmake_minimum_required(VERSION 3.24)

project(BreezeCAD
    VERSION 0.2.0
    DESCRIPTION "Free offline parametric CAD"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BZ_BUILD_APP   "Build the BreezeCAD desktop application" ON)
option(BZ_BUILD_TESTS "Build unit tests"                          ON)

# All binaries land in one folder so the app finds assets next to itself.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(CompilerWarnings)
include(ThirdParty)
include(OpenCascade)

add_subdirectory(src)

if(BZ_BUILD_TESTS)
    enable_testing()
    add_subdirectory(examples/modules)
    add_subdirectory(tests)
endif()
