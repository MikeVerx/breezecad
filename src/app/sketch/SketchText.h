#pragma once
// Sketch text as the editor sees it: the letters placed in their box, in
// sketch coordinates (the model builds the same letters, see kernel/occt).

#include "kernel/TextGeometry.h"
#include "sketch/Sketch.h"

#include <vector>

namespace bz::app {

kernel::TextFormat FormatOf(const sketch::Text& t);

/// The outlines of the letters in sketch coordinates.
std::vector<std::vector<Vec2>> TextLoops(const sketch::Sketch& sk, const sketch::Text& t);

/// True when `p` (sketch coordinates) is inside the text's box.
bool InsideTextBox(const sketch::Sketch& sk, const sketch::Text& t, Vec2 p);

} // namespace bz::app
