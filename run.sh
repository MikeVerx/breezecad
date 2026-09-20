#!/bin/bash
cd ~/BreezeCAD/build
if [ ! -f bin/breezecad.exe ]; then
    echo "Сначала соберите проект: ninja"
    exit 1
fi
# Проверяем наличие OCCT DLL
if [ ! -f bin/libTKV3d.dll ]; then
    echo "Копирую OCCT DLL..."
    cp ~/occt/install/win64/gcc/bin/*.dll bin/ 2>/dev/null
    sleep 1
fi
./bin/breezecad.exe
