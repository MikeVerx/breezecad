#include "modules/DynamicLibrary.h"

#include <cstring>
#include <format>
#include <string>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace bz::modules {

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& o) noexcept {
    if (this != &o) {
        Close();
        myHandle = std::exchange(o.myHandle, nullptr);
    }
    return *this;
}

Expected<DynamicLibrary> DynamicLibrary::Open(const std::filesystem::path& file) {
    DynamicLibrary lib;
#if defined(_WIN32)
    // The module's folder first: a module may ship its own DLLs next to it.
    HMODULE h = LoadLibraryExW(file.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!h) return Error{std::format("Не удалось загрузить библиотеку модуля (код ошибки Windows {})", GetLastError())};
    lib.myHandle = reinterpret_cast<void*>(h);
#else
    void* h = dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        const char* why = dlerror();
        return Error{std::format("Не удалось загрузить библиотеку модуля: {}", why ? why : "неизвестная ошибка")};
    }
    lib.myHandle = h;
#endif
    return lib;
}

void* DynamicLibrary::Symbol(const char* name) const {
    if (!myHandle) return nullptr;
#if defined(_WIN32)
    const FARPROC f = GetProcAddress(reinterpret_cast<HMODULE>(myHandle), name);
    void*         p = nullptr;
    static_assert(sizeof(p) == sizeof(f));
    std::memcpy(&p, &f, sizeof(p));   // function pointer -> void* without a cast warning
    return p;
#else
    return dlsym(myHandle, name);
#endif
}

void DynamicLibrary::Close() {
    if (!myHandle) return;
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(myHandle));
#else
    dlclose(myHandle);
#endif
    myHandle = nullptr;
}

} // namespace bz::modules
