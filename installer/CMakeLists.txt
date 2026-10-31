# The installers: BreezeCAD-Setup.exe with the File Explorer previews
# (bz_thumbs.dll) on Windows, BreezeCAD-Setup-linux on Linux — the same pages
# (setup/main.cpp), each in its system's window and terms (Install.cpp,
# InstallLinux.cpp). Single files with nothing beside them: they run from
# Downloads on any Windows 10/11 or any desktop Linux from 2019 on.
#
#   -DBZ_SETUP_CHANNEL=latest      where the online installer looks for the
#                                   release: "latest" or a tag (next-build)
#   -DBZ_SETUP_STATIC_GLFW=ON      Linux: GLFW built from source into the
#                                   installer (CI), not the system's library

set(BZ_SETUP_CHANNEL "latest" CACHE STRING "Release the online installer downloads: latest or a tag")

if(WIN32)
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
endif()

if(UNIX AND NOT APPLE AND NOT ANDROID)

# --- Linux ------------------------------------------------------------------------
# The files Windows packs as resources, linked in by the assembler (.incbin).
set(SETUP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/setup)
set(res_names FONT_REGULAR FONT_MEDIUM FONT_SEMIBOLD FONT_MONO LIC_ru LIC_en PAIN1 PAIN2 PAIN3 PAIN4)
set(res_files
    ${PROJECT_SOURCE_DIR}/assets/fonts/IBMPlexSans-Regular.ttf ${PROJECT_SOURCE_DIR}/assets/fonts/IBMPlexSans-Medium.ttf
    ${PROJECT_SOURCE_DIR}/assets/fonts/IBMPlexSans-SemiBold.ttf ${PROJECT_SOURCE_DIR}/assets/fonts/IBMPlexMono-Regular.ttf
    ${SETUP_DIR}/license/ru.txt ${SETUP_DIR}/license/en.txt
    ${SETUP_DIR}/pain/1.jpg ${SETUP_DIR}/pain/2.jpg ${SETUP_DIR}/pain/3.jpg ${SETUP_DIR}/pain/4.jpg)
file(GLOB SETUP_LANGUAGES ${SETUP_DIR}/strings/*.json)
foreach(f ${SETUP_LANGUAGES})
    get_filename_component(code ${f} NAME_WE)
    list(APPEND res_names STR_${code})
    list(APPEND res_files ${f})
endforeach()
foreach(kind app part assembly module library project)
    list(APPEND res_names ICON_${kind})
    list(APPEND res_files ${CMAKE_CURRENT_SOURCE_DIR}/icons/png/${kind}.png)
endforeach()
set(res_asm "")
set(res_decl "")
set(res_table "")
list(LENGTH res_names n)
math(EXPR last "${n} - 1")
foreach(i RANGE ${last})
    list(GET res_names ${i} name)
    list(GET res_files ${i} file)
    string(APPEND res_asm "__asm__(\".section .rodata\\n.balign 16\\n.global bzr_${name}\\nbzr_${name}:\\n.incbin \\\"${file}\\\"\\n.global bzr_${name}_end\\nbzr_${name}_end:\\n.previous\\n\");\n")
    string(APPEND res_decl "extern \"C\" const unsigned char bzr_${name}[], bzr_${name}_end[];\n")
    string(APPEND res_table "    {\"${name}\", bzr_${name}, bzr_${name}_end},\n")
endforeach()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/resources.cpp.in
"// Made by installer/CMakeLists.txt: the installer's files, linked in.
#include \"Resources.h\"
#include <cstring>
${res_asm}
${res_decl}
namespace bz::setup {
namespace {
struct Entry { const char* name; const unsigned char* begin; const unsigned char* end; };
const Entry kEntries[] = {
${res_table}};
} // namespace
std::span<const std::uint8_t> EmbeddedResource(const std::string& name) {
    for (const Entry& e : kEntries)
        if (name == e.name) return {e.begin, static_cast<std::size_t>(e.end - e.begin)};
    return {};
}
} // namespace bz::setup
")
configure_file(${CMAKE_CURRENT_BINARY_DIR}/resources.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/resources.cpp COPYONLY)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/resources.cpp PROPERTIES OBJECT_DEPENDS "${res_files}")

option(BZ_SETUP_STATIC_GLFW "Build GLFW from source into the Linux installer" OFF)
if(BZ_SETUP_STATIC_GLFW)
    include(FetchContent)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)   # X11 (XWayland on Wayland desktops): fewer build dependencies
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_Declare(setup_glfw URL https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.zip)
    FetchContent_MakeAvailable(setup_glfw)
    set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED})
    set(SETUP_GLFW glfw)
else()
    find_package(glfw3 3.3 REQUIRED)
    set(SETUP_GLFW glfw)
endif()
find_package(OpenGL REQUIRED)

set(IMGUI_DIR ${PROJECT_SOURCE_DIR}/third_party/imgui)
add_executable(bz_setup
    setup/main.cpp
    setup/InstallLinux.cpp
    setup/Payload.cpp
    setup/Strings.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/resources.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(bz_setup PRIVATE setup)
target_include_directories(bz_setup SYSTEM PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_compile_definitions(bz_setup PRIVATE IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    BZ_SETUP_CHANNEL="${BZ_SETUP_CHANNEL}" BZ_SETUP_VERSION="${BZ_VERSION_LABEL}")
target_link_libraries(bz_setup PRIVATE bz::miniz bz::json bz::stb ${SETUP_GLFW} OpenGL::GL Threads::Threads ${CMAKE_DL_LIBS})
target_link_options(bz_setup PRIVATE -static-libgcc -static-libstdc++)
set_target_properties(bz_setup PROPERTIES OUTPUT_NAME BreezeCAD-Setup-linux)
endif()
