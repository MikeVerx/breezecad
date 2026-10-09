#include "app/ShortcutPresets.h"

#include "core/I18n.h"

namespace bz::app {

const std::vector<ShortcutPreset>& ShortcutPresets() {
    // Sources: the programs' own help on default shortcuts. Only one-chord
    // shortcuts are taken (multi-letter aliases such as Inventor's "TR" are
    // not chords).
    static const std::vector<ShortcutPreset> presets{
        {"breezecad", N_("BreezeCAD (по умолчанию)"), {}},
        {"inventor",
         N_("Как в Autodesk Inventor"),
         {{"sketch.start", "S"},
          {"sketch.line", "L"},
          {"sketch.circle", "C"},
          {"sketch.dimension", "D"},
          {"feature.extrude", "E"},
          {"feature.revolve", "R"},
          {"feature.hole", "H"},
          {"feature.fillet", "F"},
          {"feature.workPlane", "RightBracket"},
          {"sketch.showConstraints", "F8"},
          {"sketch.hideConstraints", "F9"},
          {"view.home", "F6"},
          {"view.fit", "Home"},
          {"sketch.lookAt", "PageUp"},
          {"edit.redo", "Ctrl+Y"}}},
        {"kompas",
         N_("Как в КОМПАС-3D"),
         {{"view.fit", "F9"},
          {"edit.redo", "Ctrl+Y"},
          {"file.close", "Ctrl+W"},
          {"app.search", "Alt+Slash"}}},
        {"blender",
         N_("Как в Blender"),
         {{"sketch.move", "G"},
          {"sketch.rotate", "R"},
          {"sketch.scale", "S"},
          {"sketch.rectangle", "Shift+R"},
          {"sketch.construction", "Shift+G"},
          {"sketch.copy", "Shift+D"},
          {"sketch.mirror", "Ctrl+M"},
          {"feature.extrude", "E"},
          {"edit.delete", "X"},
          {"edit.redo", "Ctrl+Shift+Z"},
          {"app.search", "F3"},
          {"view.fit", "Home"},
          {"view.home", "Shift+C"}}},
    };
    return presets;
}

const ShortcutPreset* FindShortcutPreset(std::string_view id) {
    for (const ShortcutPreset& p : ShortcutPresets())
        if (p.id == id) return &p;
    return nullptr;
}

} // namespace bz::app
