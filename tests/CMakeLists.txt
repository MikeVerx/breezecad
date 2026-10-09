add_executable(bz_tests
    main.cpp
    core/ExpressionTests.cpp
    document/DocumentTests.cpp
    kernel/BracketTests.cpp
    kernel/FilletTests.cpp
    kernel/HoleTests.cpp
    kernel/LoftTests.cpp
    kernel/MeasureTests.cpp
    kernel/ModelTests.cpp
    kernel/PatternTests.cpp
    kernel/RevolveTests.cpp
    kernel/ShellTests.cpp
    kernel/WorkPlaneTests.cpp
    sketch/SolverTests.cpp)
target_link_libraries(bz_tests PRIVATE bz::core bz::sketch bz::document bz::kernel bz::doctest)
bz_set_warnings(bz_tests)
add_test(NAME bz_tests COMMAND bz_tests)
