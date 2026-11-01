#pragma once
// Linux: the files Windows packs as resources (setup.rc.in) are linked into
// the installer by the assembler (.incbin, resources.S made by CMake).
#include <cstdint>
#include <span>
#include <string>

namespace bz::setup {
/// The embedded file by its resource name ("FONT_REGULAR", "STR_de", "ICON_part"); empty when missing.
std::span<const std::uint8_t> EmbeddedResource(const std::string& name);
} // namespace bz::setup
