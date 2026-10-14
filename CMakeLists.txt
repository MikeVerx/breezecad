cmake_minimum_required(VERSION 3.24)

project(BreezeCAD
    VERSION 0.2.1.1
    DESCRIPTION "Free offline parametric CAD"
    LANGUAGES C CXX)
# What the program calls itself: the number above plus a stage ("-Beta";
# empty for a stable release). Releases from main are tagged after it
# (v0.2.1-beta) and their notes are docs/releases/<label in lower case>.md.
set(BZ_VERSION_SUFFIX "-Beta")
set(BZ_VERSION_LABEL "${PROJECT_VERSION}${BZ_VERSION_SUFFIX}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(APPLE)
    # OpenGL is deprecated on macOS (still there, and what OCCT draws with):
    # without this every gl* call is a warning.
    add_compile_definitions(GL_SILENCE_DEPRECATION)
    # The few Cocoa calls (touchpad gestures) are Objective-C++.
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(CMAKE_OBJCXX_EXTENSIONS OFF)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BZ_BUILD_APP   "Build the BreezeCAD desktop application" ON)
option(BZ_BUILD_TESTS "Build unit tests"                          ON)

# All binaries land in one folder so the app finds assets next to itself.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
# std::thread: glibc before 2.34 (the Linux package targets 2.28) keeps
# pthread_create in libpthread, which must then be linked explicitly.
find_package(Threads REQUIRED)
include(CompilerWarnings)
include(ThirdParty)
include(OpenCascade)

add_subdirectory(src)

if(BZ_BUILD_TESTS)
    enable_testing()
    add_subdirectory(examples/modules)
    add_subdirectory(tests)
endif()
