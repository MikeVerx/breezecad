#include "app/MouseNavigation.h"

namespace bz::app {

const std::vector<MousePreset>& MousePresets() {
    using B = MouseButtonChoice;
    using M = ModifierChoice;
    static const std::vector<MousePreset> presets{
        {"breezecad", "BreezeCAD (по умолчанию)", {{B::Right, M::None}, {B::Middle, M::None}, {}, false}},
        {"inventor", "Как в Autodesk Inventor", {{B::Middle, M::Shift}, {B::Middle, M::None}, {}, false}},
        {"kompas", "Как в КОМПАС-3D", {{B::Middle, M::None}, {B::Middle, M::Shift}, {B::Middle, M::Ctrl}, false}},
        {"blender", "Как в Blender", {{B::Middle, M::None}, {B::Middle, M::Shift}, {B::Middle, M::Ctrl}, false}},
        {"solidworks", "Как в SolidWorks", {{B::Middle, M::None}, {B::Middle, M::Ctrl}, {B::Middle, M::Shift}, false}},
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
    out += g.button == MouseButtonChoice::Right ? "ПКМ" : "зажатое колесо";
    return out;
}

} // namespace bz::app
