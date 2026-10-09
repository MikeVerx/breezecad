add_executable(bz_tests
    main.cpp
    core/ExpressionTests.cpp
    core/I18nTests.cpp
    document/DocumentTests.cpp
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
    kernel/SweepTests.cpp
    kernel/WorkPlaneTests.cpp
    sketch/MirrorTests.cpp
    sketch/MoveTests.cpp
    sketch/ShapesTests.cpp
    sketch/SolverTests.cpp
    sketch/TrimTests.cpp)
target_link_libraries(bz_tests PRIVATE bz::core bz::sketch bz::document bz::kernel bz::doctest)
bz_set_warnings(bz_tests)
add_test(NAME bz_tests COMMAND bz_tests)
