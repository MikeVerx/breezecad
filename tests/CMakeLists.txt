add_executable(bz_tests
    main.cpp
    assembly/AssemblyFileTests.cpp
    assembly/SolverTests.cpp
    core/ExpressionTests.cpp
    core/FaultGuardTests.cpp
    core/I18nTests.cpp
    core/ImageTests.cpp
    core/SourceRulesTests.cpp
    document/AppearanceTests.cpp
    document/DocumentTests.cpp
    kernel/AssemblyModelTests.cpp
    kernel/AutoDropTests.cpp
    kernel/ComponentPatternTests.cpp
    kernel/BracketTests.cpp
    kernel/CoilTests.cpp
    kernel/FilletTests.cpp
    kernel/HoleTests.cpp
    kernel/LoftTests.cpp
    kernel/MeasureTests.cpp
    kernel/ModelTests.cpp
    kernel/PatternTests.cpp
    kernel/ProjectionTests.cpp
    kernel/RevolvedProjectionTests.cpp
    kernel/RevolveTests.cpp
    kernel/SectionTests.cpp
    kernel/ShellTests.cpp
    kernel/StandardPartsTests.cpp
    kernel/SweepTests.cpp
    kernel/ThreadTests.cpp
    kernel/TopologyTests.cpp
    kernel/WorkPlaneTests.cpp
    kernel/WorkPointTests.cpp
    modules/ModuleTests.cpp
    sketch/FilletTests.cpp
    sketch/MirrorTests.cpp
    sketch/MoveTests.cpp
    sketch/OffsetTests.cpp
    sketch/ShapesTests.cpp
    sketch/SolverTests.cpp
    sketch/TrimTests.cpp)
target_link_libraries(bz_tests PRIVATE bz::miniz bz::core bz::sketch bz::document bz::assembly bz::kernel bz::modules bz::doctest)
add_dependencies(bz_tests bz_example_module bz_example_module_fail)
target_compile_definitions(bz_tests PRIVATE BZ_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    BZ_EXAMPLE_MODULE="$<TARGET_FILE:bz_example_module>"
    BZ_EXAMPLE_MODULE_FAIL="$<TARGET_FILE:bz_example_module_fail>")
if(BZ_BUILD_MCP)   # the MCP module's protocol layer: plain C++, tested in-process
    target_sources(bz_tests PRIVATE modules/McpProtocolTests.cpp ${PROJECT_SOURCE_DIR}/modules/mcp/Protocol.cpp
                                    ${PROJECT_SOURCE_DIR}/modules/mcp/Net.cpp ${PROJECT_SOURCE_DIR}/modules/mcp/Connection.cpp)
    target_include_directories(bz_tests PRIVATE ${PROJECT_SOURCE_DIR}/modules/mcp)
    target_link_libraries(bz_tests PRIVATE bz::json)
    if(WIN32)
        target_link_libraries(bz_tests PRIVATE ws2_32)
    endif()
    add_dependencies(bz_tests bz_mcp_module)
    target_compile_definitions(bz_tests PRIVATE BZ_MCP_MODULE="$<TARGET_FILE:bz_mcp_module>")
endif()
bz_set_warnings(bz_tests)
add_test(NAME bz_tests COMMAND bz_tests)
