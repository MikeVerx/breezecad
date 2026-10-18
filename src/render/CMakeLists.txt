add_library(bz_render STATIC
    TexturedFaces.cpp
    Viewport.cpp)
target_link_libraries(bz_render
    PUBLIC  bz::core
    PRIVATE bz::kernel bz::occt_visual)
if(NOT ANDROID)
    target_link_libraries(bz_render PRIVATE glfw)   # glfwGetTime; Android has no GLFW
endif()
bz_set_warnings(bz_render)
add_library(bz::render ALIAS bz_render)
