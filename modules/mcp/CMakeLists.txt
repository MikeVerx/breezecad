# MCP server module (bz-mcp) and its stdio bridge (bz-mcp-bridge): a module
# package, not a part of the program. Built into <build>/module-packages/mcp/ with a
# copy of module.json — not into <bin>/modules, where the program would take
# it for a module shipped with it — and packed into
# <build>/packages/bz-mcp-<version>-<system>-<arch>.bzmod: the file users
# install in "Настройки → Модули". The module uses only the C ABI
# (bz_module.h); the bridge shares platform/Paths.cpp with the program to
# find the settings folder in the same place.
find_package(Threads REQUIRED)

set(BZ_MCP_DIR ${PROJECT_BINARY_DIR}/module-packages/mcp)

add_library(bz_mcp_module SHARED McpModule.cpp Net.cpp Connection.cpp Protocol.cpp)
target_include_directories(bz_mcp_module PRIVATE ${PROJECT_SOURCE_DIR}/src/modules)
target_link_libraries(bz_mcp_module PRIVATE bz::json Threads::Threads)
set_target_properties(bz_mcp_module PROPERTIES
    OUTPUT_NAME bz-mcp
    CXX_VISIBILITY_PRESET hidden
    LIBRARY_OUTPUT_DIRECTORY ${BZ_MCP_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${BZ_MCP_DIR}
    ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/modules-lib)   # the import library: not in the package
bz_set_warnings(bz_mcp_module)
# module.json and README go in when the package is packed (below): copied
# after a build of the library only, a changed description or version would
# not reach a package whose code did not change.

add_executable(bz_mcp_bridge Bridge.cpp Net.cpp Connection.cpp Protocol.cpp ${PROJECT_SOURCE_DIR}/src/platform/Paths.cpp)
target_include_directories(bz_mcp_bridge PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(bz_mcp_bridge PRIVATE bz::json)
set_target_properties(bz_mcp_bridge PROPERTIES OUTPUT_NAME bz-mcp-bridge RUNTIME_OUTPUT_DIRECTORY ${BZ_MCP_DIR})
bz_set_warnings(bz_mcp_bridge)

if(WIN32)
    set_target_properties(bz_mcp_module PROPERTIES PREFIX "")   # bz-mcp.dll, as module.json expects
    target_link_libraries(bz_mcp_module PRIVATE ws2_32)
    target_link_libraries(bz_mcp_bridge PRIVATE ws2_32 shell32 ole32)
    if(MINGW)
        # The folder is not next to the program: carry the C++ runtime inside,
        # Windows would not find libstdc++-6.dll from here.
        target_link_options(bz_mcp_module PRIVATE -static)
        target_link_options(bz_mcp_bridge PRIVATE -static)
        # A newer MinGW leaves one CRT symbol undefined in static DLLs.
        target_sources(bz_mcp_module PRIVATE MingwCompat.c)
    endif()
endif()

# The package: the folder "mcp" zipped (CMake makes the ZIP itself, no zip tool needed).
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/module.json BZ_MCP_MANIFEST)
string(JSON BZ_MCP_VERSION GET "${BZ_MCP_MANIFEST}" version)
if(WIN32)
    set(BZ_MCP_SYSTEM windows)
elseif(APPLE)
    set(BZ_MCP_SYSTEM macos)
else()
    set(BZ_MCP_SYSTEM linux)
endif()
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" BZ_MCP_ARCH)
if(BZ_MCP_ARCH MATCHES "^(amd64|x86_64|x64)$")
    set(BZ_MCP_ARCH x64)
elseif(BZ_MCP_ARCH MATCHES "^(arm64|aarch64)$")
    set(BZ_MCP_ARCH arm64)
endif()
set(BZ_MCP_PACKAGE ${PROJECT_BINARY_DIR}/packages/bz-mcp-${BZ_MCP_VERSION}-${BZ_MCP_SYSTEM}-${BZ_MCP_ARCH}.bzmod)
set(BZ_MCP_PACKAGE ${BZ_MCP_PACKAGE} CACHE INTERNAL "The MCP module package (for tests)")
add_custom_command(OUTPUT ${BZ_MCP_PACKAGE}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_BINARY_DIR}/packages
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/module.json ${BZ_MCP_DIR}/module.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/README.md ${BZ_MCP_DIR}/README.md
    COMMAND ${CMAKE_COMMAND} -E chdir ${PROJECT_BINARY_DIR}/module-packages ${CMAKE_COMMAND} -E tar cf ${BZ_MCP_PACKAGE} --format=zip mcp
    DEPENDS bz_mcp_module bz_mcp_bridge ${CMAKE_CURRENT_SOURCE_DIR}/module.json ${CMAKE_CURRENT_SOURCE_DIR}/README.md
    COMMENT "Packing the MCP module: ${BZ_MCP_PACKAGE}"
    VERBATIM)
add_custom_target(bz_mcp ALL DEPENDS ${BZ_MCP_PACKAGE})
