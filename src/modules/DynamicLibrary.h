#pragma once
// A shared library opened at run time (LoadLibraryW / dlopen).

#include "core/Expected.h"

#include <filesystem>

namespace bz::modules {

class DynamicLibrary {
public:
    DynamicLibrary() = default;
    ~DynamicLibrary() { Close(); }
    DynamicLibrary(DynamicLibrary&& o) noexcept : myHandle(o.myHandle) { o.myHandle = nullptr; }
    DynamicLibrary& operator=(DynamicLibrary&& o) noexcept;
    DynamicLibrary(const DynamicLibrary&)            = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    /// Error text (Russian) includes the system's reason.
    static Expected<DynamicLibrary> Open(const std::filesystem::path& file);

    /// Exported symbol or nullptr.
    void* Symbol(const char* name) const;
    bool  IsOpen() const { return myHandle != nullptr; }
    void  Close();

private:
    void* myHandle = nullptr;
};

} // namespace bz::modules
