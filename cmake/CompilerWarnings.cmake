# Strict warnings for BreezeCAD's own targets (never for third_party).
function(bz_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
            -Wold-style-cast -Wcast-align -Woverloaded-virtual
            -Wconversion -Wsign-conversion -Wnull-dereference
            -Wno-unused-parameter)
    endif()
endfunction()
