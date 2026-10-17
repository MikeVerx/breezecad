#include "ui/Icons.h"

#include "core/Log.h"

#include "ui/Gl.h"
#include <lunasvg.h>

#include <cmath>
#include <fstream>
#include <iterator>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F   // OpenGL 1.2; Windows headers stop at 1.1
#endif

namespace bz::ui {

Icons::~Icons() { Clear(); }

void Icons::SetScale(float scale) {
    if (scale == myScale) return;
    myScale = scale;
    Clear();
}

void Icons::Clear() {
    for (const auto& [key, texture] : myTextures) {
        const GLuint id = texture;
        glDeleteTextures(1, &id);
    }
    myTextures.clear();
}

const Icons::Svg& Icons::Load(std::string_view name) {
    const auto found = mySvgs.find(std::string(name));
    if (found != mySvgs.end()) return found->second;

    Svg svg;
    std::ifstream in(myDir / (std::string(name) + ".svg"), std::ios::binary);
    if (!in) {
        svg.missing = true;
        log::Warn("Icon not found: {}", name);
    } else {
        svg.text.assign(std::istreambuf_iterator<char>(in), {});
        // Monochrome icons are drawn in white and tinted by ImGui.
        for (std::size_t pos = svg.text.find("currentColor"); pos != std::string::npos;
             pos = svg.text.find("currentColor", pos)) {
            svg.text.replace(pos, 12, "#FFFFFF");
            svg.monochrome = true;
        }
    }
    return mySvgs.emplace(std::string(name), std::move(svg)).first->second;
}

bool Icons::IsMonochrome(std::string_view name) { return Load(name).monochrome; }

ImTextureID Icons::Texture(std::string_view name, float size) {
    const int px = std::max(1, static_cast<int>(std::lround(size * myScale)));
    const std::string key = std::string(name) + "@" + std::to_string(px);
    if (const auto it = myTextures.find(key); it != myTextures.end()) return static_cast<ImTextureID>(it->second);

    const Svg& svg = Load(name);
    GLuint texture = 0;
    if (!svg.missing) {
        const auto document = lunasvg::Document::loadFromData(svg.text);
        if (document) {
            lunasvg::Bitmap bitmap = document->renderToBitmap(px, px);
            if (!bitmap.isNull()) {
                bitmap.convertToRGBA();
                glGenTextures(1, &texture);
                glBindTexture(GL_TEXTURE_2D, texture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, bitmap.stride() / 4);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, px, px, 0, GL_RGBA, GL_UNSIGNED_BYTE, bitmap.data());
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            }
        } else {
            log::Warn("Icon cannot be parsed: {}", name);
        }
    }
    myTextures[key] = texture;
    return static_cast<ImTextureID>(texture);
}

void Icons::Draw(ImDrawList* list, std::string_view name, ImVec2 pos, float size, Color tint) {
    const ImTextureID texture = Texture(name, size);
    if (!texture) return;
    const ImU32 color = IsMonochrome(name) ? ImGui::ColorConvertFloat4ToU32({tint.r, tint.g, tint.b, tint.a})
                                           : ImGui::ColorConvertFloat4ToU32({1, 1, 1, tint.a});
    // Snap to whole pixels so the rasterised icon stays sharp.
    const ImVec2 p{std::round(pos.x), std::round(pos.y)};
    list->AddImage(texture, p, {p.x + size, p.y + size}, {0, 0}, {1, 1}, color);
}

} // namespace bz::ui
