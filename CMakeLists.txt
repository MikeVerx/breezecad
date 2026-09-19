cmake_minimum_required(VERSION 3.16)
project(OrdoCAD LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

# --- Путь к установленному OCCT ---
set(OCCT_ROOT "$ENV{HOME}/occt/install")

# --- Находим OCCT через его конфиг (он лежит в install/cmake) ---
find_package(OpenCASCADE REQUIRED
    PATHS "${OCCT_ROOT}/cmake"
    NO_DEFAULT_PATH
)

# find_package сам установит OpenCASCADE_INCLUDE_DIR и OpenCASCADE_LIBRARIES
include_directories(${OpenCASCADE_INCLUDE_DIR})

# --- Наш исполняемый файл ---
add_executable(ordocad src/main.cpp)

# --- Какие модули OCCT линкуем ---
target_link_libraries(ordocad PRIVATE
    TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep
    TKPrim TKTopAlgo TKBO
    TKDESTEP
)

# --- Чтобы Windows нашёл DLL при запуске ---
set_target_properties(ordocad PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)