#include "app/Notifications.h"

#include <algorithm>

namespace bz::app {

void Notifications::Push(std::string text, Kind kind, double seconds) {
    // The same message twice in a row just restarts its timer.
    if (!myNotes.empty() && myNotes.back().text == text) {
        myNotes.back().shownAt = -1.0;
        return;
    }
    myNotes.push_back({std::move(text), kind, seconds});
    if (myNotes.size() > 4) myNotes.erase(myNotes.begin());
}

bool Notifications::Draw(ui::Ui& ui, ImVec2 bottomRight, double now) {
    std::erase_if(myNotes, [&](const Note& n) { return n.shownAt >= 0 && now - n.shownAt > n.duration; });
    if (myNotes.empty()) return false;

    const ui::ThemeColors& c = ui.C();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ui.fonts.medium;
    const float fs = ui.S(13);
    float y = bottomRight.y - ui.S(16);
    for (auto it = myNotes.rbegin(); it != myNotes.rend(); ++it) {
        Note& n = *it;
        if (n.shownAt < 0) n.shownAt = now;
        const double age = now - n.shownAt;
        const float alpha = static_cast<float>(std::clamp(std::min(age / 0.15, (n.duration - age) / 0.4), 0.0, 1.0));

        const float maxWidth = ui.S(420);
        const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, maxWidth, n.text.c_str());
        const ImVec2 size{ts.x + ui.S(44), ts.y + ui.S(20)};
        const ImVec2 p{bottomRight.x - ui.S(16) - size.x, y - size.y};
        const Color bg = ui.theme.dark ? c.field : c.panel;
        dl->AddRectFilled({p.x, p.y + ui.S(3)}, {p.x + size.x, p.y + size.y + ui.S(3)},
                          ui::ToU32(Color{0, 0, 0, 0.12f * alpha}), ui.S(10));
        dl->AddRectFilled(p, {p.x + size.x, p.y + size.y}, ui::ToU32(bg.WithAlpha(alpha)), ui.S(10));
        dl->AddRect(p, {p.x + size.x, p.y + size.y}, ui::ToU32(c.divider.WithAlpha(alpha)), ui.S(10));
        const char* icon = n.kind == Kind::Error ? "status-error" : n.kind == Kind::Success ? "status-ok" : "ui-info";
        ui.icons.Draw(dl, icon, {p.x + ui.S(12), p.y + (size.y - ui.S(16)) / 2}, ui.S(16),
                      c.accent.WithAlpha(alpha));
        dl->AddText(font, fs, {p.x + ui.S(34), p.y + ui.S(10)}, ui::ToU32(c.text.WithAlpha(alpha)), n.text.c_str(),
                    nullptr, maxWidth);
        y = p.y - ui.S(8);
    }
    return true;
}

} // namespace bz::app
