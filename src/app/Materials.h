#pragma once
// Materials the app knows the density of (for mass in "МЦХ"). A part's
// material is free text in its properties; a name from this list gives it
// a density.

#include <span>
#include <string_view>

namespace bz::app {

struct Material {
    std::string_view name;
    double           density;   ///< g/cm^3
};

std::span<const Material> KnownMaterials();

/// The known material called `name` (exact match), or nullptr.
const Material* FindMaterial(std::string_view name);

} // namespace bz::app
