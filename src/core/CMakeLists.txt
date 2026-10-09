add_library(bz_core STATIC
    Expression.cpp
    I18n.cpp
    Log.cpp
    Units.cpp
    ViewProjection.cpp)
target_include_directories(bz_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(bz_core PRIVATE bz::json)
bz_set_warnings(bz_core)
add_library(bz::core ALIAS bz_core)
