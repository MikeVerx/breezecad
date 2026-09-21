#!/bin/bash
# BreezeCAD — run script
# Запускает приложение через gdb (обход бага инициализации OpenGL на Windows)

cd "$(dirname "$0")/build" || exit 1

# Убиваем возможные висящие процессы
taskkill //F //IM breezecad.exe 2>/dev/null >/dev/null

# Проверяем бинарник
if [ ! -f bin/breezecad.exe ]; then
    echo "ERROR: bin/breezecad.exe не найден. Сначала собери: ninja"
    exit 1
fi

# Копируем MSYS runtime DLL, если их нет
for dll in libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll; do
    if [ ! -f "bin/$dll" ] && [ -f "/mingw64/bin/$dll" ]; then
        cp /mingw64/bin/$dll bin/ 2>/dev/null
    fi
done

# Запуск через gdb — обход бага с белым экраном
echo "== Запуск BreezeCAD через gdb =="
cd bin
gdb --batch -ex run -ex bt --args ./breezecad.exe 2>&1 | grep -v "^warning:" | tail -60
