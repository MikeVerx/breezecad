#pragma once
// Android's input events (AInputEvent) for the program: pointers to the
// touch input (mobile/TouchInput), keys to ImGui and the shortcuts.

#if defined(__ANDROID__)

#include "app/mobile/MobileState.h"

#include <imgui.h>

#include <functional>
#include <vector>

namespace bz::app::android {

struct InputSink {
    std::function<void(const mobile::PointerEvent&)> pointer;
    std::function<void(ImGuiKeyChord)>               chord;    ///< a key pressed with its modifiers (shortcuts)
    std::function<void()>                            woke;     ///< any input: draw frames
    std::function<bool()>                            backLeaves;   ///< "back" now leaves the app (nothing to close in it)
    std::function<bool(int direction)>               volume;   ///< a volume key: zoom in (+1) or out (-1); false: the system's
    float density = 1.0f;
};

/// Handles one event; returns 1 when the program took it.
int HandleInput(const void* event, const InputSink& sink);

/// Text from the system keyboard (Java) into ImGui.
void FeedTypedText();

} // namespace bz::app::android

#endif
