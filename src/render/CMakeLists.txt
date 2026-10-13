add_library(bz_render STATIC
    TexturedFaces.cpp
    Viewport.cpp)
target_link_libraries(bz_render
    PUBLIC  bz::core
    PRIVATE bz::kernel bz::occt_visual glfw)
bz_set_warnings(bz_render)
add_library(bz::render ALIAS bz_render)
