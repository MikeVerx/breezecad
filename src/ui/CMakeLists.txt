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
    PRIVATE lunasvg::lunasvg)
if(ANDROID)
    target_link_libraries(bz_ui PRIVATE GLESv3)
else()
    target_link_libraries(bz_ui PRIVATE glfw OpenGL::GL)
endif()
bz_set_warnings(bz_ui)
add_library(bz::ui ALIAS bz_ui)
