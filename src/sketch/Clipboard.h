#pragma once
// The sketch clipboard (22.10.2026, as in KOMPAS): the chosen shapes taken
// with a base point the user picks, put into any sketch of any part with
// that point where the cursor is — as many times as wanted.
//
// What is copied: curves (lines, arcs, circles, ellipses, splines, texts) with
// their points, single points, and the constraints wholly among them. Sizes
// become plain numbers (they no longer depend on the variables of the part
// they came from); a fixed point and a symmetry with a line left behind do
// not come along. Projected model edges come as ordinary geometry.

#include "core/Id.h"
#include "sketch/Sketch.h"

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace bz::sketch {

struct Clipboard {
    Sketch shapes;   ///< positions relative to the base point (it is 0, 0)
    bool Empty() const;
};

/// The shapes of `entities` in `sk`, the base point `base` at 0, 0.
Clipboard CopyToClipboard(const Sketch& sk, std::span<const ObjectId> entities, Vec2 base);

/// A copy of `clip` in `sk`, its base point at `at`. `nextName` names each
/// size the copy brings ("d7"…: the document's next parameter names). The
/// document must evaluate its parameters and solve the sketch after.
/// Returns the new curves and points (to choose them).
std::vector<ObjectId> PasteFromClipboard(Sketch& sk, IdGenerator& ids, const Clipboard& clip, Vec2 at,
                                         const std::function<std::string()>& nextName);

} // namespace bz::sketch
