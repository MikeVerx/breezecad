#include "app/Materials.h"

#include <algorithm>
#include <array>

namespace bz::app {
namespace {

// Typical handbook densities; good to a few percent, as mass estimates need.
constexpr std::array<Material, 9> kMaterials{{
    {"Сталь", 7.85},
    {"Нерж. сталь", 7.93},
    {"Алюминий", 2.70},
    {"Латунь", 8.50},
    {"Медь", 8.96},
    {"Титан", 4.51},
    {"ABS", 1.04},
    {"PLA", 1.24},
    {"PETG", 1.27},
}};

} // namespace

std::span<const Material> KnownMaterials() { return kMaterials; }

const Material* FindMaterial(std::string_view name) {
    const auto it = std::ranges::find(kMaterials, name, &Material::name);
    return it != kMaterials.end() ? &*it : nullptr;
}

} // namespace bz::app
