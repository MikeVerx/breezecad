add_library(bz_platform STATIC
    FileDialogs.cpp
    Launch.cpp
    Paths.cpp
    ProcessInfo.cpp
    SystemInfo.cpp
    SystemTheme.cpp
    Touchpad.cpp
    Window.cpp)
target_link_libraries(bz_platform PUBLIC bz::core PRIVATE glfw OpenGL::GL bz::pfd)
if(WIN32)
    target_link_libraries(bz_platform PRIVATE shell32 ole32 advapi32 psapi)
elseif(APPLE)
    # Touchpad gestures come from Cocoa (Objective-C++, ARC).
    target_sources(bz_platform PRIVATE Touchpad_mac.mm)
    set_source_files_properties(Touchpad_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(bz_platform PRIVATE "-framework CoreFoundation" "-framework Cocoa")
else()
    # Touchpad gestures: XInput 2 (libXi) on a connection of our own.
    find_package(X11 REQUIRED)
    if(NOT TARGET X11::Xi)
        message(FATAL_ERROR "libXi (XInput 2) not found: install libxi-dev (Debian/Ubuntu) or libXi-devel (Fedora)")
    endif()
    target_link_libraries(bz_platform PRIVATE X11::X11 X11::Xi)
endif()
bz_set_warnings(bz_platform)
add_library(bz::platform ALIAS bz_platform)
