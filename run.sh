#!/bin/bash
cd ~/BreezeCAD/build || exit 1

if [ ! -f bin/breezecad.exe ]; then
    echo "ERROR: breezecad.exe не найден"
    exit 1
fi

# Копируем OCCT DLL если их нет
if [ ! -f bin/libTKV3d.dll ]; then
    cp ~/occt/install/win64/gcc/bin/*.dll bin/ 2>/dev/null
fi

# Копируем MSYS runtime
for dll in libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll libglfw3.dll; do
    if [ ! -f "bin/$dll" ]; then
        cp /mingw64/bin/$dll bin/ 2>/dev/null
    fi
done

# Ждём пока Windows "отпустит" файлы после ninja
sleep 2

# Запускаем
./bin/breezecad.exe
