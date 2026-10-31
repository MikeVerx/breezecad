#include "ui/Fonts.h"

#include "core/I18n.h"

#include "core/Log.h"

#include <imgui.h>

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace bz::ui {
namespace {

/// Latin, Latin-1, Cyrillic, Greek, general punctuation (— … «»), degree,
/// diameter, arrows and math signs used in the UI. A character outside these
/// ranges (or missing from the font) is drawn as "?": the diameter sign is Ø
/// (U+00D8), not ⌀ (U+2300), which IBM Plex lacks.
const ImWchar* GlyphRanges() {
    static const ImWchar ranges[] = {
        0x0020, 0x00FF,   // Basic Latin + Latin-1 (°, ±, ×, «», Ø)
        0x0100, 0x024F,   // Latin Extended-A/B: Polish, Czech, Turkish… (ł, ř, ğ, ş, ı)
        0x0370, 0x03FF,   // Greek: Δ in the measure panel; μ, π for later (IBM Plex Sans has them, Mono lacks Δ)
        0x0400, 0x052F,   // Cyrillic and its supplement: Ukrainian, Belarusian, Kazakh (ї, ў, ә, ғ, қ)
        0x2010, 0x2044,   // dashes, quotes, ellipsis
        0x2070, 0x209F,   // super/subscripts
        0x2116, 0x2116,   // №
        0x2190, 0x21FF,   // arrows
        0x2212, 0x2265,   // minus, ≈, ≠, ≤, ≥
        0,
    };
    return ranges;
}

/// A font with the glyphs of Chinese, Japanese or Korean: the language
/// pack's own (<packs>/<code>.ttf|.otf|.ttc), else the system's. Empty when
/// there is none (the text then shows "?"). `face`: the font in a collection.
std::filesystem::path CjkFont(const std::string& code, const std::filesystem::path& packs, int& face) {
    face = 0;
    std::error_code ec;
    for (const char* ext : {".ttf", ".otf", ".ttc"})
        if (const auto own = packs / (code + ext); !packs.empty() && std::filesystem::exists(own, ec)) return own;
    std::vector<std::pair<std::filesystem::path, int>> candidates;
#if defined(_WIN32)
    std::filesystem::path fonts = "C:\\Windows\\Fonts";
    if (const char* windir = std::getenv("WINDIR")) fonts = std::filesystem::path(windir) / "Fonts";
    if (code == "zh") candidates = {{fonts / "msyh.ttc", 0}, {fonts / "simsun.ttc", 0}};
    if (code == "ja") candidates = {{fonts / "YuGothM.ttc", 0}, {fonts / "meiryo.ttc", 0}, {fonts / "msgothic.ttc", 0}};
    if (code == "ko") candidates = {{fonts / "malgun.ttf", 0}, {fonts / "gulim.ttc", 0}};
#elif defined(__APPLE__)
    if (code == "zh") candidates = {{"/System/Library/Fonts/PingFang.ttc", 0}, {"/System/Library/Fonts/STHeiti Light.ttc", 0}};
    if (code == "ja") candidates = {{"/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc", 0}, {"/System/Library/Fonts/Hiragino Sans GB.ttc", 0}};
    if (code == "ko") candidates = {{"/System/Library/Fonts/AppleSDGothicNeo.ttc", 0}};
#else
    // Noto Sans CJK: one collection, the face by language (JP 0, KR 1, SC 2).
    const int noto = code == "ja" ? 0 : code == "ko" ? 1 : 2;
    for (const char* dir : {"/system/fonts", "/usr/share/fonts/opentype/noto", "/usr/share/fonts/noto-cjk", "/usr/share/fonts/google-noto-cjk",
                            "/usr/share/fonts/truetype/noto", "/usr/share/fonts/noto"})
        candidates.push_back({std::filesystem::path(dir) / "NotoSansCJK-Regular.ttc", noto});
    if (code == "zh")
        for (const char* wqy : {"/usr/share/fonts/truetype/wqy/wqy-microhei.ttc", "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc"})
            candidates.push_back({wqy, 0});
#endif
    for (const auto& [file, f] : candidates)
        if (std::filesystem::exists(file, ec)) {
            face = f;
            return file;
        }
    return {};
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

bool Fonts::Load(const std::filesystem::path& assetsDir, float scale, const std::string& language,
                 const std::filesystem::path& languagePacks) {
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

    // Chinese, Japanese, Korean: their glyphs merged into the text fonts
    // (not the numbers' mono ones), at the size and only for that language
    // — a few thousand glyphs each would weigh on a weak computer otherwise.
    loadedLanguage = language;
    if (language == "zh" || language == "ja" || language == "ko") {
        int face = 0;
        const std::filesystem::path cjk = CjkFont(language, languagePacks, face);
        if (cjk.empty()) {
            log::Warn("No font with {} glyphs found: text in that language shows question marks", language);
        } else {
            // The language's usual set plus every character its catalog uses.
            static ImVector<ImWchar> built;
            built.clear();
            ImFontGlyphRangesBuilder builder;
            builder.AddRanges(language == "zh" ? atlas->GetGlyphRangesChineseSimplifiedCommon()
                              : language == "ja" ? atlas->GetGlyphRangesJapanese()
                                                 : atlas->GetGlyphRangesKorean());
            builder.AddText(i18n::AllTranslatedText().c_str());
            builder.BuildRanges(&built);
            const ImWchar* ranges = built.Data;
            const std::u8string path = cjk.u8string();
            const std::pair<ImFont*, float> targets[] = {{body, 13.0f}, {bodySmall, 12.0f}, {caption, 11.0f}, {medium, 13.0f},
                                                         {strong, 13.0f}, {title, 14.0f}, {display, 22.0f}};
            for (const auto& [font, size] : targets) {
                if (!font) continue;
                ImFontConfig merge;
                merge.MergeMode = true;
                merge.FontNo = face;
                merge.OversampleH = merge.OversampleV = 1;
                merge.DstFont = font;
                atlas->AddFontFromFileTTF(reinterpret_cast<const char*>(path.c_str()), size * scale, &merge, ranges);
            }
            log::Info("Interface font for {}: {}", language, reinterpret_cast<const char*>(path.c_str()));
        }
    }

    const bool ok = body && bodySmall && caption && medium && strong && title && display && mono && monoLarge;
    if (!ok) {
        atlas->Clear();
        ImFont* fallback = atlas->AddFontDefault();
        body = bodySmall = caption = medium = strong = title = display = mono = monoLarge = fallback;
    }
    return ok;
}

} // namespace bz::ui
