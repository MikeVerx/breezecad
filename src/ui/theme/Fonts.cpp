#include "Fonts.h"
#include "imgui.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace BreezeCAD::UI::Theme {

// Диапазоны глифов Font Awesome (Private Use Area)
static const ImWchar kFontAwesomeRanges[] = { 0xf000, 0xf8ff, 0 };

void LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();

    // 1) Основной шрифт — Inter (современный, как в Figma/Linear)
    const std::vector<std::string> mainCandidates = {
        "assets/fonts/Inter-Regular.ttf",
        "../assets/fonts/Inter-Regular.ttf",
        "../../assets/fonts/Inter-Regular.ttf",
        "assets/fonts/Roboto-Regular.ttf",
        "../assets/fonts/Roboto-Regular.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };

    bool mainLoaded = false;
    for (const auto& path : mainCandidates) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) continue;
        ImFont* font = io.Fonts->AddFontFromFileTTF(
            path.c_str(), 18.0f, nullptr,
            io.Fonts->GetGlyphRangesCyrillic());
        if (font) {
            std::cout << "BreezeCAD: font '" << path << "'\n";
            mainLoaded = true;
            break;
        }
    }
    if (!mainLoaded) io.Fonts->AddFontDefault();

    // 2) Font Awesome — в режиме MergeMode поверх основного
    const std::vector<std::string> faCandidates = {
        "assets/fonts/fa-solid-900.ttf",
        "../assets/fonts/fa-solid-900.ttf",
        "../../assets/fonts/fa-solid-900.ttf",
    };

    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.PixelSnapH = true;

    for (const auto& path : faCandidates) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) continue;
        ImFont* fa = io.Fonts->AddFontFromFileTTF(
            path.c_str(), 18.0f, &cfg, kFontAwesomeRanges);
        if (fa) {
            std::cout << "BreezeCAD: Font Awesome loaded from '" << path << "'\n";
            break;
        }
    }

    // Явно собираем атлас — так поймаем проблемы здесь, а не в первом кадре
    io.Fonts->Build();
    std::cout << "BreezeCAD: font atlas built: " << (io.Fonts->IsBuilt() ? "yes" : "no") << "\n";
}


} // namespace BreezeCAD::UI::Theme
