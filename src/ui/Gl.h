#pragma once
// OpenGL for the interface's own drawing (icons, pictures, the window's
// clear): desktop OpenGL through GLFW's header, OpenGL ES 3 on Android and iOS.
// Only calls common to both are used.

#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#elif defined(BZ_IOS)
#include <OpenGLES/ES3/gl.h>
#else
#include <GLFW/glfw3.h>
#endif
