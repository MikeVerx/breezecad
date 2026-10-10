add_executable(bz_tests
    main.cpp
    assembly/AssemblyFileTests.cpp
    core/ExpressionTests.cpp
    core/I18nTests.cpp
    core/ImageTests.cpp
    core/SourceRulesTests.cpp
    document/AppearanceTests.cpp
    document/DocumentTests.cpp
    kernel/AssemblyModelTests.cpp
    kernel/BracketTests.cpp
    kernel/CoilTests.cpp
    kernel/FilletTests.cpp
    kernel/HoleTests.cpp
    kernel/LoftTests.cpp
    kernel/MeasureTests.cpp
    kernel/ModelTests.cpp
    kernel/PatternTests.cpp
    kernel/ProjectionTests.cpp
    kernel/RevolveTests.cpp
    kernel/SectionTests.cpp
    kernel/ShellTests.cpp
    kernel/StandardPartsTests.cpp
    kernel/SweepTests.cpp
    kernel/ThreadTests.cpp
    kernel/WorkPlaneTests.cpp
    kernel/WorkPointTests.cpp
    sketch/FilletTests.cpp
    sketch/MirrorTests.cpp
    sketch/MoveTests.cpp
    sketch/OffsetTests.cpp
    sketch/ShapesTests.cpp
    sketch/SolverTests.cpp
    sketch/TrimTests.cpp)
target_link_libraries(bz_tests PRIVATE bz::miniz bz::core bz::sketch bz::document bz::assembly bz::kernel bz::doctest)
target_compile_definitions(bz_tests PRIVATE BZ_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
bz_set_warnings(bz_tests)
add_test(NAME bz_tests COMMAND bz_tests)
