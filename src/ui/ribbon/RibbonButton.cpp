#include "ui/ribbon/RibbonButton.h"

#include "app/IconManager.h"
#include "app/Localization.h"
#include "ui/widgets/IconTint.h"

#include "imgui.h"

#include <cfloat>
#include <string>
#include <vector>

namespace BreezeCAD {
namespace UI {
namespace Ribbon {

int Button(IconManager& icons,
           const char* id, const char* const* opts, int optCount, int& idx,
           const char* icon, bool enabled, bool big, const char* tooltip,
           float iconScale) {
    int result = 0;
    const float bigW = 76.0f, bigH = 76.0f;
    const float smallW = 32.0f, smallH = 32.0f;
    const float arrowW = 14.0f;
    float w = big ? bigW : smallW;
    float h = big ? bigH : smallH;
    const int iconSize = (int)((big ? 32 : 24) * iconScale);
    const float o = 4.0f;
    bool hasArrow = (optCount > 1);
    float totalW = w + (hasArrow ? arrowW : 0.0f);

    bool isLight = (ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).x > 0.5f);
    ImVec4 bgHover, bgActive;
    if (isLight) {
        bgHover  = ImVec4(0.760f, 0.800f, 0.860f, 0.6f);
        bgActive = ImVec4(0.640f, 0.720f, 0.840f, 0.8f);
    } else {
        bgHover  = ImVec4(0.35f, 0.55f, 0.85f, 0.35f);
        bgActive = ImVec4(0.35f, 0.55f, 0.85f, 0.55f);
    }
    if (!enabled) {
        bgHover.w *= 0.4f; bgActive.w *= 0.4f;
    }

    ImVec4 tint = UI::Widgets::IconTintColor(enabled);
    GLuint tex = icons.Get(icon, iconSize);
    ImTextureID tid = (ImTextureID)(intptr_t)tex;

    ImGui::PushID(id);
    ImGui::BeginGroup();

    ImVec2 startPos = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("##click", ImVec2(totalW, h));
    bool clicked = ImGui::IsItemClicked();
    bool hovered = ImGui::IsItemHovered();
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();

    ImVec2 mp = ImGui::GetIO().MousePos;
    bool inArrow = hasArrow && (mp.x >= min.x + w);

    if (tooltip && hovered && !inArrow) ImGui::SetTooltip("%s", tooltip);
    if (inArrow && hovered) ImGui::SetTooltip("Открыть список вариантов");

    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImVec2 mainMax(min.x + w, max.y);
    if (hovered && enabled && !inArrow) {
        ImU32 bgCol = ImGui::GetColorU32(bgHover);
        dl->AddRectFilled(min, mainMax, bgCol, 4.0f);
    }

    if (hasArrow) {
        ImU32 arrowBg;
        if (isLight) {
            arrowBg = enabled ? IM_COL32(220, 225, 232, 220)
                              : IM_COL32(210, 215, 220, 140);
        } else {
            arrowBg = enabled ? IM_COL32(60, 70, 86, 200)
                              : IM_COL32(50, 58, 72, 130);
        }
        if (hovered && enabled && inArrow) {
            arrowBg = ImGui::GetColorU32(bgActive);
        }
        dl->AddRectFilled(ImVec2(min.x + w, min.y), max, arrowBg, 4.0f);
    }

    if (tid != 0) {
        ImVec2 iconPos(min.x + (w - iconSize) * 0.5f, min.y + o);
        dl->AddImage(tid, iconPos,
                     ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
                     ImVec2(0, 0), ImVec2(1, 1),
                     ImGui::GetColorU32(tint));
    } else {
        ImVec2 p1(min.x + (w - iconSize) * 0.5f, min.y + o);
        dl->AddRectFilled(p1, ImVec2(p1.x + iconSize, p1.y + iconSize),
                          IM_COL32(140, 140, 140, 160), 4.0f);
    }

    if (big && optCount > 0) {
        std::string label = Tr(opts[idx]);
        float maxTextW = w - 4.0f;
        ImFont* font = ImGui::GetFont();
        float baseSize = ImGui::GetFontSize();
        ImU32 txtCol = ImGui::GetColorU32(tint);

        float textTop    = min.y + o + iconSize + o;
        float textBottom = min.y + h - o;

        auto fits = [&](const std::string& s, float fs) {
            return font->CalcTextSizeA(fs, FLT_MAX, 0.0f, s.c_str()).x <= maxTextW;
        };
        auto splitMiddle = [&](const std::string& s) {
            std::vector<size_t> spaces;
            for (size_t i = 0; i < s.size(); ++i) if (s[i] == ' ') spaces.push_back(i);
            if (spaces.empty()) return std::pair<std::string,std::string>{s, ""};
            size_t pos = spaces[spaces.size() / 2];
            return std::pair<std::string,std::string>{s.substr(0, pos), s.substr(pos + 1)};
        };

        bool done = false;
        auto drawBottom = [&](const std::vector<std::string>& ls, float fs) {
            float lineH = fs + 1.0f;
            float y = textBottom - lineH * (float)ls.size();
            for (auto& s : ls) {
                ImVec2 sz = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, s.c_str());
                dl->AddText(font, fs,
                            ImVec2(min.x + (w - sz.x) * 0.5f, y),
                            txtCol, s.c_str());
                y += lineH;
            }
        };

        float sizes[] = { baseSize, baseSize * 0.82f, baseSize * 0.62f, baseSize * 0.52f };
        for (float fs : sizes) {
            if (done) break;
            if (fits(label, fs)) { drawBottom({label}, fs); done = true; break; }
            auto [l1, l2] = splitMiddle(label);
            if (!l2.empty() && fits(l1, fs) && fits(l2, fs)) {
                if (textTop + 2.0f * (fs + 1.0f) <= textBottom) {
                    drawBottom({l1, l2}, fs); done = true; break;
                }
            }
        }
        if (!done) drawBottom({label}, baseSize * 0.45f);
    }

    if (hasArrow) {
        GLuint arrowTex = icons.Get("chevron-down", 10);
        if (arrowTex != 0) {
            float ax = min.x + w + (arrowW - 10) * 0.5f;
            float ay = min.y + (h - 10) * 0.5f;
            dl->AddImage((ImTextureID)(intptr_t)arrowTex,
                         ImVec2(ax, ay), ImVec2(ax + 10, ay + 10),
                         ImVec2(0, 0), ImVec2(1, 1),
                         ImGui::GetColorU32(tint));
        }
    }

    if (clicked && enabled) {
        if (inArrow) result = 2;
        else result = 1;
    }

    ImGui::EndGroup();

    if (result == 2) ImGui::OpenPopup("##popup");
    if (ImGui::BeginPopup("##popup")) {
        for (int i = 0; i < optCount; i++) {
            bool selected = (i == idx);
            if (ImGui::MenuItem(Tr(opts[i]).c_str(), nullptr, selected)) {
                idx = i;
                result = 3;
            }
        }
        ImGui::EndPopup();
    }

    ImGui::PopID();
    return result;
}

int SingleButton(IconManager& icons,
                 const char* id, const char* key, const char* icon,
                 bool enabled, bool big, const char* tooltip, float iconScale) {
    // NOTE: static dummy — антипаттерн из RULES п.20 (записан как B-17 в REFACTOR_NOTES).
    static int dummy = 0;
    const char* opts[] = { key };
    return Button(icons, id, opts, 1, dummy, icon, enabled, big, tooltip, iconScale);
}

}}}
