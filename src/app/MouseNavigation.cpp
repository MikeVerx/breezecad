#include "app/MouseNavigation.h"

#include "core/I18n.h"

namespace bz::app {

const std::vector<MousePreset>& MousePresets() {
    using B = MouseButtonChoice;
    using M = ModifierChoice;
    static const std::vector<MousePreset> presets{
        {"breezecad", N_("BreezeCAD (по умолчанию)"), {{B::Right, M::None}, {B::Middle, M::None}, {}, false}},
        {"inventor", N_("Как в Autodesk Inventor"), {{B::Middle, M::Shift}, {B::Middle, M::None}, {}, false}},
        {"kompas", N_("Как в КОМПАС-3D"), {{B::Middle, M::None}, {B::Middle, M::Shift}, {B::Middle, M::Ctrl}, false}},
        {"blender", N_("Как в Blender"), {{B::Middle, M::None}, {B::Middle, M::Shift}, {B::Middle, M::Ctrl}, false}},
        {"solidworks", N_("Как в SolidWorks"), {{B::Middle, M::None}, {B::Middle, M::Ctrl}, {B::Middle, M::Shift}, false}},
    };
    return presets;
}

const MousePreset* FindMousePreset(std::string_view id) {
    for (const MousePreset& p : MousePresets())
        if (p.id == id) return &p;
    return nullptr;
}

std::string GestureText(const MouseGesture& g) {
    if (g.button == MouseButtonChoice::None) return "—";
    std::string out;
    switch (g.modifier) {
        case ModifierChoice::Shift: out = "Shift + "; break;
        case ModifierChoice::Ctrl:  out = "Ctrl + "; break;
        case ModifierChoice::Alt:   out = "Alt + "; break;
        case ModifierChoice::None:  break;
    }
    out += g.button == MouseButtonChoice::Right ? Tr("ПКМ") : Tr("зажатое колесо");
    return out;
}

} // namespace bz::app
