add_library(bz_platform STATIC
    FileDialogs.cpp
    Launch.cpp
    Paths.cpp
    ProcessInfo.cpp
    SystemTheme.cpp
    Window.cpp)
target_link_libraries(bz_platform PUBLIC bz::core PRIVATE glfw OpenGL::GL bz::pfd)
if(WIN32)
    target_link_libraries(bz_platform PRIVATE shell32 ole32 advapi32 psapi)
elseif(APPLE)
    target_link_libraries(bz_platform PRIVATE "-framework CoreFoundation")
endif()
bz_set_warnings(bz_platform)
add_library(bz::platform ALIAS bz_platform)
