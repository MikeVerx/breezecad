#!/bin/bash
# BreezeCAD — запуск приложения
#
# ВАЖНО: НЕ использовать taskkill /F.
# Жёсткое убийство процесса оставляет GL-контекст в драйвере,
# следующий запуск получает белый экран.
# Закрывать приложение через крестик окна.

cd "$(dirname "$0")/build" || exit 1

if [ ! -f bin/breezecad.exe ]; then
    echo "ERROR: bin/breezecad.exe не найден. Сначала собери: ninja"
    exit 1
fi

# MSYS runtime DLL
for dll in libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll; do
    if [ ! -f "bin/$dll" ] && [ -f "/mingw64/bin/$dll" ]; then
        cp /mingw64/bin/$dll bin/ 2>/dev/null
    fi
done

cd bin
./breezecad.exe
