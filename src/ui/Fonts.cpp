#include "ui/Fonts.h"

#include "core/Log.h"

#include <imgui.h>

namespace bz::ui {
namespace {

/// Latin, Latin-1, Cyrillic, Greek, general punctuation (— … «»), degree,
/// diameter, arrows and math signs used in the UI. A character outside these
/// ranges (or missing from the font) is drawn as "?": the diameter sign is Ø
/// (U+00D8), not ⌀ (U+2300), which IBM Plex lacks.
const ImWchar* GlyphRanges() {
    static const ImWchar ranges[] = {
        0x0020, 0x00FF,   // Basic Latin + Latin-1 (°, ±, ×, «», Ø)
        0x0370, 0x03FF,   // Greek: Δ in the measure panel; μ, π for later (IBM Plex Sans has them, Mono lacks Δ)
        0x0400, 0x04FF,   // Cyrillic
        0x2010, 0x2044,   // dashes, quotes, ellipsis
        0x2070, 0x209F,   // super/subscripts
        0x2116, 0x2116,   // №
        0x2190, 0x21FF,   // arrows
        0x2212, 0x2265,   // minus, ≈, ≠, ≤, ≥
        0,
    };
    return ranges;
}

ImFont* Add(ImFontAtlas* atlas, const std::filesystem::path& file, float size, float scale) {
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 2;
    config.PixelSnapH = false;
    const std::u8string path = file.u8string();
    ImFont* font = atlas->AddFontFromFileTTF(reinterpret_cast<const char*>(path.c_str()), size * scale, &config,
                                              GlyphRanges());
    if (!font) log::Error("Font not found: {}", reinterpret_cast<const char*>(path.c_str()));
    return font;
}

} // namespace

bool Fonts::Load(const std::filesystem::path& assetsDir, float scale) {
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    atlas->Clear();
    const auto dir = assetsDir / "fonts";
    const auto regular = dir / "IBMPlexSans-Regular.ttf";
    const auto med = dir / "IBMPlexSans-Medium.ttf";
    const auto semi = dir / "IBMPlexSans-SemiBold.ttf";
    const auto monoFile = dir / "IBMPlexMono-Regular.ttf";

    body      = Add(atlas, regular, 13.0f, scale);   // first font = ImGui default
    bodySmall = Add(atlas, regular, 12.0f, scale);
    caption   = Add(atlas, regular, 11.0f, scale);
    medium    = Add(atlas, med, 13.0f, scale);
    strong    = Add(atlas, semi, 13.0f, scale);
    title     = Add(atlas, semi, 14.0f, scale);
    display   = Add(atlas, semi, 22.0f, scale);
    mono      = Add(atlas, monoFile, 12.0f, scale);
    monoLarge = Add(atlas, monoFile, 14.0f, scale);

    const bool ok = body && bodySmall && caption && medium && strong && title && display && mono && monoLarge;
    if (!ok) {
        atlas->Clear();
        ImFont* fallback = atlas->AddFontDefault();
        body = bodySmall = caption = medium = strong = title = display = mono = monoLarge = fallback;
    }
    return ok;
}

} // namespace bz::ui
