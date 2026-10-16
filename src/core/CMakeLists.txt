# Which build this is (version, commit, date): refreshed at every build.
if(NOT DEFINED BZ_VERSION_LABEL)   # built on its own (tests/audit)
    set(BZ_VERSION_LABEL "${PROJECT_VERSION}")
endif()
set(BZ_BUILD_INFO ${PROJECT_BINARY_DIR}/generated/core/BuildInfo.h)
add_custom_target(bz_build_info
    COMMAND ${CMAKE_COMMAND} -DSRC=${PROJECT_SOURCE_DIR} -DOUT=${BZ_BUILD_INFO} -DVERSION=${BZ_VERSION_LABEL}
            -P ${PROJECT_SOURCE_DIR}/cmake/BuildInfo.cmake
    BYPRODUCTS ${BZ_BUILD_INFO}
    COMMENT "Build info"
    VERBATIM)

add_library(bz_core STATIC
    BuildInfo.cpp
    CrashCode.cpp
    Expression.cpp
    FaultGuard.cpp
    I18n.cpp
    Log.cpp
    Monitor.cpp
    Image.cpp
    Profile.cpp
    Units.cpp
    ViewProjection.cpp)
target_include_directories(bz_core PUBLIC ${PROJECT_SOURCE_DIR}/src PRIVATE ${PROJECT_BINARY_DIR}/generated)
add_dependencies(bz_core bz_build_info)
target_link_libraries(bz_core PUBLIC Threads::Threads PRIVATE bz::json bz::stb)
if(ANDROID)
    target_link_libraries(bz_core PRIVATE log)   # core/Log writes to logcat
endif()
bz_set_warnings(bz_core)
add_library(bz::core ALIAS bz_core)
