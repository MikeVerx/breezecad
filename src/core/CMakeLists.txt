# Which build this is (version, commit, date): refreshed at every build.
set(BZ_BUILD_INFO ${PROJECT_BINARY_DIR}/generated/core/BuildInfo.h)
add_custom_target(bz_build_info
    COMMAND ${CMAKE_COMMAND} -DSRC=${PROJECT_SOURCE_DIR} -DOUT=${BZ_BUILD_INFO} -DVERSION=${PROJECT_VERSION}
            -P ${PROJECT_SOURCE_DIR}/cmake/BuildInfo.cmake
    BYPRODUCTS ${BZ_BUILD_INFO}
    COMMENT "Build info"
    VERBATIM)

add_library(bz_core STATIC
    BuildInfo.cpp
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
bz_set_warnings(bz_core)
add_library(bz::core ALIAS bz_core)
