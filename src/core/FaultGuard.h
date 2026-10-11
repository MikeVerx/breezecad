#pragma once
// A hardware fault (an access violation, a division by zero) inside the
// geometry kernel must cost one operation, not the whole program and the
// user's unsaved work. RunGuarded runs a piece of work so that such a fault
// ends that piece early instead.
//
// How: the fault handler jumps back to RunGuarded (a long jump), the way
// OCCT itself turns signals into exceptions on Linux. What the work had on
// its stack is not destroyed (a leak), so only self-contained work belongs
// here: computations on their own copies, whose results are taken after
// RunGuarded returned true. Only the thread that called RunGuarded is
// guarded; a fault anywhere else is a crash as before (reported by main).

#include <functional>
#include <string>

namespace bz::guard {

/// Runs `work`. False when a fault stopped it; `what` then names the fault.
/// C++ exceptions from `work` pass through unchanged.
bool RunGuarded(const std::function<void()>& work, std::string* what = nullptr);

} // namespace bz::guard
