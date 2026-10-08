add_library(bz_core STATIC
    Expression.cpp
    Log.cpp
    Units.cpp)
target_include_directories(bz_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
bz_set_warnings(bz_core)
add_library(bz::core ALIAS bz_core)
