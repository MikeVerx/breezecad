add_executable(bz_tests
    main.cpp
    app/DimensionLayoutTests.cpp
    app/NewTemplatesTests.cpp
    app/SettingsTests.cpp
    # The app is an executable; its OCCT-free pieces are compiled in directly.
    ${PROJECT_SOURCE_DIR}/src/app/Format.cpp
    ${PROJECT_SOURCE_DIR}/src/app/NewTemplates.cpp
    ${PROJECT_SOURCE_DIR}/src/app/sketch/DimensionLayout.cpp
    ${PROJECT_SOURCE_DIR}/src/app/Settings.cpp
    assembly/AssemblyFileTests.cpp
    assembly/SolverTests.cpp
    core/CrashCodeTests.cpp
    core/ExpressionTests.cpp
    core/FaultGuardTests.cpp
    core/FoldersTests.cpp
    core/I18nTests.cpp
    core/ImageTests.cpp
    core/PathTextTests.cpp
    core/SourceRulesTests.cpp
    document/AppearanceTests.cpp
    document/CorruptFileTests.cpp
    document/DocumentTests.cpp
    document/StandardPacksTests.cpp
    kernel/AssemblyModelTests.cpp
    kernel/AutoDropTests.cpp
    kernel/ComponentPatternTests.cpp
    kernel/BracketTests.cpp
    kernel/CoilTests.cpp
    kernel/FilletTests.cpp
    kernel/HoleTests.cpp
    kernel/LoftTests.cpp
    kernel/MeasureTests.cpp
    kernel/ExtrudeToFaceTests.cpp
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
    kernel/ToolRunTests.cpp
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
target_link_libraries(bz_tests PRIVATE bz::miniz bz::core bz::sketch bz::document bz::assembly bz::kernel bz::modules bz::doctest bz::json)
add_dependencies(bz_tests bz_example_module bz_example_module_fail)
if(ANDROID)
    # The tests run on a phone or the emulator, not where they were built:
    # scripts/android/run-tests.sh copies the sources' data (assets, src,
    # tests) and the example modules to these places on the device.
    set(BZ_TEST_DEVICE_DIR "/data/local/tmp/breezecad" CACHE STRING "Where run-tests.sh puts the tests on the device")
    target_compile_definitions(bz_tests PRIVATE BZ_SOURCE_DIR="${BZ_TEST_DEVICE_DIR}/source"
        BZ_EXAMPLE_MODULE="${BZ_TEST_DEVICE_DIR}/modules/hello/$<TARGET_FILE_NAME:bz_example_module>"
        BZ_EXAMPLE_MODULE_FAIL="${BZ_TEST_DEVICE_DIR}/modules/hello-fail/$<TARGET_FILE_NAME:bz_example_module_fail>"
        BZ_CMAKE="cmake")   # no CMake on a device: the one test using it is off there
else()
    target_compile_definitions(bz_tests PRIVATE BZ_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        BZ_EXAMPLE_MODULE="$<TARGET_FILE:bz_example_module>"
        BZ_EXAMPLE_MODULE_FAIL="$<TARGET_FILE:bz_example_module_fail>"
        BZ_CMAKE="${CMAKE_COMMAND}")
endif()
if(BZ_BUILD_MCP)   # the MCP module's protocol layer, the module, and its package
    target_sources(bz_tests PRIVATE modules/McpProtocolTests.cpp ${PROJECT_SOURCE_DIR}/modules/mcp/Protocol.cpp
                                    ${PROJECT_SOURCE_DIR}/modules/mcp/Net.cpp ${PROJECT_SOURCE_DIR}/modules/mcp/Connection.cpp)
    target_include_directories(bz_tests PRIVATE ${PROJECT_SOURCE_DIR}/modules/mcp)
    if(WIN32)
        target_link_libraries(bz_tests PRIVATE ws2_32)
    endif()
    add_dependencies(bz_tests bz_mcp)
    target_compile_definitions(bz_tests PRIVATE BZ_MCP_MODULE="$<TARGET_FILE:bz_mcp_module>" BZ_MCP_PACKAGE="${BZ_MCP_PACKAGE}")
endif()
bz_set_warnings(bz_tests)
add_test(NAME bz_tests COMMAND bz_tests)
