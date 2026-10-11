# MCP server module (bz-mcp) and its stdio bridge (bz-mcp-bridge), built into
# <bin>/modules/mcp/ next to a copy of module.json: the folder the program
# looks for shipped modules in. The module uses only the C ABI (bz_module.h);
# the bridge shares platform/Paths.cpp with the program to find the settings
# folder in the same place.
find_package(Threads REQUIRED)

set(BZ_MCP_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/modules/mcp)

add_library(bz_mcp_module SHARED McpModule.cpp Net.cpp Connection.cpp Protocol.cpp)
target_include_directories(bz_mcp_module PRIVATE ${PROJECT_SOURCE_DIR}/src/modules)
target_link_libraries(bz_mcp_module PRIVATE bz::json Threads::Threads)
set_target_properties(bz_mcp_module PROPERTIES
    OUTPUT_NAME bz-mcp
    CXX_VISIBILITY_PRESET hidden
    LIBRARY_OUTPUT_DIRECTORY ${BZ_MCP_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${BZ_MCP_DIR}
    ARCHIVE_OUTPUT_DIRECTORY ${BZ_MCP_DIR})
bz_set_warnings(bz_mcp_module)
add_custom_command(TARGET bz_mcp_module POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/module.json ${BZ_MCP_DIR}/module.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/README.md ${BZ_MCP_DIR}/README.md)

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
    endif()
endif()

add_custom_target(bz_mcp ALL DEPENDS bz_mcp_module bz_mcp_bridge)
