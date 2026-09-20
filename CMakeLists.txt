cmake_minimum_required(VERSION 3.16)
project(BreezeCAD LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

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
    src/app/Localization.cpp
    src/platform/FileDialog.cpp
    src/platform/glfw/GlfwWindow.cpp
    src/occt/OcctView.cpp
    src/doc/BreezeApplication.cpp
    src/doc/BreezeDocument.cpp
)

target_include_directories(breezecad PRIVATE src)

# --- Линкуем всё вместе ---
target_link_libraries(breezecad PRIVATE
    TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep
    TKPrim TKTopAlgo TKBO
    TKDESTEP
    TKV3d TKOpenGl TKService TKMesh
    TKCAF TKLCAF TKBin
    glfw
    imgui
    opengl32
)

if(WIN32)
    target_link_libraries(breezecad PRIVATE comdlg32)
endif()

# --- Копируем DLL OCCT после сборки ---
add_custom_command(TARGET breezecad POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${OCCT_ROOT}/win64/gcc/bin"
    "$<TARGET_FILE_DIR:breezecad>"
)

set_target_properties(breezecad PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
