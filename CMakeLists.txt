cmake_minimum_required(VERSION 3.16)
project(BreezeCAD LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# По умолчанию — Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Принудительно -O2 для Release (избегаем UB, который проявляется на -O3)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")

# --- lunasvg ---
set(LUNASVG_ROOT "$ENV{HOME}/lunasvg/install")
include_directories(${LUNASVG_ROOT}/include)

# --- Поиск OCCT ---
set(OCCT_ROOT "$ENV{HOME}/occt/install")
find_package(OpenCASCADE REQUIRED
    PATHS "${OCCT_ROOT}/cmake"
    NO_DEFAULT_PATH
)
include_directories(${OpenCASCADE_INCLUDE_DIR})

# --- Поиск GLFW ---
find_package(glfw3 REQUIRED)

# --- Настройка ImGui ---
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/external/imgui")
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw)

# --- Наш исполняемый файл ---
add_executable(breezecad
    src/main.cpp
    src/app/Application.cpp
    src/app/IconManager.cpp
    src/app/Localization.cpp
    src/platform/FileDialog.cpp
    src/platform/glfw/GlfwWindow.cpp
    src/occt/OcctView.cpp
    src/doc/BreezeApplication.cpp
    src/doc/BreezeDocument.cpp
    external/stb_image_impl.cpp
)

target_include_directories(breezecad PRIVATE src external)

# --- Линкуем всё вместе ---
target_link_libraries(breezecad PRIVATE
    TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep
    TKPrim TKTopAlgo TKBO
    TKDESTEP
    TKDESTL
    TKV3d TKOpenGl TKService TKMesh
    TKCAF TKLCAF TKBin
    glfw
    imgui
    opengl32
    ${LUNASVG_ROOT}/lib/liblunasvg.a
    ${LUNASVG_ROOT}/lib/libplutovg.a
)

if(WIN32)
    target_link_libraries(breezecad PRIVATE comdlg32 psapi)
endif()

# --- Копируем DLL OCCT после сборки ---
add_custom_command(TARGET breezecad POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${OCCT_ROOT}/win64/gcc/bin"
    "$<TARGET_FILE_DIR:breezecad>"
)

# Копируем assets (иконки, шрифты) рядом с exe
add_custom_command(TARGET breezecad POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:breezecad>/assets"
)

set_target_properties(breezecad PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
