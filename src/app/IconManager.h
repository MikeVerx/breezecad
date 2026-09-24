#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>

namespace BreezeCAD {

class IconManager {
public:
    void LoadAll(const std::string& dir);

    // Предзагрузка всех иконок при старте (чтобы не тормозить первый кадр)
    void PreloadAll(const std::vector<std::string>& names);
    // Предзагрузка дефолтного списка иконок проекта
    void PreloadDefaults();
    GLuint Get(const std::string& name, int size = 32);
    void Clear();

    // Установить путь к папке с иконками
    void SetIconDir(const std::string& dir) { myIconDir = dir; }

private:
    std::string myIconDir = "assets/icons";
    std::unordered_map<std::string, GLuint> myCache;

    GLuint RasterizeToTexture(const std::string& svgPath, int size);
    GLuint CreateTexture(const unsigned char* pixels, int w, int h);
};

} // namespace BreezeCAD
