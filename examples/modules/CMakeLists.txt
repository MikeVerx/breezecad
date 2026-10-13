# Example module (C ABI only, no link to BreezeCAD's libraries). Built into
# <build>/modules/hello/ next to a copy of its module.json. The unit tests
# copy the library into their own folders; bz_example_module_fail is the
# same source with init() refusing (tests only, no manifest).
function(bz_example_module target folder)
    add_library(${target} SHARED hello/hello.c)
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src/modules)
    set_target_properties(${target} PROPERTIES
        C_VISIBILITY_PRESET hidden
        OUTPUT_NAME hello
        LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/modules/${folder}
        RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/modules/${folder}
        ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/modules/${folder})   # MinGW import lib: one per variant
    if(WIN32)
        set_target_properties(${target} PROPERTIES PREFIX "")   # hello.dll, as module.json expects
    endif()
endfunction()

bz_example_module(bz_example_module hello)
add_custom_command(TARGET bz_example_module POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/hello/module.json
            ${PROJECT_BINARY_DIR}/modules/hello/module.json)
bz_example_module(bz_example_module_fail hello-fail)
target_compile_definitions(bz_example_module_fail PRIVATE BZ_HELLO_FAIL)
