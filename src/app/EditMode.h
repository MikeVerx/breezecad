#pragma once

namespace bz::app {

enum class EditMode : unsigned char {
    Model,       ///< the part with its tree
    PickPlane,   ///< "Начать эскиз": waiting for a plane or a flat face
    Sketch,      ///< editing a sketch
};

/// Whether "Разрез вида" is shown in this mode: the model, a sketch and the
/// picking of a plane for one (a sketch drawn inside a cut part is what the
/// cut is for; going into a sketch must not take it away).
constexpr bool SectionApplies(EditMode mode) {
    return mode == EditMode::Model || mode == EditMode::Sketch || mode == EditMode::PickPlane;
}

} // namespace bz::app
