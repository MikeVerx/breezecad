#include "HomeScreen.h"
#include "Thumbnails.h"
#include "app/IconManager.h"
#include "ui/theme/ThemeTokens.h"
#include "ui/widgets/IconTint.h"

#include "imgui.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>

// stb_image externs
extern "C" unsigned char* stbi_load(const char* filename, int* x, int* y,
                                    int* channels_in_file, int desired_channels);
extern "C" void stbi_image_free(void* retval_from_stbi_load);

#include <GL/gl.h>

namespace BreezeCAD::UI::Home {

void DrawHomeScreen(const HomeContext& ctx) {
    if (!(*ctx.showHomeScreen)) return;

    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y - 76.0f));  // -76 = таб 52 + статус 24
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ctx.theme->bgContent);
    ImGui::Begin("##home_bg", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    const float titleH  = 32.0f;              // титульная полоса
    const float topBarH = 76.0f;              // топ-полоса с кнопками
    const float topH    = titleH + topBarH;   // = 108
    const float leftW   = 240.0f;
    const float pad     = 16.0f;

    // ===== ТИТУЛЬНАЯ ПОЛОСА (32 px) =====
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(0, 0), ImVec2(io.DisplaySize.x, titleH),
        ctx.theme->bgTitle);

    ImGui::SetCursorPos(ImVec2(pad, 8));
    ImGui::PushStyleColor(ImGuiCol_Text, ctx.theme->textMain);
    ImGui::TextUnformatted("BreezeCAD");
    ImGui::PopStyleColor();

    {
        char telBuf[128];
        std::snprintf(telBuf, sizeof(telBuf), "FPS: %.0f | RAM: %.0f MB",
                      io.Framerate, ctx.ramMB);
        ImVec2 tsz = ImGui::CalcTextSize(telBuf);
        float tx = io.DisplaySize.x - pad - tsz.x;
        ImGui::SetCursorPos(ImVec2(tx, 8));
        ImGui::TextDisabled("%s", telBuf);
    }

    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(0, titleH), ImVec2(io.DisplaySize.x, titleH),
        ctx.theme->border, 1.0f);

    // ===== ТОП-ПОЛОСА (76 px): Деталь / Сборка / Открыть =====
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(0, titleH), ImVec2(io.DisplaySize.x, topH),
        ctx.theme->bgRibbon);

    const float hbtnW   = 88.0f;
    const float hbtnH   = 74.0f;
    const float hbtnY   = titleH + (topBarH - hbtnH) * 0.5f;
    const float hbtnGap = 8.0f;
    const char* hbtnIcons[3]  = { "box", "boxes", "folder-open" };
    const char* hbtnLabels[3] = { "Деталь", "Сборка", "Открыть" };
    float hbx = pad;
    for (int bi = 0; bi < 3; bi++) {
        ImGui::PushID(1000 + bi);
        ImGui::SetCursorPos(ImVec2(hbx, hbtnY));
        ImGui::PushStyleColor(ImGuiCol_Button,        ctx.theme->homeBtnBg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ctx.theme->homeBtnBgHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ctx.theme->accent);
        bool hbPressed = ImGui::Button("##hbtn", ImVec2(hbtnW, hbtnH));
        ImGui::PopStyleColor(3);

        ImVec2 bmin = ImGui::GetItemRectMin();
        GLuint icoTex = ctx.icons->Get(hbtnIcons[bi], 32);
        if (icoTex) {
            float ix = bmin.x + (hbtnW - 32) * 0.5f;
            float iy = bmin.y + 12.0f;
            ImGui::GetWindowDrawList()->AddImage(
                (ImTextureID)(intptr_t)icoTex,
                ImVec2(ix, iy), ImVec2(ix + 32, iy + 32),
                ImVec2(0, 0), ImVec2(1, 1),
                ImGui::GetColorU32(UI::Widgets::IconTintColor(true)));
        }
        ImVec2 lsz = ImGui::CalcTextSize(hbtnLabels[bi]);
        float lx = bmin.x + (hbtnW - lsz.x) * 0.5f;
        float ly = bmin.y + hbtnH - lsz.y - 8.0f;
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(lx, ly), ctx.theme->homeBtnLabel, hbtnLabels[bi]);

        if (hbPressed) {
            if (bi == 0)      if (ctx.onCreateDoc) ctx.onCreateDoc();
            else if (bi == 1) if (ctx.onCreateDoc) ctx.onCreateDoc();   // TODO: сборка
            else if (bi == 2) if (ctx.onOpenFile) ctx.onOpenFile();
        }
        ImGui::PopID();
        hbx += hbtnW + hbtnGap;
    }

    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(0, topH), ImVec2(io.DisplaySize.x, topH),
        IM_COL32(60, 68, 82, 255), 1.0f);
    // ЛЕВАЯ КОЛОНКА
    ImGui::SetCursorPos(ImVec2(0, topH));
    ImGui::BeginChild("##home_left", ImVec2(leftW, io.DisplaySize.y - topH - 76.0f),
                      false, ImGuiWindowFlags_NoScrollbar);

    const float colH = io.DisplaySize.y - topH - 76.0f;

    // ===== Название BreezeCAD (крупно, полужирно) =====
    {
        ImFont* font = ImGui::GetFont();
        const float bigSize = 26.0f;
        const char* nm = "BreezeCAD";
        ImVec2 base = ImGui::GetCursorScreenPos();
        ImVec2 tsz = font->CalcTextSizeA(bigSize, FLT_MAX, 0.0f, nm);
        ImVec2 pos(base.x + (leftW - tsz.x) * 0.5f, base.y + 22.0f);
        ImU32 colText = ctx.theme->textMain;
        // Псевдо-жирный: дважды с 0.6 px
        ImGui::GetWindowDrawList()->AddText(font, bigSize, pos, colText, nm);
        ImGui::GetWindowDrawList()->AddText(font, bigSize, ImVec2(pos.x + 0.6f, pos.y), colText, nm);
    }

    // ===== Логотип 140x140 (под названием) =====
    {
        const float logoSz = 140.0f;
        float logoX = (leftW - logoSz) * 0.5f;
        float logoY = 22.0f + 36.0f + 16.0f;  // название + gap
        GLuint logo = ctx.icons->Get("breeze-logo", (int)logoSz);
        if (logo) {
            ImGui::SetCursorPos(ImVec2(logoX, logoY));
            ImGui::Image((ImTextureID)(intptr_t)logo, ImVec2(logoSz, logoSz));
        }
    }

    // ===== Хелпер: кнопка с фиксированной шириной и центрированным текстом =====
    auto homeBtn = [&](const char* id, const char* label, float w, float h, bool disabled) -> bool {
        ImGui::PushID(id);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##btn", ImVec2(w, h));
        bool clicked = ImGui::IsItemClicked();
        bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        ImU32 bg;
        if (disabled)      bg = ctx.theme->homeBtnBgDisabled;
        else if (clicked)  bg = ctx.theme->accent;
        else if (hovered)  bg = ctx.theme->homeBtnBgHover;
        else               bg = ctx.theme->homeBtnBg;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, 4.0f);
        ImVec2 tsz = ImGui::CalcTextSize(label);
        ImU32 tcol = disabled ? ctx.theme->homeBtnTextDisabled : ctx.theme->homeBtnLabel;
        dl->AddText(ImVec2(p.x + (w - tsz.x) * 0.5f, p.y + (h - tsz.y) * 0.5f), tcol, label);
        return clicked && !disabled;
    };

    // ===== Позиции в левой колонке (абсолютные Y от низа) =====
    // Схема снизу вверх:
    //   20   нижий отступ
    //   32   Параметры/Выход
    //   20   gap
    //   32   О программе
    //    6   gap
    //   32   Справка
    //    6   gap
    //   32   Управление модулями (disabled)
    //   16   gap
    //    1   разделитель
    //    8   gap
    //   36   Версия (2 строки)
    const float bh      = 32.0f;
    const float gap     = 6.0f;
    const float bwFull  = leftW - pad * 2.0f;
    const float yParamsTop  = colH - 20.0f - bh;
    const float yAboutTop   = yParamsTop - 20.0f - bh;
    const float yHelpTop    = yAboutTop - gap - bh;
    const float yModulesTop = yHelpTop  - gap - bh;
    const float ySepY       = yModulesTop - 16.0f;
    const float yVerTop     = ySepY - 8.0f - 36.0f;

    // ===== Версия (над разделителем) =====
    {
        const char* v1 = "Версия 0.1.x";
        const char* v2 = "Прототип";
        ImVec2 s1 = ImGui::CalcTextSize(v1);
        ImVec2 s2 = ImGui::CalcTextSize(v2);
        ImGui::SetCursorPos(ImVec2((leftW - s1.x) * 0.5f, yVerTop));
        ImGui::TextDisabled("%s", v1);
        ImGui::SetCursorPos(ImVec2((leftW - s2.x) * 0.5f, yVerTop + 18.0f));
        ImGui::TextDisabled("%s", v2);
        ImGui::SetCursorPos(ImVec2(pad, ySepY));
        ImGui::Separator();
    }

    // ===== 3 кнопки внизу колонки (абсолютные Y) =====
    ImGui::SetCursorPos(ImVec2(pad, yModulesTop));
    homeBtn("btn_modules", "Управление модулями", bwFull, bh, true);
    ImGui::SetCursorPos(ImVec2(pad, yHelpTop));
    if (homeBtn("btn_help", "Справка", bwFull, bh, false)) if (ctx.onNotImplemented) ctx.onNotImplemented();
    ImGui::SetCursorPos(ImVec2(pad, yAboutTop));
    if (homeBtn("btn_about", "О программе", bwFull, bh, false)) if (ctx.onShowSettings) ctx.onShowSettings();

    // ===== Параметры / Выход (самый низ) =====
    {
        const float bw2 = (bwFull - 8.0f) * 0.5f;
        ImGui::SetCursorPos(ImVec2(pad, yParamsTop));
        if (homeBtn("btn_settings", "Параметры", bw2, bh, false)) if (ctx.onShowSettings) ctx.onShowSettings();
        ImGui::SetCursorPos(ImVec2(pad + bw2 + 8.0f, yParamsTop));
        if (homeBtn("btn_exit", "Выход", bw2, bh, false)) {
            if (ctx.onQuit) ctx.onQuit();
        }
    }
    ImGui::EndChild();
    // ПРАВАЯ ОБЛАСТЬ
    const float rightX = leftW + 1.0f;
    const float rightW = io.DisplaySize.x - rightX;
    const float contentY = topH;

    ImGui::SetCursorPos(ImVec2(rightX, contentY));
    ImGui::BeginChild("##home_right", ImVec2(rightW, io.DisplaySize.y - topH - 76.0f),
                      false, ImGuiWindowFlags_NoScrollbar);

    // Заголовок по центру
    {
        const char* h = "Последние документы";
        ImVec2 hs = ImGui::CalcTextSize(h);
        ImGui::SetCursorPos(ImVec2((rightW - hs.x) * 0.5f, 20.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ctx.theme->textMain);
        ImGui::TextUnformatted(h);
        ImGui::PopStyleColor();
    }

    // Тумблер Плитки/Список + Поиск
    {
        const float toggleY = 60.0f;
        const float tgW = 36.0f;
        const float tgH = 28.0f;

        // Тумблер слева
        ImGui::SetCursorPos(ImVec2(pad, toggleY));
        ImGui::PushID(2001);
        bool activeTiles = ((*ctx.viewMode) == 0);
        ImGui::PushStyleColor(ImGuiCol_Button, activeTiles
            ? ImVec4(0.20f, 0.55f, 0.95f, 1.0f)
            : ImVec4(0.20f, 0.23f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.40f, 0.55f, 1.0f));
        if (ImGui::Button("##tg_tiles", ImVec2(tgW, tgH))) (*ctx.viewMode) = 0;
        ImGui::PopStyleColor(2);
        {
            ImVec2 mn = ImGui::GetItemRectMin();
            GLuint ico = ctx.icons->Get("layout-grid", 18);
            if (ico) {
                ImGui::GetWindowDrawList()->AddImage(
                    (ImTextureID)(intptr_t)ico,
                    ImVec2(mn.x + (tgW - 18) * 0.5f, mn.y + (tgH - 18) * 0.5f),
                    ImVec2(mn.x + (tgW + 18) * 0.5f, mn.y + (tgH + 18) * 0.5f),
                    ImVec2(0, 0), ImVec2(1, 1),
                    ImGui::GetColorU32(UI::Widgets::IconTintColor(true)));
            }
        }
        ImGui::PopID();

        ImGui::SameLine(0, 4);
        ImGui::PushID(2002);
        ImGui::PushStyleColor(ImGuiCol_Button, !activeTiles
            ? ImVec4(0.20f, 0.55f, 0.95f, 1.0f)
            : ImVec4(0.20f, 0.23f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.40f, 0.55f, 1.0f));
        if (ImGui::Button("##tg_list", ImVec2(tgW, tgH))) (*ctx.viewMode) = 1;
        ImGui::PopStyleColor(2);
        {
            ImVec2 mn = ImGui::GetItemRectMin();
            GLuint ico = ctx.icons->Get("list", 18);
            if (ico) {
                ImGui::GetWindowDrawList()->AddImage(
                    (ImTextureID)(intptr_t)ico,
                    ImVec2(mn.x + (tgW - 18) * 0.5f, mn.y + (tgH - 18) * 0.5f),
                    ImVec2(mn.x + (tgW + 18) * 0.5f, mn.y + (tgH + 18) * 0.5f),
                    ImVec2(0, 0), ImVec2(1, 1),
                    ImGui::GetColorU32(UI::Widgets::IconTintColor(true)));
            }
        }
        ImGui::PopID();

        // Поиск справа
        const float searchW = 260.0f;
        ImGui::SetCursorPos(ImVec2(rightW - pad - searchW, toggleY));
        ImGui::SetNextItemWidth(searchW);
        ImGui::InputTextWithHint("##home_search", "Поиск...",
                                 ctx.searchBuffer, ctx.searchBufferSize);
    }

    // Контент
    const float listY = 110.0f;
    const float listH = io.DisplaySize.y - topH - 76.0f - listY - 16.0f;
    ImGui::SetCursorPos(ImVec2(pad, listY));
    ImGui::BeginChild("##home_content", ImVec2(rightW - pad * 2, listH), false,
                      ImGuiWindowFlags_NoScrollbar);

    std::string searchLower = ctx.searchBuffer;
    for (auto& ch : searchLower) ch = (char)std::tolower((unsigned char)ch);

    if ((*ctx.viewMode) == 0) {
        // ===== ПЛИТКИ =====
        const float tileW = 200.0f;
        const float tileH = 200.0f;
        const float spacing = 16.0f;
        int col = 0; float x = 0, y = 0;
        bool any = false;
        for (int i = 0; i < (int)ctx.documentCount; i++) {
            std::string nm = (*ctx.projectNames)[i];
            std::string lname = nm;
            for (auto& ch : lname) ch = (char)std::tolower((unsigned char)ch);
            if (!searchLower.empty() && lname.find(searchLower) == std::string::npos) continue;
            any = true;
            ImGui::PushID(i);
            ImGui::SetCursorPos(ImVec2(x, y));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ctx.theme->bgCardBg);
            ImGui::PushStyleColor(ImGuiCol_Border,  ctx.theme->borderStrong);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
            ImGui::BeginChild(("##tile_" + std::to_string(i)).c_str(),
                              ImVec2(tileW, tileH), true, ImGuiWindowFlags_NoScrollbar);
            ImVec2 thumbPos = ImGui::GetCursorScreenPos();
            ImVec2 thumbSize(tileW - 24, 120);
            bool thumbLoaded = false;
            std::string thumbPath = UI::Home::GetThumbnailPath(*ctx.thumbnails, i);
            if (!thumbPath.empty() && std::filesystem::exists(thumbPath)) {
                static std::map<std::string, ImTextureID> s_thumbs;
                auto cacheIt = s_thumbs.find(thumbPath);
                if (cacheIt == s_thumbs.end()) {
                    int w = 0, h = 0, channels = 0;
                    unsigned char* data = ::stbi_load(thumbPath.c_str(), &w, &h, &channels, 4);
                    if (data) {
                        GLuint texId = 0;
                        glGenTextures(1, &texId);
                        glBindTexture(GL_TEXTURE_2D, texId);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
                        ::stbi_image_free(data);
                        s_thumbs[thumbPath] = (ImTextureID)(intptr_t)texId;
                        cacheIt = s_thumbs.find(thumbPath);
                    }
                }
                if (cacheIt != s_thumbs.end()) {
                    ImGui::GetWindowDrawList()->AddImage(cacheIt->second, thumbPos,
                        ImVec2(thumbPos.x + thumbSize.x, thumbPos.y + thumbSize.y));
                    thumbLoaded = true;
                }
            }
            if (!thumbLoaded) {
                ImGui::GetWindowDrawList()->AddRectFilled(thumbPos,
                    ImVec2(thumbPos.x + thumbSize.x, thumbPos.y + thumbSize.y),
                    ctx.theme->thumbBg, 6.0f);
                ImVec2 ts = ImGui::CalcTextSize("[ 3D ]");
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(thumbPos.x + (thumbSize.x - ts.x) * 0.5f,
                           thumbPos.y + (thumbSize.y - ts.y) * 0.5f),
                    ctx.theme->textDim, "[ 3D ]");
            }
            ImGui::SetCursorPosY(132);
            ImGui::Text("%s", (*ctx.projectNames)[i].c_str());
            ImGui::SetCursorPosY(152);
            ImGui::TextDisabled("%s", (*ctx.projectPaths)[i].empty() ? "<не сохранён>" : (*ctx.projectPaths)[i].c_str());
            ImGui::SetCursorPosY(172);
            if (ImGui::Button("Открыть", ImVec2((tileW - 36) * 0.5f - 2, 0))) {
                if (ctx.onSwitchDoc) ctx.onSwitchDoc(i);
                (*ctx.showHomeScreen) = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Закрыть", ImVec2((tileW - 36) * 0.5f - 2, 0))) {
                if (ctx.onCloseDoc) ctx.onCloseDoc(i);
                ImGui::EndChild();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(2);
                ImGui::PopID();
                break;
            }
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
            ImGui::PopID();
            col++;
            if (col >= 4) { col = 0; x = 0; y += tileH + spacing; }
            else x += tileW + spacing;
        }
        if (!any) {
            ImGui::SetCursorPos(ImVec2(20, 40));
            ImGui::TextDisabled("Нет недавних документов. Создайте новый — кнопка «Деталь» сверху.");
        }
    } else {
        // ===== СПИСОК =====
        if (ImGui::BeginTable("##home_list", 4,
                ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Имя",           ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn("Расположение",   ImGuiTableColumnFlags_WidthStretch, 4.0f);
            ImGui::TableSetupColumn("Дата",           ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableSetupColumn("Размер",         ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableHeadersRow();
            for (int i = 0; i < (int)ctx.documentCount; i++) {
                std::string nm = (*ctx.projectNames)[i];
                std::string lname = nm;
                for (auto& ch : lname) ch = (char)std::tolower((unsigned char)ch);
                if (!searchLower.empty() && lname.find(searchLower) == std::string::npos) continue;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(i);
                bool sel = ImGui::Selectable(nm.c_str(), false,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
                if (sel && ImGui::IsMouseDoubleClicked(0)) {
                    if (ctx.onSwitchDoc) ctx.onSwitchDoc(i);
                    (*ctx.showHomeScreen) = false;
                }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s", (*ctx.projectPaths)[i].empty() ? "<не сохранён>" : (*ctx.projectPaths)[i].c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled("—");
                ImGui::TableSetColumnIndex(3);
                ImGui::TextDisabled("—");
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    ImGui::EndChild();  // ##home_right
    ImGui::End();
    ImGui::PopStyleColor();
}


} // namespace BreezeCAD::UI::Home
