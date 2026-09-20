#!/bin/bash
cd ~/BreezeCAD/build || exit 1

if [ ! -f bin/breezecad.exe ]; then
    echo "ERROR: breezecad.exe не найден. Соберите проект: ninja"
    exit 1
fi

# Всегда проверяем и копируем DLL (это быстро, если они уже на месте)
OCCT_BIN=~/occt/install/win64/gcc/bin
MSYS_BIN=/mingw64/bin

# Копируем OCCT DLL
if [ ! -f bin/libTKV3d.dll ] || [ ! -f bin/libTKernel.dll ]; then
    echo "→ Копирую OCCT DLL..."
    cp $OCCT_BIN/*.dll bin/ 2>/dev/null
fi

# Копируем MSYS2 runtime
for dll in libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll libglfw3.dll; do
    if [ ! -f "bin/$dll" ]; then
        echo "→ Копирую $dll"
        cp $MSYS_BIN/$dll bin/ 2>/dev/null
    fi
done

sleep 1
./bin/breezecad.exe
