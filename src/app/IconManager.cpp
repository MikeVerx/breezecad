#include "app/IconManager.h"

// Lunasvg использует разные префиксы для функций в статической и динамической
// библиотеках. Мы линкуемся статически, поэтому нужен LUNASVG_BUILD_STATIC.
#define LUNASVG_BUILD_STATIC 1

#include <lunasvg/lunasvg.h>
#include <filesystem>

// Windows-версия <GL/gl.h> ограничена OpenGL 1.1.
// Константы ниже — из OpenGL 1.2+.
#ifndef GL_CLAMP_TO_EDGE
  #define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_BGRA
  #define GL_BGRA 0x80E1
#endif
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>

namespace BreezeCAD {

// Синонимы имён — если Lucide переименовал иконку, мы всё равно её найдём.
// Ключ = что мы запрашиваем, значение = настоящее имя файла в Lucide.
static const std::unordered_map<std::string, std::string> kIconAliases = {
    { "home",     "house" },
    { "house",    "house" },
    { "settings", "settings" },
    { "cog",      "cog" },
    { "file",     "file-text" },
    { "save",     "save" },
    { "close",    "x" },
    { "check",    "check" },
    { "trash",    "trash" },
    { "flip-horizontal",  "mirror-rectangular" },
    { "flip-horizontal-2", "mirror-round" },
    { "pencil",   "pencil" },
    { "eye",      "eye" },
    { "eye-slash", "eye-off" },
    { "chevron-down",  "chevron-down" },
    { "chevron-left",  "chevron-left" },
    { "chevron-right", "chevron-right" },
    { "chevron-up",    "chevron-up" },
    { "plus",     "plus" },
    { "minus",    "minus" },
    { "search",   "search" },
    { "square",   "square" },
    { "circle",   "circle" },
    { "circle-dot", "circle-dot" },
    { "ruler",    "ruler" },
    { "scissors", "scissors" },
    { "undo",     "undo-2" },
    { "redo",     "redo-2" },
};



GLuint IconManager::CreateTexture(const unsigned char* pixels, int w, int h) {
    GLuint texId = 0;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    return texId;
}

GLuint IconManager::RasterizeToTexture(const std::string& svgPath, int size) {
    std::error_code ec;
    if (!std::filesystem::exists(svgPath, ec)) {
        std::cerr << "IconManager: file not found: " << svgPath << "\n";
        return 0;
    }

    // Читаем SVG как строку
    std::ifstream file(svgPath);
    if (!file.is_open()) {
        std::cerr << "IconManager: cannot open " << svgPath << "\n";
        return 0;
    }
    std::stringstream buf;
    buf << file.rdbuf();
    std::string svgData = buf.str();
    file.close();

    // Заменяем currentColor на белый — потом tint при отрисовке
    // Lucide использует stroke="currentColor" по умолчанию
    size_t pos = 0;
    while ((pos = svgData.find("currentColor", pos)) != std::string::npos) {
        svgData.replace(pos, 12, "#FFFFFF");
        pos += 7;
    }

    // Загружаем SVG из памяти
    auto doc = lunasvg::Document::loadFromData(svgData);
    if (!doc) {
        std::cerr << "IconManager: parse failed: " << svgPath << "\n";
        return 0;
    }

    // Рендерим в 2x размере для HiDPI
    int renderSize = size * 2;
    auto bitmap = doc->renderToBitmap(renderSize, renderSize);
    if (bitmap.isNull()) {
        std::cerr << "IconManager: render failed: " << svgPath << "\n";
        return 0;
    }

    // Lunasvg возвращает данные в формате BGRA (premultiplied).
    // Конвертируем в RGBA (non-premultiplied) для ImGui.
    int w = bitmap.width();
    int h = bitmap.height();
    const uint8_t* src = bitmap.data();
    std::vector<uint8_t> rgba(w * h * 4);

    for (int i = 0; i < w * h; i++) {
        uint8_t b = src[i * 4 + 0];
        uint8_t g = src[i * 4 + 1];
        uint8_t r = src[i * 4 + 2];
        uint8_t a = src[i * 4 + 3];
        rgba[i * 4 + 0] = r;
        rgba[i * 4 + 1] = g;
        rgba[i * 4 + 2] = b;
        rgba[i * 4 + 3] = a;
    }

    return CreateTexture(rgba.data(), w, h);
}

void IconManager::LoadAll(const std::string& dir) {
    namespace fs = std::filesystem;
    if (!fs::exists(dir)) {
        std::cerr << "IconManager: dir not found: " << dir << "\n";
        return;
    }
    myIconDir = dir;
    std::cout << "IconManager: icons dir = " << dir << "\n";
}

GLuint IconManager::Get(const std::string& name, int /*size*/) {
    // Всегда рендерим в 32px — ImGui масштабирует при отрисовке.
    // Это позволяет кешировать одну текстуру для всех размеров.
    std::string key = name;
    auto it = myCache.find(key);
    if (it != myCache.end()) return it->second;

    // Применяем синоним, если он есть
    std::string realName = name;
    auto aliasIt = kIconAliases.find(name);
    if (aliasIt != kIconAliases.end()) {
        realName = aliasIt->second;
    }

    // Ищем SVG по нескольким возможным путям
    std::vector<std::string> candidates = {
        myIconDir + "/" + realName + ".svg",
        "assets/icons/" + realName + ".svg",
        "../assets/icons/" + realName + ".svg",
        "../../assets/icons/" + realName + ".svg",
    };

    std::string foundPath;
    std::error_code ec;
    for (const auto& p : candidates) {
        if (std::filesystem::exists(p, ec)) {
            foundPath = p;
            break;
        }
    }

    if (foundPath.empty()) {
        std::cerr << "IconManager: NOT FOUND " << realName << ".svg (alias for " << name << ")\n";
        myCache[key] = 0;
        return 0;
    }

    GLuint tex = RasterizeToTexture(foundPath, 32);
    myCache[key] = tex;
    return tex;
}

void IconManager::PreloadAll(const std::vector<std::string>& names) {
    int count = 0;
    for (const auto& name : names) {
        GLuint tex = Get(name, 32);
        if (tex != 0) count++;
    }
    std::cout << "IconManager: preloaded " << count << "/" << names.size()
              << " icons\n";
}

void IconManager::PreloadDefaults() {
    // Список всех иконок, используемых в UI
    static const std::vector<std::string> kDefaultIcons = {
        // QAT
        "house", "file-text", "save", "settings", "undo-2", "redo-2",
        // Навигация
        "search", "hand", "rotate-3d", "eye", "palette",
        "chevron-down", "chevron-left", "chevron-right", "chevron-up",
        // Создание
        "pen-line", "square", "circle", "circle-dashed", "pentagon",
        "circle-dot", "type",
        // Проецирование
        "copy", "copy-check",
        // Изменение
        "scissors", "move-horizontal", "copy-plus", "move",
        "rotate-cw", "maximize-2", "flip-horizontal",
        // Ограничения
        "equal", "target", "align-justify", "corner-down-right",
        "move-vertical", "circle-dot-dashed", "spline",
        "flip-horizontal-2", "lock", "ruler",
        // 3D операции (базовые)
        "arrow-up-from-line", "arrow-right", "layers",
        "pencil-ruler", "package", "arrow-up", "plus", "minus", "boxes",
        "grid-3x3",
        // CAD-иконки (собственные)
        "cad/extrude", "cad/revolve", "cad/sweep", "cad/loft", "cad/hole",
        "cad/thread", "cad/fillet", "cad/chamfer", "cad/shell", "cad/union",
        // Рабочие элементы
        "move-3d",
        // Аннотации
        "angle", "arrow-right-from-line", "flag", "waves", "zap",
        // Инструменты
        "distance", "box", "alert-triangle", "info", "scale",
        // Управление
        "list", "calculator", "image", "brush",
        "refresh-cw", "refresh-ccw", "alert-circle", "upload",
        // Вид
        "arrow-down", "arrow-left", "sun", "sparkles",
        "eye-off", "focus", "layout-grid",
        // Общие
        "x", "check", "pencil", "trash", "ellipsis",
    };
    PreloadAll(kDefaultIcons);
}

void IconManager::Clear() {
    for (auto& kv : myCache) {
        if (kv.second) glDeleteTextures(1, &kv.second);
    }
    myCache.clear();
}

} // namespace BreezeCAD
