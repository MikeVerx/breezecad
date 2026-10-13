add_library(bz_ui STATIC
    Fonts.cpp
    GestureArt.cpp
    Icons.cpp
    Theme.cpp
    ViewCube.cpp
    Menus.cpp
    Widgets.cpp)
target_link_libraries(bz_ui
    PUBLIC  bz::core bz::imgui
    PRIVATE lunasvg::lunasvg glfw OpenGL::GL)
bz_set_warnings(bz_ui)
add_library(bz::ui ALIAS bz_ui)
