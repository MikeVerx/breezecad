#pragma once
// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 MikeVerx
// A prompt over the view: a rounded plate at the top middle. A text wider
// than the view wraps (the phone's view is narrow); on the phone it starts
// under the floating islands (ViewportFrame::promptTop).

#include "app/AppContext.h"
#include "ui/Theme.h"
#include "ui/Ui.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>

namespace bz::app {

inline void DrawViewPrompt(ui::Ui& ui, ImDrawList* dl, const ViewportFrame& vf, const char* text, Color color) {
    ImFont* font = ui.fonts.medium;
    const float fs = ui.S(13), pad = ui.S(14);
    const float wrap = std::max(ui.S(120), vf.max.x - vf.min.x - ui.S(24) - pad * 2);
    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, wrap, text);
    const float top = vf.promptTop > 0 ? vf.promptTop : vf.min.y + ui.S(14);
    const ImVec2 p{(vf.min.x + vf.max.x - ts.x) / 2 - pad, top};
    const float h = std::max(ui.S(34), ts.y + ui.S(18));
    dl->AddRectFilled(p, {p.x + ts.x + pad * 2, p.y + h}, ui::ToU32(ui.C().panel.WithAlpha(0.94f)), std::min(ui.S(17), h / 2));
    dl->AddText(font, fs, {p.x + pad, p.y + (h - ts.y) / 2}, ui::ToU32(color), text, nullptr, wrap);
}

} // namespace bz::app
