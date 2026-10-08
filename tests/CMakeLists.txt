add_executable(bz_tests
    main.cpp
    core/ExpressionTests.cpp
    document/DocumentTests.cpp
    kernel/FilletTests.cpp
    kernel/ModelTests.cpp
    kernel/RevolveTests.cpp
    sketch/SolverTests.cpp)
target_link_libraries(bz_tests PRIVATE bz::core bz::sketch bz::document bz::kernel bz::doctest)
bz_set_warnings(bz_tests)
add_test(NAME bz_tests COMMAND bz_tests)
