#pragma once
// OpenGL for the interface's own drawing (icons, pictures, the window's
// clear): desktop OpenGL through GLFW's header, OpenGL ES 3 on Android.
// Only calls common to both are used.

#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#else
#include <GLFW/glfw3.h>
#endif
