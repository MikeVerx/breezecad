# Third-party code is vendored in third_party/ with pinned versions
# (see third_party/README.md). Nothing is downloaded at configure time,
# so the project builds offline from a clean clone.

set(BZ_THIRD_PARTY ${PROJECT_SOURCE_DIR}/third_party)

# nlohmann/json — header-only JSON for the .bzc format.
add_library(bz_json INTERFACE)
target_include_directories(bz_json SYSTEM INTERFACE ${BZ_THIRD_PARTY}/nlohmann)
add_library(bz::json ALIAS bz_json)

# miniz — ZIP container for the .bzc format.
add_library(bz_miniz STATIC
    ${BZ_THIRD_PARTY}/miniz/miniz.c
    ${BZ_THIRD_PARTY}/miniz/miniz_tdef.c
    ${BZ_THIRD_PARTY}/miniz/miniz_tinfl.c
    ${BZ_THIRD_PARTY}/miniz/miniz_zip.c)
target_include_directories(bz_miniz SYSTEM PUBLIC ${BZ_THIRD_PARTY}/miniz)
set_target_properties(bz_miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(bz::miniz ALIAS bz_miniz)

# stb_image — reading pictures (textures, the splash picture).
add_library(bz_stb STATIC ${BZ_THIRD_PARTY}/stb/stb_image.c)
target_include_directories(bz_stb SYSTEM PUBLIC ${BZ_THIRD_PARTY}/stb)
set_target_properties(bz_stb PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(bz::stb ALIAS bz_stb)

# doctest — unit tests.
add_library(bz_doctest INTERFACE)
target_include_directories(bz_doctest SYSTEM INTERFACE ${BZ_THIRD_PARTY}/doctest)
add_library(bz::doctest ALIAS bz_doctest)

if(BZ_BUILD_APP)
    find_package(OpenGL REQUIRED)
    find_package(glfw3 3.3 REQUIRED)

    # Dear ImGui with GLFW + OpenGL3 backends.
    set(IMGUI_DIR ${BZ_THIRD_PARTY}/imgui)
    add_library(bz_imgui STATIC
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_demo.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
    target_include_directories(bz_imgui SYSTEM PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends ${IMGUI_DIR}/misc/cpp)
    target_compile_definitions(bz_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
    target_link_libraries(bz_imgui PUBLIC glfw OpenGL::GL)
    add_library(bz::imgui ALIAS bz_imgui)

    # lunasvg — rasterizes the SVG icon set.
    set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(${BZ_THIRD_PARTY}/lunasvg ${PROJECT_BINARY_DIR}/third_party/lunasvg EXCLUDE_FROM_ALL)
    set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED})

    # portable-file-dialogs — native open/save dialogs, header-only.
    add_library(bz_pfd INTERFACE)
    target_include_directories(bz_pfd SYSTEM INTERFACE ${BZ_THIRD_PARTY}/pfd)
    add_library(bz::pfd ALIAS bz_pfd)
endif()
