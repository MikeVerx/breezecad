#pragma once
// Ready-made shortcut sets for people coming from other programs. Each is a
// list of changes to BreezeCAD's own shortcuts. The program names describe
// which habits a set follows; they belong to their owners.

#include <string_view>
#include <utility>
#include <vector>

namespace bz::app {

struct ShortcutPreset {
    std::string_view id;      ///< stored in settings: "inventor"
    std::string_view label;   ///< "Как в Inventor"
    std::vector<std::pair<std::string_view, std::string_view>> changes;   ///< action id -> chord text ("" removes)
};

const std::vector<ShortcutPreset>& ShortcutPresets();
const ShortcutPreset* FindShortcutPreset(std::string_view id);

} // namespace bz::app
