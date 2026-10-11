# Modules: manifest, loading/switching off, the C ABI (bz_module.h) and its
# host side over the read API of kernel. No UI here.
add_library(bz_modules STATIC
    DynamicLibrary.cpp
    ModuleHost.cpp
    ModuleManager.cpp
    ModuleManifest.cpp)
target_link_libraries(bz_modules PUBLIC bz::core bz::kernel PRIVATE bz::json ${CMAKE_DL_LIBS})
bz_set_warnings(bz_modules)
add_library(bz::modules ALIAS bz_modules)
