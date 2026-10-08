#pragma once
// Handles of the window and OpenGL context that OCCT needs to render with
// the application's context. Produced by platform::Window, consumed by
// render::Viewport, so neither OS headers nor GLFW reach the OCCT code.

#include <cstdint>

namespace bz {

struct NativeGlSurface {
    std::uintptr_t window    = 0;         ///< HWND, NSWindow* or X11 Window
    void*          glContext = nullptr;   ///< HGLRC, NSOpenGLContext* or GLXContext
};

} // namespace bz
