#include "app/Backgrounds.h"

#include "core/I18n.h"

#include <algorithm>
#include <array>

namespace bz::app {
namespace {

constexpr std::array<BackgroundPreset, 12> kPresets{{
    {"theme", N_("Как в теме"), {}, {}, false, true},
    {"light", N_("Светлый ровный"), {}, {}, false, true},
    {"dark", N_("Тёмный ровный"), {}, {}, false, true},
    {"white", N_("Белый"), Color::Hex(0xFFFFFF), Color::Hex(0xFFFFFF)},
    {"studio", N_("Студия"), Color::Hex(0xFFFFFF), Color::Hex(0xC9CED6), true},
    {"sky", N_("Небо"), Color::Hex(0x8FB8E8), Color::Hex(0xEAF2FB)},
    {"warm", N_("Тёплый свет"), Color::Hex(0xF6EBDD), Color::Hex(0xD9C7B0)},
    {"cool", N_("Холодный свет"), Color::Hex(0xE8EEF5), Color::Hex(0xAFC0D3)},
    {"workshop", N_("Мастерская"), Color::Hex(0xB9B4AA), Color::Hex(0x6F6A62)},
    {"graphite", N_("Графит"), Color::Hex(0x4A5059), Color::Hex(0x1E2126), true},
    {"night", N_("Ночь"), Color::Hex(0x1C2433), Color::Hex(0x0B0F17)},
    {"blueprint", N_("Синька"), Color::Hex(0x1F4E8C), Color::Hex(0x163A6B)},
}};

} // namespace

std::span<const BackgroundPreset> BackgroundPresets() { return kPresets; }

const BackgroundPreset& FindBackground(std::string_view id) {
    const auto it = std::ranges::find_if(kPresets, [&](const BackgroundPreset& p) { return id == p.id; });
    return it != kPresets.end() ? *it : kPresets.front();
}

BackgroundColours ColoursOf(const BackgroundPreset& preset, const ui::ThemeColors& c) {
    const std::string_view id = preset.id;
    if (id == "theme") return {c.viewportTop, c.viewportBottom};
    if (id == "light") return {c.viewportTop, c.viewportTop};
    if (id == "dark") return {c.viewportBottom, c.viewportBottom};
    return {preset.top, preset.bottom, preset.radial};
}

} // namespace bz::app
