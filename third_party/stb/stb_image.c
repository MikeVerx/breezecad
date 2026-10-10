// The one translation unit with stb_image's implementation: the picture
// formats people have (PNG, JPEG, BMP, TGA), nothing else.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_NO_STDIO   // files are read by the caller (UTF-8 paths on Windows)
#include "stb_image.h"
