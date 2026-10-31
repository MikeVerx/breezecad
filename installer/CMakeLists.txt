# The Windows installer (BreezeCAD-Setup.exe) and the File Explorer previews
# (bz_thumbs.dll). Windows only; both are single files with no DLLs beside
# them (everything linked statically), so the installer runs from Downloads
# and the previews load into Explorer on any Windows 10/11.
#
#   -DBZ_SETUP_CHANNEL=latest      where the online installer looks for the
#                                   release: "latest" or a tag (next-build)

set(BZ_SETUP_CHANNEL "latest" CACHE STRING "Release the online installer downloads: latest or a tag")

# --- File Explorer previews -----------------------------------------------------
add_library(bz_thumbs SHARED thumbnails/Thumbnails.cpp thumbnails/Thumbnails.def)
target_include_directories(bz_thumbs PRIVATE thumbnails)
target_compile_definitions(bz_thumbs PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(bz_thumbs PRIVATE bz::miniz ole32 shlwapi windowscodecs uuid gdi32)
set_target_properties(bz_thumbs PROPERTIES PREFIX "" OUTPUT_NAME bz_thumbs)
if(MINGW)
    target_link_options(bz_thumbs PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# --- the installer --------------------------------------------------------------
set(SETUP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/setup)
set(BZ_ROOT ${PROJECT_SOURCE_DIR})
set(SETUP_STRINGS "")
file(GLOB SETUP_LANGUAGES RELATIVE ${SETUP_DIR}/strings ${SETUP_DIR}/strings/*.json)
foreach(f ${SETUP_LANGUAGES})
    get_filename_component(code ${f} NAME_WE)
    string(APPEND SETUP_STRINGS "STR_${code} RCDATA \"${SETUP_DIR}/strings/${f}\"\n")
endforeach()
string(REPLACE "." "," SETUP_VERSION_COMMAS "${PROJECT_VERSION}")
string(REGEX MATCHALL "," commas "${SETUP_VERSION_COMMAS}")
list(LENGTH commas n)
while(n LESS 3)
    string(APPEND SETUP_VERSION_COMMAS ",0")
    math(EXPR n "${n} + 1")
endwhile()
configure_file(setup/setup.rc.in ${CMAKE_CURRENT_BINARY_DIR}/setup.rc @ONLY)

set(IMGUI_DIR ${PROJECT_SOURCE_DIR}/third_party/imgui)
add_executable(bz_setup WIN32
    setup/main.cpp
    setup/Install.cpp
    setup/Payload.cpp
    setup/Strings.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/setup.rc)
target_include_directories(bz_setup SYSTEM PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_compile_definitions(bz_setup PRIVATE
    UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00 IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    BZ_SETUP_CHANNEL="${BZ_SETUP_CHANNEL}" BZ_SETUP_VERSION="${BZ_VERSION_LABEL}")
target_link_libraries(bz_setup PRIVATE bz::miniz bz::json bz::stb
    d3d11 d3dcompiler dxgi dwmapi winhttp bcrypt ole32 shell32 shlwapi uuid advapi32 gdi32 imm32)
set_target_properties(bz_setup PROPERTIES OUTPUT_NAME BreezeCAD-Setup)
if(MINGW)
    target_link_options(bz_setup PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()
