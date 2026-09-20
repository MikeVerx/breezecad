# BreezeCAD

Кроссплатформенная САПР на базе OpenCASCADE.

## Сборка

### 1. Установить зависимости (MSYS2 MINGW64)

```bash
pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-ninja mingw-w64-x86_64-git \
  mingw-w64-x86_64-freetype mingw-w64-x86_64-tcl mingw-w64-x86_64-tk \
  mingw-w64-x86_64-tbb mingw-w64-x86_64-jemalloc \
  mingw-w64-x86_64-rapidjson mingw-w64-x86_64-glfw

