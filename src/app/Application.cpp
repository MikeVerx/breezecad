#include "app/Application.h"
#include "app/Localization.h"
#include "app/IconsFontAwesome.h"
#include "doc/BreezeApplication.h"
#include "platform/FileDialog.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <TDF_ChildIterator.hxx>
#include <STEPControl_Writer.hxx>
#include <Interface_Static.hxx>
#include <StlAPI_Writer.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <OSD_MemInfo.hxx>
#include <TDataStd_Name.hxx>
#include <TDataStd_Integer.hxx>
#include <TCollection_AsciiString.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopExp_Explorer.hxx>
#include <BRep_Tool.hxx>
#include <TNaming_NamedShape.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <gp_Dir.hxx>
#include <gp_Trsf.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pln.hxx>

#define _USE_MATH_DEFINES
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

// stb_image extern declarations (глобальные, до namespace)
extern "C" unsigned char* stbi_load(const char* filename, int* x, int* y,
                                    int* channels_in_file, int desired_channels);
extern "C" void stbi_image_free(void* retval_from_stbi_load);

namespace BreezeCAD {

// === TRACE (enabled only when env BREEZECAD_TRACE is set) ===
static void TRACE(const char* msg) {
    static FILE* g_traceFile = nullptr;
    static bool g_enabled = (getenv("BREEZECAD_TRACE") != nullptr);
    if (!g_enabled) return;
    if (!g_traceFile) g_traceFile = fopen("breezecad_trace.log", "w");
    if (g_traceFile) {
        fprintf(g_traceFile, "%s\n", msg);
        fflush(g_traceFile);
    }
}

// Вспомогательная: получить tint-цвет иконки по текущей теме
static ImVec4 IconTintColor(bool enabled) {
    bool isLight = (ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).x > 0.5f);
    ImVec4 tint = isLight ? ImVec4(0.176f, 0.196f, 0.227f, 1.0f)   // #2d323a
                          : ImVec4(0.92f, 0.94f, 0.97f, 1.0f);
    if (!enabled) tint.w *= 0.45f;
    return tint;
}

// Хелпер: маленькая кнопка с SVG-иконкой
static bool SvgIconBtn(IconManager& icons, const char* id,
                       const char* name, int iconSize,
                       float btnW, float btnH,
                       const char* tooltip = nullptr,
                       bool transparentBg = false) {
    ImGui::PushID(id);

    ImVec4 tint = IconTintColor(true);
    GLuint tex = icons.Get(name, iconSize);
    ImTextureID tid = (ImTextureID)(intptr_t)tex;

    ImGui::InvisibleButton("##svgb", ImVec2(btnW, btnH));
    bool clicked = ImGui::IsItemClicked();
    bool hovered = ImGui::IsItemHovered();
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (!transparentBg) {
        ImU32 bg = ImGui::GetColorU32(hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
        dl->AddRectFilled(min, max, bg, 4.0f);
    } else if (hovered) {
        dl->AddRectFilled(min, max,
            ImGui::GetColorU32(ImGuiCol_ButtonHovered), 4.0f);
    }

    if (tid != 0) {
        ImVec2 ip(min.x + (btnW - iconSize) * 0.5f,
                  min.y + (btnH - iconSize) * 0.5f);
        dl->AddImage(tid, ip,
                     ImVec2(ip.x + iconSize, ip.y + iconSize),
                     ImVec2(0, 0), ImVec2(1, 1),
                     ImGui::GetColorU32(tint));
    } else {
        dl->AddRectFilled(ImVec2(min.x + 4, min.y + 4),
                          ImVec2(max.x - 4, max.y - 4),
                          IM_COL32(140, 140, 140, 180), 3.0f);
    }

    if (hovered && tooltip) ImGui::SetTooltip("%s", tooltip);

    ImGui::PopID();
    return clicked;
}

// Обёртка для QAT
static bool QatIconButton(IconManager& icons, const char* id,
                          const char* name, int size,
                          const char* tooltip = nullptr) {
    return SvgIconBtn(icons, id, name, size, 32, 28, tooltip, true);
}







// Чистый C++ конвертеры (см. BreezeDocument.cpp — там идентичные, но локальные)

static TCollection_ExtendedString Utf8ToExt(const std::string& s) {
    if (s.empty()) return TCollection_ExtendedString();
    std::wstring ws;
    ws.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp = 0;
        int extra = 0;
        if      (c < 0x80) { cp = c;          extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else { i++; continue; }
        if (i + extra >= s.size()) break;
        for (int k = 1; k <= extra; k++) {
            unsigned char cc = (unsigned char)s[i + k];
            if ((cc & 0xC0) != 0x80) { cp = 0; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        i += extra + 1;
        if (cp < 0x10000) {
            ws.push_back((wchar_t)cp);
        } else if (cp <= 0x10FFFF) {
            cp -= 0x10000;
            ws.push_back((wchar_t)(0xD800 + (cp >> 10)));
            ws.push_back((wchar_t)(0xDC00 + (cp & 0x3FF)));
        }
    }
    ws.push_back(0);
    return TCollection_ExtendedString(ws.c_str());
}

static std::string ExtToUtf8(const TCollection_ExtendedString& es) {
    const uint16_t* ws = (const uint16_t*)es.ToExtString();
    if (!ws) return "";
    std::string out;
    for (size_t i = 0; ws[i] != 0; i++) {
        uint32_t cp = ws[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && ws[i+1] != 0) {
            uint32_t lo = ws[i+1];
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                i++;
            }
        }
        if (cp < 0x80) {
            out.push_back((char)cp);
        } else if (cp < 0x800) {
            out.push_back((char)(0xC0 | (cp >> 6)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (cp >> 18)));
            out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}
static std::string LabelKey(const TDF_Label& label) {
    std::ostringstream oss;
    label.EntryDump(oss);
    return oss.str();
}

// Диапазоны глифов Font Awesome (Private Use Area)
static const ImWchar kFontAwesomeRanges[] = { 0xf000, 0xf8ff, 0 };

// Тёмная тема
static void SetupDarkTheme() {
    ImGuiStyle& s = ImGui::GetStyle();

    // === Скругления — больше, чем по умолчанию ===
    s.WindowRounding    = 8.0f;
    s.ChildRounding     = 6.0f;
    s.FrameRounding     = 6.0f;
    s.PopupRounding     = 8.0f;
    s.ScrollbarRounding = 8.0f;
    s.GrabRounding      = 6.0f;
    s.TabRounding       = 6.0f;

    // === Отступы — просторнее ===
    s.WindowPadding     = ImVec2(12, 12);
    s.FramePadding      = ImVec2(10, 6);
    s.CellPadding       = ImVec2(8, 4);
    s.ItemSpacing       = ImVec2(8, 6);
    s.ItemInnerSpacing  = ImVec2(6, 6);
    s.IndentSpacing     = 18.0f;

    // === Рамки — минимализм ===
    s.WindowBorderSize  = 0.0f;
    s.ChildBorderSize   = 0.0f;
    s.FrameBorderSize   = 0.0f;
    s.PopupBorderSize   = 1.0f;

    // === Прозрачность — лёгкая, современно ===
    s.Alpha             = 1.0f;
    s.WindowRounding    = 8.0f;

    // ============================================================
    // ЦВЕТА — современная тёмная палитра
    // ============================================================
    ImVec4* c = s.Colors;

    // Фон — глубокий тёмно-серый с лёгким синим оттенком
    c[ImGuiCol_WindowBg]             = ImVec4(0.11f, 0.12f, 0.14f, 1.00f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.11f, 0.12f, 0.14f, 0.0f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.13f, 0.14f, 0.17f, 0.98f);
    c[ImGuiCol_MenuBarBg]            = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);

    // Текст — мягкий белый
    c[ImGuiCol_Text]                 = ImVec4(0.92f, 0.93f, 0.95f, 1.00f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.52f, 0.56f, 1.00f);

    // Рамки — почти невидимые, только разделители
    c[ImGuiCol_Border]               = ImVec4(0.20f, 0.22f, 0.26f, 1.00f);
    c[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Кнопки — прозрачные до наведения
    c[ImGuiCol_Button]               = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(1.00f, 1.00f, 1.00f, 0.12f);
    c[ImGuiCol_ButtonActive]         = ImVec4(1.00f, 1.00f, 1.00f, 0.18f);

    // Заголовки (для дерева, коллапсирующихся заголовков)
    c[ImGuiCol_Header]               = ImVec4(1.00f, 1.00f, 1.00f, 0.08f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(1.00f, 1.00f, 1.00f, 0.14f);
    c[ImGuiCol_HeaderActive]         = ImVec4(1.00f, 1.00f, 1.00f, 0.20f);

    // Акцент — синий, как в Fusion 360
    ImVec4 accent        = ImVec4(0.20f, 0.55f, 0.95f, 1.00f);   // #338CF2
    ImVec4 accentHover   = ImVec4(0.30f, 0.65f, 1.00f, 1.00f);
    ImVec4 accentActive  = ImVec4(0.15f, 0.45f, 0.85f, 1.00f);

    c[ImGuiCol_Tab]                  = ImVec4(0.15f, 0.16f, 0.20f, 1.00f);
    c[ImGuiCol_TabHovered]           = accentHover;
    c[ImGuiCol_TabSelected]          = accent;
    c[ImGuiCol_TabSelectedOverline]  = accent;
    c[ImGuiCol_TabDimmed]            = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
    c[ImGuiCol_TabDimmedSelected]    = ImVec4(0.30f, 0.40f, 0.55f, 1.00f);

    // Флажок, ползунки — с акцентом
    c[ImGuiCol_CheckMark]            = accent;
    c[ImGuiCol_SliderGrab]           = accent;
    c[ImGuiCol_SliderGrabActive]     = accentActive;

    // Заголовок окна
    c[ImGuiCol_TitleBg]              = ImVec4(0.11f, 0.12f, 0.14f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.11f, 0.12f, 0.14f, 0.75f);

    // Скроллбар — тонкий, тёмный
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.11f, 0.12f, 0.14f, 0.0f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.30f, 0.32f, 0.38f, 0.6f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.42f, 0.48f, 0.8f);
    c[ImGuiCol_ScrollbarGrabActive]  = accent;

    // Разделители
    c[ImGuiCol_Separator]            = ImVec4(0.20f, 0.22f, 0.26f, 1.00f);
    c[ImGuiCol_SeparatorHovered]     = accentHover;
    c[ImGuiCol_SeparatorActive]      = accent;

    // Изменение размеров окна
    c[ImGuiCol_ResizeGrip]           = ImVec4(1.00f, 1.00f, 1.00f, 0.04f);
    c[ImGuiCol_ResizeGripHovered]    = accentHover;
    c[ImGuiCol_ResizeGripActive]     = accent;

    // Плавающие окна
    c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.05f, 0.06f, 0.08f, 0.65f);

    // Таблицы
    c[ImGuiCol_TableHeaderBg]        = ImVec4(0.15f, 0.16f, 0.20f, 1.00f);
    c[ImGuiCol_TableBorderStrong]    = ImVec4(0.20f, 0.22f, 0.26f, 1.00f);
    c[ImGuiCol_TableBorderLight]     = ImVec4(0.16f, 0.17f, 0.20f, 1.00f);
    c[ImGuiCol_TableRowBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_TableRowBgAlt]        = ImVec4(1.00f, 1.00f, 1.00f, 0.02f);

    // Текст в выделении
    c[ImGuiCol_TextSelectedBg]       = accent;
    c[ImGuiCol_NavHighlight]         = accent;

    // Plot
    c[ImGuiCol_PlotLines]            = accent;
    c[ImGuiCol_PlotLinesHovered]     = accentHover;
    c[ImGuiCol_PlotHistogram]        = accent;
    c[ImGuiCol_PlotHistogramHovered] = accentHover;
}

// Светлая тема — фон с оттенком RAL 5024, кнопки светло-серые
// Светлая тема — белый фон + серые кнопки, максимальный контраст
static void SetupLightTheme() {
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding    = 6.0f;
    s.ChildRounding     = 5.0f;
    s.FrameRounding     = 5.0f;
    s.PopupRounding     = 6.0f;
    s.ScrollbarRounding = 6.0f;
    s.GrabRounding      = 5.0f;
    s.TabRounding       = 5.0f;

    s.WindowPadding     = ImVec2(12, 12);
    s.FramePadding      = ImVec2(10, 6);
    s.CellPadding       = ImVec2(8, 4);
    s.ItemSpacing       = ImVec2(8, 6);
    s.ItemInnerSpacing  = ImVec2(6, 6);
    s.IndentSpacing     = 18.0f;

    s.WindowBorderSize  = 0.0f;
    s.ChildBorderSize   = 0.0f;
    s.FrameBorderSize   = 1.0f;
    s.PopupBorderSize   = 1.0f;

    // ==== Нейтральная серая палитра (Inventor-стиль) ====
    ImVec4 BG_WINDOW(0.941f, 0.941f, 0.941f, 1.00f);   // #F0F0F0 — нейтральный
    ImVec4 BG_PANEL (0.910f, 0.910f, 0.910f, 1.00f);   // #E8E8E8 — панели
    ImVec4 BTN      (0.878f, 0.878f, 0.878f, 1.00f);   // #E0E0E0 — кнопки
    ImVec4 BTN_HOV  (0.831f, 0.831f, 0.831f, 1.00f);   // #D4D4D4
    ImVec4 BTN_ACT  (0.784f, 0.784f, 0.784f, 1.00f);   // #C8C8C8
    ImVec4 BORDER   (0.753f, 0.753f, 0.753f, 1.00f);   // #C0C0C0
    ImVec4 TXT      (0.200f, 0.200f, 0.200f, 1.00f);   // #333333 — тёмно-серый
    ImVec4 TXT_DIS  (0.502f, 0.502f, 0.502f, 1.00f);   // #808080 — приглушённый

    ImVec4 accent      (0.969f, 0.635f, 0.000f, 1.00f);   // #F7A200 — Autodesk оранжевый
    ImVec4 accentHover (1.000f, 0.706f, 0.157f, 1.00f);   // #FFB428
    ImVec4 accentActive(0.816f, 0.510f, 0.000f, 1.00f);   // #D08200

    ImVec4* c = s.Colors;

    c[ImGuiCol_WindowBg]             = BG_WINDOW;
    c[ImGuiCol_ChildBg]              = ImVec4(BG_WINDOW.x, BG_WINDOW.y, BG_WINDOW.z, 0.0f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.988f, 0.992f, 0.996f, 0.98f);
    c[ImGuiCol_MenuBarBg]            = BG_PANEL;

    c[ImGuiCol_Text]                 = TXT;
    c[ImGuiCol_TextDisabled]         = TXT_DIS;

    c[ImGuiCol_Border]               = BORDER;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_Button]               = BTN;
    c[ImGuiCol_ButtonHovered]        = BTN_HOV;
    c[ImGuiCol_ButtonActive]         = BTN_ACT;

    c[ImGuiCol_Header]               = BTN;
    c[ImGuiCol_HeaderHovered]        = BTN_HOV;
    c[ImGuiCol_HeaderActive]         = BTN_ACT;

    c[ImGuiCol_Tab]                  = BTN;
    c[ImGuiCol_TabHovered]           = BTN_HOV;
    c[ImGuiCol_TabSelected]          = accent;
    c[ImGuiCol_TabSelectedOverline]  = accent;
    c[ImGuiCol_TabDimmed]            = BG_PANEL;
    c[ImGuiCol_TabDimmedSelected]    = BTN_ACT;

    c[ImGuiCol_CheckMark]            = accent;
    c[ImGuiCol_SliderGrab]           = accent;
    c[ImGuiCol_SliderGrabActive]     = accentActive;

    c[ImGuiCol_TitleBg]              = BG_PANEL;
    c[ImGuiCol_TitleBgActive]        = BG_PANEL;
    c[ImGuiCol_TitleBgCollapsed]     = BG_PANEL;

    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.94f, 0.95f, 0.96f, 0.0f);
    c[ImGuiCol_ScrollbarGrab]        = BTN_HOV;
    c[ImGuiCol_ScrollbarGrabHovered] = BTN_ACT;
    c[ImGuiCol_ScrollbarGrabActive]  = accent;

    c[ImGuiCol_Separator]            = BORDER;
    c[ImGuiCol_SeparatorHovered]     = accentHover;
    c[ImGuiCol_SeparatorActive]      = accent;

    c[ImGuiCol_ResizeGrip]           = ImVec4(0, 0, 0, 0.04f);
    c[ImGuiCol_ResizeGripHovered]    = accentHover;
    c[ImGuiCol_ResizeGripActive]     = accent;
    c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.15f, 0.17f, 0.20f, 0.40f);

    c[ImGuiCol_TableHeaderBg]        = BG_PANEL;
    c[ImGuiCol_TableBorderStrong]    = BORDER;
    c[ImGuiCol_TableBorderLight]     = BTN;
    c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]        = ImVec4(0, 0, 0, 0.03f);

    c[ImGuiCol_TextSelectedBg]       = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    c[ImGuiCol_NavHighlight]         = accent;
    c[ImGuiCol_PlotLines]            = accent;
    c[ImGuiCol_PlotLinesHovered]     = accentHover;
    c[ImGuiCol_PlotHistogram]        = accent;
    c[ImGuiCol_PlotHistogramHovered] = accentHover;
}

// DeepSColor — фирменная тема с бирюзовым акцентом
static void SetupDeepSTheme() {
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding    = 8.0f;
    s.ChildRounding     = 6.0f;
    s.FrameRounding     = 6.0f;
    s.PopupRounding     = 8.0f;
    s.ScrollbarRounding = 8.0f;
    s.GrabRounding      = 6.0f;
    s.TabRounding       = 6.0f;

    s.WindowPadding     = ImVec2(12, 12);
    s.FramePadding      = ImVec2(10, 6);
    s.CellPadding       = ImVec2(8, 4);
    s.ItemSpacing       = ImVec2(8, 6);
    s.ItemInnerSpacing  = ImVec2(6, 6);
    s.IndentSpacing     = 18.0f;

    s.WindowBorderSize  = 0.0f;
    s.ChildBorderSize   = 0.0f;
    s.FrameBorderSize   = 1.0f;
    s.PopupBorderSize   = 1.0f;

    // ==== DeepSColor — глубокий океан с бирюзой ====
    ImVec4 BG_WINDOW(0.059f, 0.078f, 0.098f, 1.00f);   // #0F1419
    ImVec4 BG_PANEL (0.086f, 0.110f, 0.141f, 1.00f);   // #161C24
    ImVec4 BTN      (0.137f, 0.169f, 0.212f, 1.00f);   // #232B36
    ImVec4 BTN_HOV  (0.176f, 0.212f, 0.267f, 1.00f);   // #2D3644
    ImVec4 BTN_ACT  (0.227f, 0.275f, 0.329f, 1.00f);   // #3A4654
    ImVec4 BORDER   (0.165f, 0.204f, 0.255f, 1.00f);   // #2A3441
    ImVec4 TXT      (0.914f, 0.929f, 0.949f, 1.00f);   // #E9EDF2
    ImVec4 TXT_DIS  (0.478f, 0.518f, 0.580f, 1.00f);   // #7A8494

    ImVec4 accent      (0.000f, 0.851f, 0.753f, 1.00f);   // #00D9C0
    ImVec4 accentHover (0.114f, 0.914f, 0.816f, 1.00f);   // #1DE9D0
    ImVec4 accentActive(0.000f, 0.690f, 0.610f, 1.00f);   // #00B09B

    ImVec4* c = s.Colors;

    c[ImGuiCol_WindowBg]             = BG_WINDOW;
    c[ImGuiCol_ChildBg]              = ImVec4(BG_WINDOW.x, BG_WINDOW.y, BG_WINDOW.z, 0.0f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.110f, 0.137f, 0.176f, 0.98f);
    c[ImGuiCol_MenuBarBg]            = BG_PANEL;

    c[ImGuiCol_Text]                 = TXT;
    c[ImGuiCol_TextDisabled]         = TXT_DIS;

    c[ImGuiCol_Border]               = BORDER;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_Button]               = BTN;
    c[ImGuiCol_ButtonHovered]        = BTN_HOV;
    c[ImGuiCol_ButtonActive]         = BTN_ACT;

    c[ImGuiCol_Header]               = BTN;
    c[ImGuiCol_HeaderHovered]        = BTN_HOV;
    c[ImGuiCol_HeaderActive]         = BTN_ACT;

    c[ImGuiCol_Tab]                  = BTN;
    c[ImGuiCol_TabHovered]           = BTN_HOV;
    c[ImGuiCol_TabSelected]          = accent;
    c[ImGuiCol_TabSelectedOverline]  = accent;
    c[ImGuiCol_TabDimmed]            = BG_PANEL;
    c[ImGuiCol_TabDimmedSelected]    = BTN_ACT;

    c[ImGuiCol_CheckMark]            = accent;
    c[ImGuiCol_SliderGrab]           = accent;
    c[ImGuiCol_SliderGrabActive]     = accentActive;

    c[ImGuiCol_TitleBg]              = BG_PANEL;
    c[ImGuiCol_TitleBgActive]        = BG_PANEL;
    c[ImGuiCol_TitleBgCollapsed]     = BG_PANEL;

    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.08f, 0.10f, 0.13f, 0.0f);
    c[ImGuiCol_ScrollbarGrab]        = BTN_HOV;
    c[ImGuiCol_ScrollbarGrabHovered] = BTN_ACT;
    c[ImGuiCol_ScrollbarGrabActive]  = accent;

    c[ImGuiCol_Separator]            = BORDER;
    c[ImGuiCol_SeparatorHovered]     = accentHover;
    c[ImGuiCol_SeparatorActive]      = accent;

    c[ImGuiCol_ResizeGrip]           = ImVec4(1, 1, 1, 0.04f);
    c[ImGuiCol_ResizeGripHovered]    = accentHover;
    c[ImGuiCol_ResizeGripActive]     = accent;
    c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.05f, 0.06f, 0.08f, 0.60f);

    c[ImGuiCol_TableHeaderBg]        = BTN;
    c[ImGuiCol_TableBorderStrong]    = BORDER;
    c[ImGuiCol_TableBorderLight]     = BTN;
    c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]        = ImVec4(1, 1, 1, 0.02f);

    c[ImGuiCol_TextSelectedBg]       = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    c[ImGuiCol_NavHighlight]         = accent;
    c[ImGuiCol_PlotLines]            = accent;
    c[ImGuiCol_PlotLinesHovered]     = accentHover;
    c[ImGuiCol_PlotHistogram]        = accent;
    c[ImGuiCol_PlotHistogramHovered] = accentHover;
}

void Application::ApplyTheme(int theme) {
    myCurrentTheme = theme;

    if (theme == 0) {
        ImGui::StyleColorsLight();
        SetupLightTheme();

        if (myOcctView) {
            // Холодный серо-голубой градиент (Inventor-стиль)
            Quantity_Color top(0.66, 0.72, 0.78, Quantity_TOC_RGB);      // #A8B8C7
            Quantity_Color bottom(0.85, 0.88, 0.91, Quantity_TOC_RGB);   // #D9E0E8
            myOcctView->SetBackgroundGradient(top, bottom);
        }
    } else if (theme == 3) {
        ImGui::StyleColorsDark();
        SetupDeepSTheme();

        if (myOcctView) {
            Quantity_Color top(0.040, 0.075, 0.130, Quantity_TOC_RGB);
            Quantity_Color bottom(0.010, 0.025, 0.050, Quantity_TOC_RGB);
            myOcctView->SetBackgroundGradient(top, bottom);
        }
    } else {
        ImGui::StyleColorsDark();
        SetupDarkTheme();

        if (myOcctView) {
            Quantity_Color top(0.15, 0.17, 0.22, Quantity_TOC_RGB);
            Quantity_Color bottom(0.06, 0.07, 0.10, Quantity_TOC_RGB);
            myOcctView->SetBackgroundGradient(top, bottom);
        }
    }
}

static void LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();

    // 1) Основной шрифт — Inter (современный, как в Figma/Linear)
    const std::vector<std::string> mainCandidates = {
        "assets/fonts/Inter-Regular.ttf",
        "../assets/fonts/Inter-Regular.ttf",
        "../../assets/fonts/Inter-Regular.ttf",
        "assets/fonts/Roboto-Regular.ttf",
        "../assets/fonts/Roboto-Regular.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };

    bool mainLoaded = false;
    for (const auto& path : mainCandidates) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) continue;
        ImFont* font = io.Fonts->AddFontFromFileTTF(
            path.c_str(), 18.0f, nullptr,
            io.Fonts->GetGlyphRangesCyrillic());
        if (font) {
            std::cout << "BreezeCAD: font '" << path << "'\n";
            mainLoaded = true;
            break;
        }
    }
    if (!mainLoaded) io.Fonts->AddFontDefault();

    // 2) Font Awesome — в режиме MergeMode поверх основного
    const std::vector<std::string> faCandidates = {
        "assets/fonts/fa-solid-900.ttf",
        "../assets/fonts/fa-solid-900.ttf",
        "../../assets/fonts/fa-solid-900.ttf",
    };

    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.PixelSnapH = true;

    for (const auto& path : faCandidates) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) continue;
        ImFont* fa = io.Fonts->AddFontFromFileTTF(
            path.c_str(), 18.0f, &cfg, kFontAwesomeRanges);
        if (fa) {
            std::cout << "BreezeCAD: Font Awesome loaded from '" << path << "'\n";
            break;
        }
    }

    // Явно собираем атлас — так поймаем проблемы здесь, а не в первом кадре
    io.Fonts->Build();
    std::cout << "BreezeCAD: font atlas built: " << (io.Fonts->IsBuilt() ? "yes" : "no") << "\n";
}

Application::Application() {
    myWindow = std::make_unique<GlfwWindow>(1280, 720, "BreezeCAD - Prototype");
    if (!myWindow->GetNativeWindow()) return;

    // Загружаем и ПРЕДЗАГРУЖАЕМ все SVG-иконки Lucide
    // (делается до создания окна, чтобы первый кадр не тормозил)
    myIcons.LoadAll("assets/icons");
    myIcons.PreloadDefaults();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // Отключаем сохранение позиций окон в imgui.ini —
    // это устраняет баг «белого экрана» при уехавших окнах
    ImGui::GetIO().IniFilename = nullptr;

    ImGui::StyleColorsDark();
    ApplyTheme(myCurrentTheme);
    LoadFonts();
    ImGui_ImplGlfw_InitForOpenGL(myWindow->GetNativeWindow(), true);
    ImGui_ImplOpenGL3_Init("#version 330");

    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(myWindow->GetNativeWindow(), &fbW, &fbH);

    myOcctView = std::make_unique<OcctView>();
    try {
        if (!myOcctView->Init(myWindow->GetNativeWindow(), fbW, fbH)) {
            myOcctView.reset(); return;
        }
    } catch (...) { myOcctView.reset(); return; }

    ApplyTheme(myCurrentTheme);

    CreateNewDoc();
    InitCommands();

    RefreshView();
    if (myOcctView) myOcctView->FitAll();

    myWindow->SetMouseButtonCallback([this](int b, int a, int m) {
        if (ImGui::GetIO().WantCaptureMouse) return;
        if (myOcctView) myOcctView->OnMouseButton(b, a, m);
    });
    myWindow->SetCursorPosCallback([this](double x, double y) {
        if (ImGui::GetIO().WantCaptureMouse) return;
        if (myOcctView) myOcctView->OnMouseMove(x, y);
    });
    myWindow->SetScrollCallback([this](double x, double y) {
        if (ImGui::GetIO().WantCaptureMouse) return;
        if (myOcctView) myOcctView->OnMouseScroll(x, y);
    });
    myWindow->SetResizeCallback([this](int w, int h) {
        if (myOcctView) myOcctView->Resize(w, h);
    });
}

Application::~Application() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Application::RefreshView() {
    if (!myOcctView || !myDocument) return;
    myOcctView->ClearAllShapes();

    bool hasOps = myDocument->Has3DOperations();

    // Рекурсивный обход всего дерева
    std::function<void(const TDF_Label&)> walk = [&](const TDF_Label& l) {
        int type = BreezeDocument::GetType(l);

        TopoDS_Shape s = myDocument->GetShape(l);
        if (!s.IsNull()) {
            myOcctView->DisplayShape(s, l, BreezeDocument::IsSystem(l));

            // Определяем видимость
            std::string key = LabelKey(l);
            auto it = myVisibility.find(key);

            bool visible;
            if (it != myVisibility.end()) {
                // Пользователь явно указал
                visible = it->second;
            } else {
                // По умолчанию: оси и плоскости скрыты, если есть 3D-операции
                bool isSystemHideable = (type == FT_Axis || type == FT_Plane);
                visible = !(hasOps && isSystemHideable);
            }

            if (!visible) {
                myOcctView->SetShapeVisible(l, false);
            }
        }

        TDF_ChildIterator it(l, false);
        for (; it.More(); it.Next()) walk(it.Value());
    };
    walk(myDocument->MainLabel());

    myOcctView->Redraw();
}

void Application::CaptureCurrentThumbnail() {
    if (!myOcctView || myActiveDocIndex < 0) return;

    // Папка для превью
    std::string dir = "D:/test_breezecad/thumbs/";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    char filename[64];
    std::snprintf(filename, sizeof(filename), "thumb_%d.bmp", myActiveDocIndex);
    std::string path = dir + filename;

    if (myOcctView->CaptureToPixmap(320, 240, path)) {
        // Расширяем вектор до нужного размера
        if ((int)myThumbnailPaths.size() <= myActiveDocIndex) {
            myThumbnailPaths.resize(myActiveDocIndex + 1);
        }
        myThumbnailPaths[myActiveDocIndex] = path;
        std::cout << "Thumbnail saved: " << path << "\n";
    }
}

std::string Application::GetThumbnailPath(int docIndex) const {
    if (docIndex < 0 || docIndex >= (int)myThumbnailPaths.size()) return "";
    return myThumbnailPaths[docIndex];
}

void Application::AddRecentFile(const std::string& path) {
    if (path.empty()) return;

    // Убираем дубликат, если был
    myRecentFiles.erase(
        std::remove(myRecentFiles.begin(), myRecentFiles.end(), path),
        myRecentFiles.end());

    // Вставляем в начало
    myRecentFiles.insert(myRecentFiles.begin(), path);

    // Ограничиваем 10 записями
    if (myRecentFiles.size() > 10) myRecentFiles.resize(10);
}

void Application::OnNewDocument() {
    CreateNewDoc();
    RefreshView();
    if (myOcctView) myOcctView->FitAll();
}

// ============================================================
//  Multi-Document Interface
// ============================================================

void Application::CreateNewDoc() {
    auto doc = std::make_unique<BreezeDocument>();
    doc->CreateBaseGeometry();

    std::string name = "Project" + std::to_string(myNextProjectNumber++);

    myDocuments.push_back(std::move(doc));
    myProjectNames.push_back(name);
    myProjectPaths.push_back("");
    myActiveDocIndex = (int)myDocuments.size() - 1;
    myDocument = myDocuments.back().get();

    std::cout << "CreateNewDoc: '" << name << "' (index " << myActiveDocIndex << ")";
}

int Application::AddDocFromFile(const std::string& path,
                                 const Handle(TDocStd_Document)& loaded) {
    auto doc = std::make_unique<BreezeDocument>();
    doc->SetDocument(loaded);

    // Извлекаем имя файла из пути
    std::string name = path;
    size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);

    myDocuments.push_back(std::move(doc));
    myProjectNames.push_back(name);
    myProjectPaths.push_back(path);
    myActiveDocIndex = (int)myDocuments.size() - 1;
    myDocument = myDocuments.back().get();

    std::cout << "AddDocFromFile: '" << name << "' from " << path << "\n";
    return myActiveDocIndex;
}

void Application::SwitchToDoc(int index) {
    if (index < 0 || index >= (int)myDocuments.size()) return;
    if (index == myActiveDocIndex) return;

    // Сохраняем превью текущего документа перед уходом
    CaptureCurrentThumbnail();

    myActiveDocIndex = index;
    myDocument = myDocuments[index].get();

    // Сброс состояния эскиза — эскиз принадлежал другому документу
    myActiveSketchLabel = TDF_Label();
    myHasSketchStart = false;
    if (myOcctView) {
        myOcctView->ClearPreviewLine();
        myOcctView->ClearAllShapes();
        myOcctView->ClearGrid();
        myOcctView->ExitSketchMode();
    }

    myVisibility.clear();
    RefreshView();
    if (myOcctView) myOcctView->FitAll();

    std::cout << "SwitchToDoc: index " << index << "\n";
}

void Application::CloseDoc(int index) {
    if (index < 0 || index >= (int)myDocuments.size()) return;
    if (myDocuments.size() <= 1) return;  // не закрываем последний

    // Проверка на несохранённые изменения
    BreezeDocument* checkDoc = myDocuments[index].get();
    if (checkDoc && checkDoc->IsModified()) {
        myPendingCloseDocIndex = index;
        myPendingWindowClose = false;
        std::snprintf(myUnsavedDialogInfo, sizeof(myUnsavedDialogInfo),
            "Проект «%s» содержит "
            "несохранённые изменения.\n"
            "Сохранить перед закрытием?",
            myProjectNames[index].c_str());
        myShowUnsavedDialog = true;
        return;
    }

    // Освободить OCAF-документ
    BreezeApplication::Instance().CloseDocument(myDocuments[index]->GetDoc());

    myDocuments.erase(myDocuments.begin() + index);
    myProjectNames.erase(myProjectNames.begin() + index);
    myProjectPaths.erase(myProjectPaths.begin() + index);

    if (myActiveDocIndex >= (int)myDocuments.size()) {
        myActiveDocIndex = (int)myDocuments.size() - 1;
    }
    myDocument = (myActiveDocIndex >= 0) ? myDocuments[myActiveDocIndex].get() : nullptr;

    myActiveSketchLabel = TDF_Label();
    myHasSketchStart = false;
    if (myOcctView) {
        myOcctView->ClearPreviewLine();
        myOcctView->ClearAllShapes();
        myOcctView->ClearGrid();
        myOcctView->ExitSketchMode();
    }
    myVisibility.clear();
    RefreshView();
    if (myOcctView) myOcctView->FitAll();

    std::cout << "CloseDoc: index " << index << "\n";
}

void Application::OnSave() {
    if (!myDocument) return;
    std::string path = myCurrentPath;
    if (path.empty()) {
        if (!ShowFileDialog(true, path, "Сохранить как")) return;
    }
    if (BreezeApplication::Instance().SaveDocument(myDocument->GetDoc(), path)) {
        myCurrentPath = path;
        std::strncpy(myFilePathBuffer, path.c_str(), sizeof(myFilePathBuffer) - 1);
        myFilePathBuffer[sizeof(myFilePathBuffer) - 1] = '\0';
        AddRecentFile(path);
        myDocument->SetModified(false);
        std::cout << "Saved to: " << myCurrentPath << "\n";
    }
}

void Application::OnExportSTEP() {
    if (!myDocument || myDocument->GetDoc().IsNull()) return;

    std::string path;
    if (!ShowFileDialog(true, path, "Экспорт в STEP",
                         "STEP (*.step)\0*.step\0STEP (*.stp)\0*.stp\0All (*.*)\0*.*\0\0",
                         "step")) return;

    try {
        // Собираем все TopoDS_Shape из документа
        std::vector<TopoDS_Shape> shapes = myDocument->GetAllShapes();
        if (shapes.empty()) {
            std::cerr << "Export STEP: no shapes\n";
            return;
        }

        STEPControl_Writer writer;
        Interface_Static::SetCVal("write.step.unit", "MM");
        Interface_Static::SetCVal("write.step.schema", "AP214");

        for (const auto& s : shapes) {
            if (writer.Transfer(s, STEPControl_AsIs) != IFSelect_RetDone) {
                std::cerr << "Export STEP: transfer failed\n";
                return;
            }
        }

        if (writer.Write(path.c_str()) != IFSelect_RetDone) {
            std::cerr << "Export STEP: write failed\n";
            return;
        }
        std::cout << "Exported to STEP: " << path << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Export STEP exception: " << e.what() << "\n";
    }
}

void Application::OnExportSTL() {
    if (!myDocument || myDocument->GetDoc().IsNull()) return;

    std::string path;
    if (!ShowFileDialog(true, path, "Экспорт в STL",
                         "STL (*.stl)\0*.stl\0All (*.*)\0*.*\0\0",
                         "stl")) return;

    try {
        std::vector<TopoDS_Shape> shapes = myDocument->GetAllShapes();
        if (shapes.empty()) {
            std::cerr << "Export STL: no shapes\n";
            return;
        }

        // Объединяем всё в один compound и пишем
        BRep_Builder bb;
        TopoDS_Compound comp;
        bb.MakeCompound(comp);
        for (const auto& s : shapes) bb.Add(comp, s);

        // Тесселяция
        BRepMesh_IncrementalMesh mesh(comp, 0.1);

        StlAPI_Writer writer;
        writer.ASCIIMode() = Standard_False;
        if (!writer.Write(comp, path.c_str())) {
            std::cerr << "Export STL: write failed\n";
            return;
        }
        std::cout << "Exported to STL: " << path << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Export STL exception: " << e.what() << "\n";
    }
}

void Application::OnOpen() {
    std::string path;
    if (!ShowFileDialog(false, path, "Открыть файл")) return;

    Handle(TDocStd_Document) loaded =
        BreezeApplication::Instance().OpenDocument(path);
    if (loaded.IsNull()) { std::cerr << "Open failed\n"; return; }

    AddDocFromFile(path, loaded);
    AddRecentFile(path);
    myVisibility.clear();
    RefreshView();
    if (myOcctView) myOcctView->FitAll();
}

// === Дерево ===

bool Application::DrawLabelTree(const TDF_Label& label,
                                 const TDF_Label& selectedLabel,
                                 bool hasOps) {
    // Линии эскиза не показываем в дереве
    int _earlyType = BreezeDocument::GetType(label);
    if (_earlyType == FT_SketchLine) return false;

    std::string labelName = "<unnamed>";
    Handle(TDataStd_Name) nameAttr;
    if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
        labelName = ExtToUtf8(nameAttr->Get());
    }

    int type = BreezeDocument::GetType(label);
    bool isSystem = (type < 0);

    TDF_ChildIterator childIt(label, false);
    bool hasChildren = childIt.More();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                ImGuiTreeNodeFlags_SpanAvailWidth |
                                ImGuiTreeNodeFlags_DefaultOpen;
    if (!hasChildren)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (!selectedLabel.IsNull() && selectedLabel.IsEqual(label))
        flags |= ImGuiTreeNodeFlags_Selected;

    ImGui::PushID(LabelKey(label).c_str());

    // Чекбокс видимости для системных элементов с геометрией
    if (isSystem && (type == FT_Plane || type == FT_Axis)) {
        std::string key = LabelKey(label);
        auto it = myVisibility.find(key);
        bool visible;
        if (it != myVisibility.end()) {
            visible = it->second;
        } else {
            // По умолчанию: скрыто, если есть 3D-операции
            visible = !hasOps;
        }

                const char* eyeName = visible ? "eye" : "eye-off";
        std::string eyeId = "eye_" + LabelKey(label);
        if (SvgIconBtn(myIcons, eyeId.c_str(), eyeName, 14, 22, 18,
                        visible ? "Скрыть" : "Показать", true)) {
            myVisibility[key] = !visible;
            if (myOcctView) myOcctView->SetShapeVisible(label, !visible);
        }
        ImGui::SameLine();
    }

    if (isSystem) {
        // Системные элементы — приглушённый цвет темы
        ImVec4 txt = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        txt.w *= 0.65f;  // чуть прозрачнее
        ImGui::PushStyleColor(ImGuiCol_Text, txt);
    }
    bool opened = ImGui::TreeNodeEx(labelName.c_str(), flags);
    if (isSystem) ImGui::PopStyleColor();

    // Двойной клик по эскизу → войти в режим 2D
    if (type == FT_Sketch && ImGui::IsItemHovered()
        && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        myPendingEnterSketchLabel = label;
    }

    // === Контекстное меню (ПКМ) ===
    if (ImGui::BeginPopupContextItem("##ctx")) {
        ImGui::TextDisabled("%s", labelName.c_str());
        ImGui::Separator();
        DrawContextMenuItems(label, hasOps);
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("[type=%d, tag=%d]", type, label.Tag());

    bool requested = false;
    if (opened && hasChildren) {
        TDF_ChildIterator it(label, false);
        for (; it.More(); it.Next()) {
            if (DrawLabelTree(it.Value(), selectedLabel, hasOps)) requested = true;
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
    return requested || !myPendingDeleteLabel.IsNull()
                     || !myPendingCreateSketchLabel.IsNull();
}

void Application::OnUndo() {
    if (!myDocument) return;
    if (myDocument->Undo()) {
        std::cout << "Undo OK\n";
        RefreshView();
        if (!myActiveSketchLabel.IsNull()) {
            // Если в эскизе — перерисовать линии
            if (myOcctView) myOcctView->ClearAllShapes();
            RefreshSketchLines();
            myOcctView->ShowGrid(myShowGrid);
        }
    } else {
        std::cout << "Undo: nothing to undo\n";
    }
}

void Application::OnRedo() {
    if (!myDocument) return;
    if (myDocument->Redo()) {
        std::cout << "Redo OK\n";
        RefreshView();
        if (!myActiveSketchLabel.IsNull()) {
            if (myOcctView) myOcctView->ClearAllShapes();
            RefreshSketchLines();
            myOcctView->ShowGrid(myShowGrid);
        }
    } else {
        std::cout << "Redo: nothing to redo\n";
    }
}

void Application::DrawQuickAccessToolbar() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 5));

    // === 1. Логотип / Меню файл ===
    // Логотип/HOME — возврат на главную
    if (QatIconButton(myIcons, "qat_home", "house", 20, "Главная")) {
        CaptureCurrentThumbnail();
        myShowHomeScreen = true;
    }

    ImGui::SameLine();

    // Меню «Файл»
    if (QatIconButton(myIcons, "qat_file", "file-text", 20, "Файл")) {
        ImGui::OpenPopup("##file_menu");
    }
    DrawAdvancedTooltip("qat.file",
                        "Меню «Файл»",
                        "Создать, открыть, сохранить, экспортировать",
                        "Здесь собраны все операции с файлами проекта: создание нового документа, "
                        "открытие существующего, сохранение, экспорт в STEP/STL/OBJ, а также доступ к "
                        "списку недавних файлов и настройкам программы.");

    if (ImGui::BeginPopup("##file_menu")) {
        if (ImGui::MenuItem("Главная", "Ctrl+H", false, false)) {}
        ImGui::Separator();
        if (ImGui::MenuItem("Создать", "Ctrl+N")) OnNewDocument();
        if (ImGui::MenuItem("Открыть...", "Ctrl+O")) OnOpen();
        if (ImGui::MenuItem("Сохранить", "Ctrl+S")) OnSave();
        if (ImGui::MenuItem("Сохранить как...")) { myCurrentPath.clear(); OnSave(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Экспорт в STEP...")) OnExportSTEP();
        if (ImGui::MenuItem("Экспорт в STL...")) OnExportSTL();
        if (ImGui::MenuItem("Экспорт в OBJ", "", false, false)) {}
        ImGui::Separator();
        if (ImGui::BeginMenu("Последние файлы")) {
            if (myRecentFiles.empty()) {
                ImGui::MenuItem("(пусто)", "", false, false);
            } else {
                for (size_t i = 0; i < myRecentFiles.size(); i++) {
                    std::string label = myRecentFiles[i];
                    // Обрезаем длинный путь
                    if (label.length() > 60) {
                        label = "..." + label.substr(label.length() - 57);
                    }
                    label += "##recent" + std::to_string(i);
                    if (ImGui::MenuItem(label.c_str())) {
                        Handle(TDocStd_Document) loaded =
                            BreezeApplication::Instance().OpenDocument(myRecentFiles[i]);
                        if (!loaded.IsNull()) {
                            AddDocFromFile(myRecentFiles[i], loaded);
                            RefreshView();
                            if (myOcctView) myOcctView->FitAll();
                            myShowHomeScreen = false;
                        }
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Очистить список")) myRecentFiles.clear();
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Настройки")) myShowSettingsDialog = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Закрыть проект")) {
            if (myActiveDocIndex >= 0) CloseDoc(myActiveDocIndex);
        }
        if (ImGui::MenuItem("Выход", "Alt+F4")) {
            glfwSetWindowShouldClose(myWindow->GetNativeWindow(), GLFW_TRUE);
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();

    // === 2. Undo / Redo ===
    if (QatIconButton(myIcons, "qat_undo", "undo-2", 18, "Отменить (Ctrl+Z)")) OnUndo();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Отменить (Ctrl+Z)");
    ImGui::SameLine();
    if (QatIconButton(myIcons, "qat_redo", "redo-2", 18, "Повторить (Ctrl+Y)")) OnRedo();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Повторить (Ctrl+Y)");

    ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();

    // === 3. Save ===
    if (SvgIconBtn(myIcons, "qat_save", "save", 20, 32, 28, "Сохранить")) OnSave();
    DrawAdvancedTooltip("qat.save",
                        "Сохранить (Ctrl+S)",
                        "Сохранить текущий проект в файл",
                        "Записывает все изменения текущего проекта в файл .cbf. Если проект "
                        "ещё не имеет имени файла — откроется диалог выбора пути. "
                        "Горячая клавиша — Ctrl+S.");
    ImGui::SameLine();

    // === 4. Settings ===
    if (QatIconButton(myIcons, "qat_settings", "settings", 20, "Настройки")) myShowSettingsDialog = true;

    ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();

    // === 5. Закреплённые команды ===
    for (size_t i = 0; i < myPinnedCommands.size(); i++) {
        ImGui::SameLine();
        ImGui::PushID((int)i);
        if (ImGui::Button(myPinnedCommands[i].c_str(), ImVec2(32, 0))) {}
        if (ImGui::BeginPopupContextItem("##pin_ctx")) {
            if (ImGui::MenuItem("Открепить от панели")) {
                myPinnedCommands.erase(myPinnedCommands.begin() + i);
                ImGui::EndPopup();
                ImGui::PopID();
                break;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    // === 6. Поиск команд (справа) ===
    ImGuiIO& io = ImGui::GetIO();
    float searchW     = 220.0f;
    float rightPanelW = 260.0f;
    float searchX     = io.DisplaySize.x - searchW - rightPanelW - 20.0f;

    ImGui::SameLine(searchX);
    ImGui::SetNextItemWidth(searchW);
    ImGui::InputTextWithHint("##cmd_search", "Поиск команд...",
                             mySearchBuffer, sizeof(mySearchBuffer));
    ImVec2 searchMin = ImGui::GetItemRectMin();
    ImVec2 searchMax = ImGui::GetItemRectMax();
    bool searchActive = ImGui::IsItemActive();

    // Результаты — плавающее окно (НЕ popup, чтобы не перехватывать клики)
    if (mySearchBuffer[0] != 0 && searchActive) {
        std::string query = mySearchBuffer;
        std::transform(query.begin(), query.end(), query.begin(),
                       [](unsigned char c){ return (char)std::tolower(c); });

        std::vector<CommandEntry*> matches;
        for (auto& cmd : myCommands) {
            std::string lower = cmd.name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c){ return (char)std::tolower(c); });
            if (lower.find(query) != std::string::npos) matches.push_back(&cmd);
        }

        ImGui::SetNextWindowPos(ImVec2(searchMin.x, searchMax.y + 2));
        ImGui::SetNextWindowSize(ImVec2(searchW, 0), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.95f);
        ImGui::Begin("##cmd_results_float", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        if (matches.empty()) {
            ImGui::TextDisabled("Ничего не найдено");
        } else {
            for (auto* cmd : matches) {
                ImGui::PushID(cmd->id.c_str());
                std::string label = cmd->name;
                if (!cmd->shortcut.empty()) label += "     " + cmd->shortcut;
                if (ImGui::Selectable(label.c_str())) {
                    if (cmd->action) cmd->action();
                    mySearchBuffer[0] = 0;
                }
                ImGui::PopID();
            }
        }
        ImGui::End();
    }

    // === 7. Телеметрия ===
    ImGui::SameLine(io.DisplaySize.x - rightPanelW);
    ImGui::Text("FPS: %.0f", io.Framerate);
    ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();
    ImGui::Text("RAM: %.0f MB", myLastRAMMB);
    ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();
    ImGui::Text("Obj: %d", myLastShapeCount);

    ImGui::PopStyleVar();
}

void Application::DrawRibbonTabs() {
    static const char* tabs[] = {
        "tab.sketch", "tab.model", "tab.annotate",
        "tab.tools", "tab.manage", "tab.view"
    };
    for (int i = 0; i < 6; i++) {
        if (i > 0) ImGui::SameLine();
        bool active = (myActiveRibbonTab == i);
        bool isLight = (ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).x > 0.5f);
        if (active) {
            if (isLight) {
                // Светлая — серебристый C3
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.698f, 0.714f, 0.737f, 1.0f));
            } else {
                // Тёмная — синий accent
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.95f, 1.0f));
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Button));
        }
        if (ImGui::Button(Tr(tabs[i]).c_str(), ImVec2(120, 26))) {
            myActiveRibbonTab = i;
        }
        ImGui::PopStyleColor();
    }
    // Кнопка "+" для подключения модулей
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Button));
    if (SvgIconBtn(myIcons, "ribbon_plus", "plus", 16, 32, 26, "Подключить модуль")) {
        // TODO: диалог подключения модуля
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Подключить модуль");
}


// Рисует кнопку Ribbon. active=false → красная рамка, "disabled".
static bool RibbonButton(const char* label, bool active, const char* tooltip = nullptr,
                          float width = 90, float height = 0) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(50, 90, 140, 255));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(120, 30, 30, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(160, 40, 40, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(180, 50, 50, 255));
    }
    bool clicked = active && ImGui::Button(label, ImVec2(width, height));
    if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    if (active) ImGui::PopStyleColor();
    else { ImGui::PopStyleColor(3); }
    return clicked;
}

// Большая кнопка Ribbon: верх — название, низ — полоска с ▼ для меню
// opts — массив указателей на ключи локализации (термины).
// idx — индекс выбранного в данный момент.
// Возвращает: 0 = не нажато, 1 = нажата основная, 2 = нажата стрелка ▼


// ============================================================
//  Вкладка «Эскиз»
// ============================================================


// Универсальная кнопка Ribbon с SVG-иконкой Lucide.
int Application::RibbonButton(const char* id, const char* const* opts,
                              int optCount, int& idx, const char* icon,
                              bool enabled, bool big) {
    int result = 0;
    const float bigW = 100.0f, bigH = 56.0f;
    const float smallW = 42.0f, smallH = 42.0f;
    const float arrowH = 14.0f;
    float w = big ? bigW : smallW;
    float h = big ? bigH : smallH;
    const int iconSize = big ? 26 : 24;

    bool isLight = (ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).x > 0.5f);
    ImVec4 bgNormal, bgHover, bgActive;
    if (isLight) {
        bgNormal = ImVec4(0.843f, 0.855f, 0.871f, 1.0f);
        bgHover  = ImVec4(0.776f, 0.792f, 0.808f, 1.0f);
        bgActive = ImVec4(0.698f, 0.714f, 0.737f, 1.0f);
    } else {
        bgNormal = ImVec4(1.0f, 1.0f, 1.0f, 0.06f);
        bgHover  = ImVec4(1.0f, 1.0f, 1.0f, 0.12f);
        bgActive = ImVec4(1.0f, 1.0f, 1.0f, 0.18f);
    }
    if (!enabled) {
        bgNormal.w *= 0.55f; bgHover.w *= 0.55f; bgActive.w *= 0.55f;
    }

    ImVec4 tint = IconTintColor(enabled);
    GLuint tex = myIcons.Get(icon, iconSize);
    ImTextureID tid = (ImTextureID)(intptr_t)tex;

    ImGui::PushID(id);
    ImGui::BeginGroup();

    // Кликабельная область
    ImVec2 startPos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##click", ImVec2(w, h));
    bool clicked = ImGui::IsItemClicked();
    bool hovered = ImGui::IsItemHovered();
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();

    // Фон кнопки
    ImU32 bgCol = ImGui::GetColorU32(hovered ? bgHover : bgNormal);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(min, max, bgCol, 6.0f);

    // Иконка
    if (tid != 0) {
        ImVec2 iconPos(min.x + (w - iconSize) * 0.5f, min.y + 5.0f);
        dl->AddImage(tid, iconPos,
                     ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
                     ImVec2(0, 0), ImVec2(1, 1),
                     ImGui::GetColorU32(tint));
    } else {
        // Placeholder — серый квадрат с ?
        ImVec2 p1(min.x + (w - iconSize) * 0.5f, min.y + 5.0f);
        dl->AddRectFilled(p1, ImVec2(p1.x + iconSize, p1.y + iconSize),
                          IM_COL32(140, 140, 140, 180), 4.0f);
    }

    // Текст подписи (для больших) — обрезка по ширине кнопки
    if (big && optCount > 0) {
        std::string label = Tr(opts[idx]);
        float maxTextW = w - 4.0f;

        // Обрезка с ".." если не влезает
        if (ImGui::CalcTextSize(label.c_str()).x > maxTextW) {
            while (!label.empty() &&
                   ImGui::CalcTextSize((label + "..").c_str()).x > maxTextW) {
                label.pop_back();
            }
            label += "..";
        }

        ImVec2 txtSize = ImGui::CalcTextSize(label.c_str());
        ImVec2 txtPos(min.x + (w - txtSize.x) * 0.5f, max.y - 15.0f);
        ImU32 txtCol = ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        dl->AddText(txtPos, txtCol, label.c_str());
    }

    if (clicked && enabled) result = 1;

    // Стрелка ▼ внизу
    if (optCount > 1) {
        ImVec2 aMin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##arrow", ImVec2(w, arrowH));
        bool arrowClick = ImGui::IsItemClicked();
        bool arrowHover = ImGui::IsItemHovered();
        ImVec2 aMax = ImGui::GetItemRectMax();

        ImU32 arrowBg = ImGui::GetColorU32(arrowHover ? bgHover : bgNormal);
        dl->AddRectFilled(aMin, aMax, arrowBg, 4.0f);

        GLuint arrowTex = myIcons.Get("chevron-down", 12);
        if (arrowTex != 0) {
            ImVec2 ip(aMin.x + (w - 12) * 0.5f, aMin.y + (arrowH - 12) * 0.5f);
            dl->AddImage((ImTextureID)(intptr_t)arrowTex, ip,
                         ImVec2(ip.x + 12, ip.y + 12),
                         ImVec2(0, 0), ImVec2(1, 1),
                         ImGui::GetColorU32(tint));
        }

        if (arrowClick && enabled) result = 2;
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

int Application::RibbonSingleButton(const char* id, const char* key,
                                    const char* icon, bool enabled, bool big) {
    static int dummy = 0;
    const char* opts[] = { key };
    return RibbonButton(id, opts, 1, dummy, icon, enabled, big);
}

void Application::DrawRibbonSketchTab() {
    bool inSketch = !myActiveSketchLabel.IsNull();

    // --- Группа: Создать ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.create").c_str());

    static const char* lineOpts[]   = { "tool.line", "tool.curve", "tool.spline", "tool.bridge" };
    static const char* rectOpts[]   = { "tool.rect.2pt", "tool.rect.center", "tool.rect.3pt" };
    static const char* circleOpts[] = { "tool.circle.center", "tool.circle.2pt",
                                        "tool.circle.3pt", "tool.circle.tan" };
    static const char* arcOpts[]    = { "tool.arc.center", "tool.arc.3pt", "tool.arc.tan" };
    static const char* polyOpts[]   = { "tool.poly.inscribed", "tool.poly.circum" };
    static int dummy = 0;

    int r;
    r = RibbonButton("line", lineOpts, 4, myLineToolIndex, ICON_LINE, inSketch, true);
    if (r == 1 || r == 3) {
        myCurrentLineStyle = LS_Normal;
        myHasSketchStart = false;
        if (myOcctView) myOcctView->ClearPreviewLine();
    }
    ImGui::SameLine();
    RibbonButton("rect",   rectOpts,   3, myRectToolIndex,   ICON_RECT,    inSketch, true);
    ImGui::SameLine();
    RibbonButton("circle", circleOpts, 4, myCircleToolIndex, ICON_CIRCLE,  inSketch, true);
    ImGui::SameLine();
    RibbonButton("arc",    arcOpts,    3, myArcToolIndex,    ICON_ARC,     inSketch, true);
    ImGui::SameLine();
    RibbonButton("poly",   polyOpts,   2, myPolyToolIndex,   ICON_POLYGON, inSketch, true);
    ImGui::SameLine();
    RibbonButton("point",  lineOpts,   1, dummy,             ICON_POINT,   inSketch, true);
    ImGui::SameLine();
    RibbonButton("text",   lineOpts,   1, dummy,             ICON_TEXT,    inSketch, true);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Группа: Проецировать ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.project").c_str());
    RibbonButton("proj_geom", lineOpts, 1, dummy, ICON_PROJECT,      inSketch, true);
    ImGui::SameLine();
    RibbonButton("proj_loop", lineOpts, 1, dummy, ICON_PROJECT_LOOP, inSketch, true);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Группа: Изменить ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.modify").c_str());

    static const char* trimOpts[]   = { "tool.trim", "tool.extend" };
    static const char* offsetOpts[] = { "tool.offset", "tool.scale" };
    static const char* moveOpts[]   = { "tool.move", "tool.copy", "tool.rotate", "tool.mirror" };

    RibbonButton("trim",   trimOpts,   2, myTrimToolIndex,   ICON_TRIM,    inSketch, false);
    ImGui::SameLine();
    RibbonButton("offset", offsetOpts, 2, myOffsetToolIndex, ICON_OFFSET,  inSketch, false);
    ImGui::SameLine();
    RibbonButton("move",   moveOpts,   4, myMoveToolIndex,   ICON_MOVE,    inSketch, false);
    ImGui::SameLine();
    RibbonButton("rotate", lineOpts,   1, dummy,             ICON_ROTATE,  inSketch, false);
    ImGui::SameLine();
    RibbonButton("mirror", lineOpts,   1, dummy,             ICON_MIRROR,  inSketch, false);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Группа: Ограничения ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.constrain").c_str());

    static const char* conOpts[] = { "tool.coincident", "tool.parallel", "tool.perpendicular",
                                     "tool.horizontal", "tool.vertical", "tool.tangent",
                                     "tool.concentric", "tool.equal" };

    int conResult = RibbonButton("constrain", conOpts, 8, myConstrainToolIndex,
                                  ICON_COINCIDENT, inSketch, false);
    if (conResult == 1 || conResult == 3) OpenDependencyDialog(myConstrainToolIndex);

    ImGui::SameLine();
    RibbonButton("dim", lineOpts, 1, dummy, ICON_DIMENSION, inSketch, false);
    ImGui::SameLine();
    RibbonButton("collinear", lineOpts, 1, dummy, ICON_COLLINEAR,    inSketch, false);
    ImGui::SameLine();
    RibbonButton("concentric", lineOpts, 1, dummy, ICON_CONCENTRIC,  inSketch, false);
    ImGui::SameLine();
    RibbonButton("fix", lineOpts, 1, dummy, ICON_FIX,                inSketch, false);
    ImGui::SameLine();
    RibbonButton("symmetric", lineOpts, 1, dummy, ICON_SYMMETRIC,    inSketch, false);
    ImGui::SameLine();
    RibbonButton("smooth", lineOpts, 1, dummy, ICON_SMOOTH_G2,       inSketch, false);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Группа: Массив ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.pattern").c_str());
    RibbonButton("sk_pattern_rect", lineOpts, 1, dummy, ICON_PATTERN_RECT, inSketch, false);
    ImGui::SameLine();
    RibbonButton("sk_pattern_circ", lineOpts, 1, dummy, ICON_PATTERN_CIRC, inSketch, false);
    ImGui::SameLine();
    RibbonButton("sk_mirror",       lineOpts, 1, dummy, ICON_MIRROR,       inSketch, false);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Группа: Формат (стили линий — столбик 3 кнопки) ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.format").c_str());

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

    // Основная — фиолетовая
    {
        bool active = (myCurrentLineStyle == LS_Normal);
        ImVec4 bg = active ? ImVec4(0.55f, 0.20f, 0.75f, 1.0f)
                           : ImVec4(0.22f, 0.24f, 0.30f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, bg);
        ImGui::PushStyleColor(ImGuiCol_Text,
            active ? ImVec4(1, 1, 1, 1) : ImVec4(0.8f, 0.8f, 0.8f, 1));
        if (ImGui::Button("Основная##ls_norm", ImVec2(110, 22)))
            myCurrentLineStyle = LS_Normal;
        ImGui::PopStyleColor(2);
    }

    // Вспомогательная — серая
    {
        bool active = (myCurrentLineStyle == LS_Construction);
        ImVec4 bg = active ? ImVec4(0.45f, 0.50f, 0.65f, 1.0f)
                           : ImVec4(0.22f, 0.24f, 0.30f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, bg);
        ImGui::PushStyleColor(ImGuiCol_Text,
            active ? ImVec4(1, 1, 1, 1) : ImVec4(0.8f, 0.8f, 0.8f, 1));
        if (ImGui::Button("Вспомогательная##ls_const", ImVec2(110, 22)))
            myCurrentLineStyle = LS_Construction;
        ImGui::PopStyleColor(2);
    }

    // Осевая — коричневая
    {
        bool active = (myCurrentLineStyle == LS_Centerline);
        ImVec4 bg = active ? ImVec4(0.70f, 0.40f, 0.15f, 1.0f)
                           : ImVec4(0.22f, 0.24f, 0.30f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, bg);
        ImGui::PushStyleColor(ImGuiCol_Text,
            active ? ImVec4(1, 1, 1, 1) : ImVec4(0.8f, 0.8f, 0.8f, 1));
        if (ImGui::Button("Осевая##ls_center", ImVec2(110, 22)))
            myCurrentLineStyle = LS_Centerline;
        ImGui::PopStyleColor(2);
    }

    ImGui::PopStyleVar(3);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Группа: Выход ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.exit").c_str());

    if (RibbonButton("finish", lineOpts, 1, dummy, ICON_FINISH, inSketch, true)) {
        ExitSketch();
    }
    ImGui::SameLine();
    if (RibbonButton("clear", lineOpts, 1, dummy, ICON_TRASH, inSketch, false)) {
        if (myDocument && !myActiveSketchLabel.IsNull()) {
            myDocument->ClearSketch(myActiveSketchLabel);
            if (myOcctView) myOcctView->ClearAllShapes();
            RefreshSketchLines();
            if (myOcctView) myOcctView->ShowGrid(myShowGrid);
        }
    }
    ImGui::EndGroup();
}

// ============================================================
//  Вкладка «3D Модель»
// ============================================================
void Application::DrawRibbonModelTab() {
    static int dummy = 0;
    static const char* oneOpt[] = { "tool.dummy" };

    // --- Группа: Эскиз ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.sketch").c_str());
    RibbonSingleButton("newsketch", "tool.newSketch", ICON_SKETCH, true, true);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Группа: Создать ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.create").c_str());
    RibbonSingleButton("extrude", "tool.extrude", ICON_EXTRUDE, true, true);
    ImGui::SameLine();
    RibbonSingleButton("revolve", "tool.revolve", ICON_REVOLVE, true, true);
    ImGui::SameLine();
    RibbonSingleButton("sweep",   "tool.sweep",   ICON_SWEEP,   true, true);
    ImGui::SameLine();
    RibbonSingleButton("loft",    "tool.loft",    ICON_LOFT,    true, true);
    ImGui::SameLine();
    RibbonSingleButton("hole",    "tool.hole",    ICON_HOLE,    true, true);
    ImGui::SameLine();
    RibbonSingleButton("thread",  "tool.thread",  ICON_THREAD,  true, true);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Группа: Изменить ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.modify").c_str());
    RibbonSingleButton("fillet",   "tool.fillet",   ICON_FILLET,  true, true);
    ImGui::SameLine();
    RibbonSingleButton("chamfer",  "tool.chamfer",  ICON_CHAMFER, true, true);
    ImGui::SameLine();
    RibbonSingleButton("shell",    "tool.shell",    ICON_SHELL,   true, true);
    ImGui::SameLine();
    RibbonSingleButton("union",    "tool.union",    ICON_UNION,   true, true);
    ImGui::SameLine();
    RibbonSingleButton("cut",      "tool.cut",      ICON_CUT,     true, true);
    ImGui::SameLine();
    RibbonSingleButton("intersect","tool.intersect",ICON_INTERSECT,true, true);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Группа: Массив ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.pattern").c_str());
    RibbonSingleButton("pattern_rect", "tool.pattern_rect", ICON_PATTERN_RECT, true, false);
    ImGui::SameLine();
    RibbonSingleButton("pattern_circ", "tool.pattern_circ", ICON_PATTERN_CIRC, true, false);
    ImGui::SameLine();
    RibbonSingleButton("mirror3d",     "tool.mirror",       ICON_MIRROR,       true, false);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Группа: Рабочие элементы ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.work").c_str());
    RibbonSingleButton("wplane", "tool.plane",    ICON_PLANE,    true, false);
    ImGui::SameLine();
    RibbonSingleButton("waxis",  "tool.axis",     ICON_AXIS,     true, false);
    ImGui::SameLine();
    RibbonSingleButton("wpoint", "tool.point",    ICON_POINT_3D, true, false);
    ImGui::EndGroup();
}

// ============================================================
//  Вкладка «Аннотация»
// ============================================================
void Application::DrawRibbonAnnotateTab() {
    static int dummy = 0;
    static const char* oneOpt[] = { "tool.dummy" };

    // --- Размеры ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.dimensions").c_str());
    RibbonSingleButton("dim_linear",   "tool.dim_linear",   ICON_DIM_LINEAR,   true, true);
    ImGui::SameLine();
    RibbonSingleButton("dim_angle",    "tool.dim_angle",    ICON_DIM_ANGLE,    true, true);
    ImGui::SameLine();
    RibbonSingleButton("dim_radius",   "tool.dim_radius",   ICON_DIM_RADIUS,   true, true);
    ImGui::SameLine();
    RibbonSingleButton("dim_diameter", "tool.dim_diameter", ICON_DIM_DIAMETER, true, true);
    ImGui::SameLine();
    RibbonSingleButton("dim_ordinate", "tool.dim_ordinate", ICON_DIM_ORDINATE, true, true);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Текст и символы ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.text").c_str());
    RibbonSingleButton("leader",    "tool.leader",     ICON_LEADER,     true, false);
    ImGui::SameLine();
    RibbonSingleButton("text_ann",  "tool.text",       ICON_TEXT,       true, false);
    ImGui::SameLine();
    RibbonSingleButton("datum",     "tool.datum",      ICON_DATUM,      true, false);
    ImGui::SameLine();
    RibbonSingleButton("tolerance", "tool.tolerance",  ICON_TOLERANCE,  true, false);
    ImGui::SameLine();
    RibbonSingleButton("roughness", "tool.roughness",  ICON_ROUGHNESS,  true, false);
    ImGui::SameLine();
    RibbonSingleButton("weld",      "tool.weld",       ICON_WELD,       true, false);
    ImGui::EndGroup();
}

// ============================================================
//  Вкладка «Инструменты»
// ============================================================
void Application::DrawRibbonToolsTab() {
    static int dummy = 0;
    static const char* oneOpt[] = { "tool.dummy" };

    // --- Измерить ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.measure").c_str());
    RibbonSingleButton("measure",  "tool.measure",  ICON_MEASURE,  true, false);
    ImGui::SameLine();
    RibbonSingleButton("distance", "tool.distance", ICON_DISTANCE, true, false);
    ImGui::SameLine();
    RibbonSingleButton("angle",    "tool.angle",    ICON_ANGLE,    true, false);
    ImGui::SameLine();
    RibbonSingleButton("area",     "tool.area",     ICON_AREA,     true, false);
    ImGui::SameLine();
    RibbonSingleButton("volume",   "tool.volume",   ICON_VOLUME,   true, false);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Анализ ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.analysis").c_str());
    RibbonSingleButton("section",       "tool.section",       ICON_SECTION,       true, false);
    ImGui::SameLine();
    RibbonSingleButton("draft_analysis","tool.draft_analysis",ICON_DRAFT_ANALYSIS,true, false);
    ImGui::SameLine();
    RibbonSingleButton("thickness",     "tool.thickness",     ICON_THICKNESS,     true, false);
    ImGui::SameLine();
    RibbonSingleButton("curvature",     "tool.curvature",     ICON_CURVATURE,     true, false);
    ImGui::SameLine();
    RibbonSingleButton("zebra",         "tool.zebra",         ICON_ZEBRA,         true, false);
    ImGui::SameLine();
    RibbonSingleButton("interference",  "tool.interference",  ICON_INTERFERENCE,  true, false);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Свойства ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.properties").c_str());
    RibbonSingleButton("model_props", "tool.model_props", ICON_MODEL_PROPS, true, false);
    ImGui::SameLine();
    RibbonSingleButton("mass",        "tool.mass",        ICON_MASS,        true, false);
    ImGui::EndGroup();
}

// ============================================================
//  Вкладка «Управление»
// ============================================================
void Application::DrawRibbonManageTab() {
    static int dummy = 0;
    static const char* oneOpt[] = { "tool.dummy" };

    // --- Параметры ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.parameters").c_str());
    RibbonSingleButton("parameters", "tool.parameters", ICON_PARAMETERS, true, true);
    ImGui::SameLine();
    RibbonSingleButton("functions",  "tool.functions",  ICON_FUNCTION,   true, true);
    ImGui::SameLine();
    RibbonSingleButton("texts",      "tool.texts",      ICON_TEXT,       true, true);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Материалы (будущее) ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.materials").c_str());
    RibbonSingleButton("material",   "tool.material",   ICON_MATERIAL,   true, false);
    ImGui::SameLine();
    RibbonSingleButton("texture",    "tool.texture",    ICON_TEXTURE,    true, false);
    ImGui::SameLine();
    RibbonSingleButton("appearance", "tool.appearance", ICON_APPEARANCE, true, false);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Обновление ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.update").c_str());
    RibbonSingleButton("update",    "tool.update",    ICON_UPDATE,    true, false);
    ImGui::SameLine();
    RibbonSingleButton("recompute", "tool.recompute", ICON_RECOMPUTE, true, false);
    ImGui::SameLine();
    RibbonSingleButton("report",    "tool.report",    ICON_REPORT,    true, false);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Вставка ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.insert").c_str());
    RibbonSingleButton("insert_obj", "tool.insert_obj", ICON_INSERT_OBJ, true, false);
    ImGui::SameLine();
    RibbonSingleButton("insert_img", "tool.insert_img", ICON_INSERT_IMG, true, false);
    ImGui::SameLine();
    RibbonSingleButton("import",     "tool.import",     ICON_IMPORT,     true, false);
    ImGui::EndGroup();
}

// ============================================================
//  Вкладка «Вид»
// ============================================================
void Application::DrawRibbonViewTab() {
    static int dummy = 0;
    static const char* oneOpt[] = { "tool.dummy" };

    // --- Навигация ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.navigation").c_str());
    RibbonSingleButton("pan",    "tool.pan",    ICON_PAN,     true, true);
    ImGui::SameLine();
    RibbonSingleButton("zoom",   "tool.zoom",   ICON_ZOOM,    true, true);
    ImGui::SameLine();
    RibbonSingleButton("orbit",  "tool.orbit",  ICON_ORBIT,   true, true);
    if (ImGui::IsItemHovered()) {}
    if (RibbonSingleButton("home_v", "tool.home", ICON_HOME, true, true)) {
        if (myOcctView) myOcctView->SetHomeView();
    }
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Стандартные виды ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.views").c_str());
    RibbonSingleButton("v_top",   "tool.view_top",   ICON_VIEW_TOP,    true, false);
    ImGui::SameLine();
    RibbonSingleButton("v_front", "tool.view_front", ICON_VIEW_FRONT,  true, false);
    ImGui::SameLine();
    RibbonSingleButton("v_right", "tool.view_right", ICON_VIEW_RIGHT,  true, false);
    ImGui::SameLine();
    RibbonSingleButton("v_iso",   "tool.view_iso",   ICON_VIEW_ISO,    true, false);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Отображение ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.display").c_str());
    RibbonSingleButton("shadows",     "tool.shadows",     ICON_SHADOWS,     true, false);
    ImGui::SameLine();
    RibbonSingleButton("reflections", "tool.reflections", ICON_REFLECTIONS, true, false);
    ImGui::SameLine();
    RibbonSingleButton("grid",        "tool.grid",        ICON_GRID,        true, false);
    ImGui::SameLine();
    RibbonSingleButton("background",  "tool.background",  ICON_BACKGROUND,  true, false);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Видимость ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.visibility").c_str());
    RibbonSingleButton("hide",        "tool.hide",        ICON_HIDE,        true, false);
    ImGui::SameLine();
    RibbonSingleButton("show_all",    "tool.show_all",    ICON_SHOW_ALL,    true, false);
    ImGui::SameLine();
    RibbonSingleButton("isolate",     "tool.isolate",     ICON_ISOLATE,     true, false);
    ImGui::SameLine();
    RibbonSingleButton("show_hidden", "tool.show_hidden", ICON_SHOW_HIDDEN, true, false);
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0, 10);

    // --- Окна ---
    ImGui::BeginGroup();
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", Tr("grp.windows").c_str());
    RibbonSingleButton("new_window", "tool.new_window", ICON_NEW_WINDOW, true, false);
    ImGui::SameLine();
    RibbonSingleButton("tile",       "tool.tile",       ICON_TILE,       true, false);
    ImGui::SameLine();
    RibbonSingleButton("cascade",    "tool.cascade",    ICON_CASCADE,    true, false);
    ImGui::EndGroup();
}

void Application::DrawRibbon() {
    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, 195), ImGuiCond_Always);
    ImGui::Begin("##ribbon", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

    DrawQuickAccessToolbar();
    ImGui::Separator();
    DrawRibbonTabs();
    ImGui::Separator();

    // Высота содержимого — 90px
    if (ImGui::BeginChild("##ribbon_content", ImVec2(0, 145), false,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        switch (myActiveRibbonTab) {
            case 0: DrawRibbonSketchTab();   break;
            case 1: DrawRibbonModelTab();    break;
            case 2: DrawRibbonAnnotateTab(); break;
            case 3: DrawRibbonToolsTab();    break;
            case 4: DrawRibbonManageTab();   break;
            case 5: DrawRibbonViewTab();     break;
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

void Application::DrawMenuBar() {
    // Оставлен для совместимости — но UI теперь полностью в DrawRibbon()
    // Вызывается из Run()
}

void Application::DrawDocumentTree() {
    ImGuiIO& io = ImGui::GetIO();
    const float panelWidth = myTreeWidth;

    ImGui::SetNextWindowPos(ImVec2(0, 200), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelWidth, io.DisplaySize.y - 200 - 56), ImGuiCond_Always);

    ImGui::Begin("##tree_panel", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoTitleBar);

    ImGui::Text("%s", Tr("panel.title").c_str());
    ImGui::Separator();

    if (ImGui::Button(Tr("panel.addBox").c_str())) {
        myShowAddBoxDialog = true;
        std::strncpy(myNewBoxName, Tr("default.boxName").c_str(), sizeof(myNewBoxName) - 1);
        myNewBoxName[sizeof(myNewBoxName) - 1] = '\0';
        myNewBoxPos[0] = myNewBoxPos[1] = myNewBoxPos[2] = 0.0f;
        myNewBoxSize[0] = myNewBoxSize[1] = myNewBoxSize[2] = 100.0f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) OnSave();
    ImGui::SameLine();
    if (ImGui::Button("Open")) OnOpen();
    ImGui::Separator();

    if (!myDocument || myDocument->GetDoc().IsNull()) {
        ImGui::Text("%s", Tr("panel.document.null").c_str());
        ImGui::End();
        return;
    }

    bool hasOps = myDocument->Has3DOperations();

    TDF_Label selectedLabel;
    if (myOcctView) selectedLabel = myOcctView->GetSelectedLabel();

    TDF_Label root = myDocument->MainLabel();

    if (ImGui::TreeNodeEx(Tr("panel.document").c_str(),
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {

        // === Папка Start ===
        TDF_Label baseLabel = myDocument->GetBaseGeometryLabel();
        if (!baseLabel.IsNull()) {
            // Start открыт, если:
            //  - нет 3D-операций, ИЛИ
            //  - пользователь развернул его вручную
            bool startOpen = !hasOps || myStartExpandedManually;

            ImVec4 startTxt = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            startTxt.w *= 0.65f;
            ImGui::PushStyleColor(ImGuiCol_Text, startTxt);
            ImGui::SetNextItemOpen(startOpen, ImGuiCond_Always);
            bool opened = ImGui::TreeNodeEx("Start",
                ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth);
            ImGui::PopStyleColor();

            // Пользователь кликнул — запоминаем его выбор
            if (ImGui::IsItemToggledOpen()) {
                myStartExpandedManually = opened;
            }

            if (opened) {
                // Внутри Start: только плоскости.
                // Origin Point — скрыт. Оси — скрыты, если есть 3D-операции.
                TDF_ChildIterator it(baseLabel, false);
                for (; it.More(); it.Next()) {
                    TDF_Label child = it.Value();
                    int type = BreezeDocument::GetType(child);

                    // Origin Point — никогда не показываем
                    if (type == FT_OriginPoint) continue;

                    // Оси не показываем вообще
                    if (type == FT_Axis) continue;

                    // Плоскости и (при отсутствии операций) оси — показываем
                    DrawLabelTree(child, selectedLabel, hasOps);
                }
                ImGui::TreePop();
            }
        }

        // === Пользовательские фичи (прямые дети Main, кроме Start) ===
        TDF_ChildIterator it(root, false);
        for (; it.More(); it.Next()) {
            TDF_Label child = it.Value();
            if (child.IsEqual(baseLabel)) continue;
            DrawLabelTree(child, selectedLabel, hasOps);
        }

        ImGui::TreePop();
    }

    ImGui::End();

    // Обработка отложенных операций
    if (!myPendingCreateSketchLabel.IsNull() && myDocument) {
        // Создаём эскиз и сразу запоминаем, куда войти
        TDF_Label parentPlane = myPendingCreateSketchLabel;
        TDF_Label newSketch = myDocument->AddSketchFeature(parentPlane);
        myPendingCreateSketchLabel = TDF_Label();

        // Автоматически входим в созданный эскиз
        if (!newSketch.IsNull()) {
            myPendingEnterSketchLabel = newSketch;
            std::cout << "Auto-enter sketch: tag=" << newSketch.Tag() << "\n";
        }
    }
    if (!myPendingEnterSketchLabel.IsNull() && myDocument) {
        EnterSketch(myPendingEnterSketchLabel);
        myPendingEnterSketchLabel = TDF_Label();
    }
    // myPendingDeleteLabel теперь обрабатывается через диалог подтверждения
}

void Application::RefreshSketchLines() {
    if (!myDocument || !myOcctView || myActiveSketchLabel.IsNull()) return;

    // Обходим дочерние метки эскиза и показываем их линии
    TDF_ChildIterator it(myActiveSketchLabel, false);
    for (; it.More(); it.Next()) {
        TDF_Label lineLabel = it.Value();
        TopoDS_Shape s = myDocument->GetShape(lineLabel);
        if (!s.IsNull()) {
            LineStyle style = BreezeDocument::GetLineStyle(lineLabel);
            myOcctView->DisplaySketchLine(s, lineLabel, static_cast<int>(style));
        }
    }
}

// Вычисление длины и угла в плоскости эскиза
static void ComputeLengthAngle(const gp_Pln& plane, const gp_Pnt& from, const gp_Pnt& to,
                               double& length, double& angleDeg) {
    gp_Ax3 ax = plane.Position();
    gp_Pnt origin = ax.Location();
    gp_Dir xDir = ax.XDirection();
    gp_Dir yDir = ax.YDirection();

    gp_Vec v(from, to);
    double u = v.Dot(gp_Vec(xDir));
    double w = v.Dot(gp_Vec(yDir));

    length = std::sqrt(u*u + w*w);
    angleDeg = std::atan2(w, u) * 180.0 / M_PI;
}

// Принудительное выравнивание угла к ближайшему кратному 90° (Компас-стиль)
static gp_Pnt ApplyOrtho(const gp_Pln& plane, const gp_Pnt& from, const gp_Pnt& to) {
    gp_Ax3 ax = plane.Position();
    gp_Pnt origin = ax.Location();
    gp_Dir xDir = ax.XDirection();
    gp_Dir yDir = ax.YDirection();

    gp_Vec v(from, to);
    double u = v.Dot(gp_Vec(xDir));
    double w = v.Dot(gp_Vec(yDir));

    double len = std::sqrt(u*u + w*w);
    if (len < 1e-6) return to;

    // Выбираем ближайшее направление: 0 / 90 / 180 / 270
    double u2 = 0.0, w2 = 0.0;
    if (std::abs(u) >= std::abs(w)) {
        u2 = (u >= 0) ? len : -len;
        w2 = 0.0;
    } else {
        u2 = 0.0;
        w2 = (w >= 0) ? len : -len;
    }

    gp_Pnt result = from;  // орто работает от стартовой точки
    result.Translate(gp_Vec(xDir) * u2);
    result.Translate(gp_Vec(yDir) * w2);
    return result;
}

// Проверка: попадает ли точка p в существующую вершину эскиза
// (Origin или конец уже нарисованной линии).
// skip — точка, которую нужно игнорировать (старт новой линии,
//        чтобы случайно не «замкнуться» на только что начатой линии).
static bool IsPointOnExistingVertex(const gp_Pnt& p, const TDF_Label& sketchLabel,
                                     const gp_Pnt& skip,
                                     double threshold) {
    auto dist2 = [](const gp_Pnt& a, const gp_Pnt& b) {
        double dx = a.X()-b.X(), dy = a.Y()-b.Y(), dz = a.Z()-b.Z();
        return dx*dx + dy*dy + dz*dz;
    };

    double th2 = threshold * threshold;

    // Origin
    gp_Pnt origin(0, 0, 0);
    if (dist2(p, origin) < th2 && dist2(skip, origin) > th2)
        return true;

    // Концы уже нарисованных линий эскиза
    TDF_ChildIterator it(sketchLabel, false);
    for (; it.More(); it.Next()) {
        TDF_Label lineLabel = it.Value();
        Handle(TNaming_NamedShape) ns;
        if (lineLabel.FindAttribute(TNaming_NamedShape::GetID(), ns) && !ns.IsNull()) {
            TopoDS_Shape s = ns->Get();
            if (!s.IsNull() && s.ShapeType() == TopAbs_EDGE) {
                TopoDS_Edge e = TopoDS::Edge(s);
                TopExp_Explorer exp(e, TopAbs_VERTEX);
                for (; exp.More(); exp.Next()) {
                    gp_Pnt vp = BRep_Tool::Pnt(TopoDS::Vertex(exp.Current()));
                    if (dist2(skip, vp) > th2 && dist2(p, vp) < th2) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

// Округление точки до сетки (шаг 10 мм) в плоскости эскиза.
static gp_Pnt SnapToGridPoint(const gp_Pnt& p, const gp_Pln& plane) {
    gp_Ax3 ax = plane.Position();
    gp_Pnt origin = ax.Location();
    gp_Dir xDir   = ax.XDirection();
    gp_Dir yDir   = ax.YDirection();

    gp_Vec v(origin, p);
    double u = v.Dot(gp_Vec(xDir));
    double w = v.Dot(gp_Vec(yDir));

    const double step = 10.0;
    u = std::round(u / step) * step;
    w = std::round(w / step) * step;

    gp_Pnt snapped = origin;
    snapped.Translate(gp_Vec(xDir) * u);
    snapped.Translate(gp_Vec(yDir) * w);
    return snapped;
}

// Привязка к известным точкам: Origin (0,0,0) + концы существующих линий эскиза
static bool SnapToKnownPoint(const gp_Pnt& p, const TDF_Label& sketchLabel,
                              const Handle(TDocStd_Document)& doc,
                              double threshold, gp_Pnt& out) {
    if (doc.IsNull() || sketchLabel.IsNull()) return false;

    std::vector<gp_Pnt> candidates;
    candidates.push_back(gp_Pnt(0, 0, 0));  // Origin

    // Концы линий эскиза
    TDF_ChildIterator it(sketchLabel, false);
    for (; it.More(); it.Next()) {
        TDF_Label lineLabel = it.Value();
        Handle(TNaming_NamedShape) ns;
        if (lineLabel.FindAttribute(TNaming_NamedShape::GetID(), ns) && !ns.IsNull()) {
            TopoDS_Shape s = ns->Get();
            if (!s.IsNull() && s.ShapeType() == TopAbs_EDGE) {
                TopoDS_Edge e = TopoDS::Edge(s);
                TopExp_Explorer exp(e, TopAbs_VERTEX);
                for (; exp.More(); exp.Next()) {
                    TopoDS_Vertex v = TopoDS::Vertex(exp.Current());
                    gp_Pnt vp = BRep_Tool::Pnt(v);
                    candidates.push_back(vp);
                }
            }
        }
    }

    double bestDist = threshold * threshold;
    bool found = false;
    for (const auto& c : candidates) {
        double dx = c.X() - p.X();
        double dy = c.Y() - p.Y();
        double dz = c.Z() - p.Z();
        double d2 = dx*dx + dy*dy + dz*dz;
        if (d2 < bestDist) {
            bestDist = d2;
            out = c;
            found = true;
        }
    }
    return found;
}

void Application::OnSketchClick(int px, int py) {
    if (!myOcctView || !myDocument || myActiveSketchLabel.IsNull()) return;

    // Если открыт диалог зависимостей — клик добавляет объекты в списки
    if (myDependencyDialogOpen) {
        if (!myOcctView->SelectAtPixel(px, py)) return;

        TDF_Label sel = myOcctView->GetSelectedLabel();
        if (sel.IsNull()) return;

        // Проверяем, что это линия эскиза
        if (BreezeDocument::GetType(sel) != FT_SketchLine) {
            myOcctView->ClearSelection();
            return;
        }

        // Проверяем, нет ли уже этой метки в списках
        auto inList = [&](const std::vector<TDF_Label>& lst) {
            for (const auto& l : lst) if (l.IsEqual(sel)) return true;
            return false;
        };

        if (!inList(myDependencyBase) && !inList(myDependencyDependent)) {
            // Первый клик — базовый, остальные — зависимые
            if (myDependencyBase.empty()) {
                myDependencyBase.push_back(sel);
                std::cout << "Dependency: base = " << GetLabelName(sel) << "\n";
            } else {
                myDependencyDependent.push_back(sel);
                std::cout << "Dependency: dependent = " << GetLabelName(sel) << "\n";
            }
        }

        myOcctView->ClearSelection();
        return;
    }

    gp_Pnt p;
    if (!myOcctView->ScreenToPlane(px, py, p)) return;

    // 1. Shift → ortho mode (45°)
    bool shiftHeld = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
                  || glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

    if (myHasSketchStart && shiftHeld) {
        p = ApplyOrtho(myOcctView->GetSketchPlane(), mySketchStartPoint, p);
    } else {
        // 2. Привязка к известным точкам
        bool snapped = false;
        if (mySnapToPoints) {
            gp_Pnt snapP;
            if (SnapToKnownPoint(p, myActiveSketchLabel, myDocument->GetDoc(),
                                  3.0, snapP)) {
                p = snapP;
                snapped = true;
            }
        }
        // 3. Привязка к сетке
        if (!snapped && mySnapToGrid) {
            p = SnapToGridPoint(p, myOcctView->GetSketchPlane());
        }
    }

    if (!myHasSketchStart) {
        mySketchStartPoint = p;
        myHasSketchStart = true;
        std::strcpy(myLengthBuffer, "0");
        std::strcpy(myAngleBuffer, "0");
        std::cout << "Sketch: start point set\n";
    } else {
        // Проверяем, что точки не совпадают
        double dx = p.X() - mySketchStartPoint.X();
        double dy = p.Y() - mySketchStartPoint.Y();
        double dz = p.Z() - mySketchStartPoint.Z();
        double dist2 = dx*dx + dy*dy + dz*dz;

        if (dist2 < 1e-4) {
            std::cout << "Sketch: points coincide, ignoring\n";
            return;
        }

        // 1. Проверка замыкания — ДО создания линии.
        //    Если конечная точка p совпадает с существующей вершиной эскиза
        //    (Origin или концом другой линии, кроме старта этой линии) — замыкание.
        bool loopClosed = IsPointOnExistingVertex(p, myActiveSketchLabel,
                                                   mySketchStartPoint,
                                                   0.5);  // порог 0.5 мм

        std::cout << "Sketch: loop check = " << (loopClosed ? "CLOSED" : "open")
                  << " (start was at " << mySketchStartPoint.X() << ","
                  << mySketchStartPoint.Y() << "," << mySketchStartPoint.Z()
                  << " end at " << p.X() << "," << p.Y() << "," << p.Z() << ")\n";

        std::cout << "Sketch: creating line with style="
                  << static_cast<int>(myCurrentLineStyle) << "\n";

        try {
            myDocument->AddSketchLine(myActiveSketchLabel, mySketchStartPoint, p, myCurrentLineStyle);
            myOcctView->ClearPreviewLine();

            TopoDS_Shape line = BRepBuilderAPI_MakeEdge(mySketchStartPoint, p).Shape();

            int maxTag = 0;
            TDF_ChildIterator it(myActiveSketchLabel, false);
            for (; it.More(); it.Next()) {
                if (it.Value().Tag() > maxTag) maxTag = it.Value().Tag();
            }
            TDF_Label lineLabel = myActiveSketchLabel.FindChild(maxTag, false);
            myOcctView->DisplaySketchLine(line, lineLabel, static_cast<int>(myCurrentLineStyle));

            if (myAutoContinue && !loopClosed) {
                mySketchStartPoint = p;
                myHasSketchStart = true;
                myOcctView->ShowCursorMarker(p);
                std::cout << "Sketch: line added, continuing\n";
            } else {
                myHasSketchStart = false;
                myOcctView->ClearPreviewLine();
                if (loopClosed) {
                    std::cout << "Sketch: line added, loop closed\n";
                } else {
                    std::cout << "Sketch: line added, stopped\n";
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Sketch: exception creating line: " << e.what() << "\n";
        } catch (...) {
            std::cerr << "Sketch: unknown exception creating line\n";
        }
    }
}

void Application::EnterSketch(const TDF_Label& sketchLabel) {
    if (!myDocument || !myOcctView || sketchLabel.IsNull()) return;

    // Родитель эскиза — плоскость. Берём её геометрию и вычисляем нормаль.
    TDF_Label parentPlane = sketchLabel.Father();
    if (parentPlane.IsNull()) {
        std::cerr << "EnterSketch: no parent plane\n";
        return;
    }

    TopoDS_Shape planeShape = myDocument->GetShape(parentPlane);
    if (planeShape.IsNull() || planeShape.ShapeType() != TopAbs_FACE) {
        std::cerr << "EnterSketch: parent is not a face\n";
        return;
    }

    TopoDS_Face face = TopoDS::Face(planeShape);
    BRepAdaptor_Surface surf(face);
    Standard_Real u = (surf.FirstUParameter() + surf.LastUParameter()) * 0.5;
    Standard_Real v = (surf.FirstVParameter() + surf.LastVParameter()) * 0.5;
    gp_Pnt p;
    gp_Vec du, dv;
    surf.D1(u, v, p, du, dv);
    gp_Dir normal = du.Crossed(dv);

    myOcctView->EnterSketchMode(normal, p);
    myOcctView->ShowGrid(myShowGrid);
    myActiveSketchLabel = sketchLabel;
    myHasSketchStart = false;
    myActiveRibbonTab = 0;  // Sketch tab

    // Показать уже существующие линии эскиза
    RefreshSketchLines();

    std::cout << "Application: entered sketch mode\n";
}

void Application::ExitSketch() {
    myHasSketchStart = false;
    if (myOcctView) {
        myOcctView->ClearPreviewLine();
        myOcctView->ExitSketchMode();
    }
    myActiveSketchLabel = TDF_Label();
    myActiveRibbonTab = 1;  // 3D Model tab

    // Принудительно перерисовываем всё из документа —
    // это гарантирует, что плоскости и другие системные объекты вернутся
    RefreshView();
    if (myOcctView) myOcctView->FitAll();

    std::cout << "Application: exited sketch mode\n";
}

static gp_Pnt PointFromLengthAngle(const gp_Pln& plane,
                                    const gp_Pnt& from,
                                    double length, double angleDeg) {
    gp_Ax3 ax = plane.Position();
    gp_Dir xDir = ax.XDirection();
    gp_Dir yDir = ax.YDirection();

    double rad = angleDeg * M_PI / 180.0;
    double u = length * std::cos(rad);
    double w = length * std::sin(rad);

    gp_Pnt result = from;
    result.Translate(gp_Vec(xDir) * u);
    result.Translate(gp_Vec(yDir) * w);
    return result;
}

void Application::ApplyExactLine() {
    if (!myOcctView || !myDocument || myActiveSketchLabel.IsNull()) return;
    if (!myHasSketchStart) return;

    double length = std::atof(myLengthBuffer);
    double angle  = std::atof(myAngleBuffer);

    if (length < 1e-6) {
        std::cout << "ApplyExactLine: length too small\n";
        return;
    }

    gp_Pnt p = PointFromLengthAngle(myOcctView->GetSketchPlane(),
                                     mySketchStartPoint,
                                     length, angle);

    try {
        myDocument->AddSketchLine(myActiveSketchLabel, mySketchStartPoint, p, myCurrentLineStyle);
        myOcctView->ClearPreviewLine();

        TopoDS_Shape line = BRepBuilderAPI_MakeEdge(mySketchStartPoint, p).Shape();
        int maxTag = 0;
        TDF_ChildIterator it(myActiveSketchLabel, false);
        for (; it.More(); it.Next()) {
            if (it.Value().Tag() > maxTag) maxTag = it.Value().Tag();
        }
        TDF_Label lineLabel = myActiveSketchLabel.FindChild(maxTag, false);
        myOcctView->DisplaySketchLine(line, lineLabel, static_cast<int>(myCurrentLineStyle));

        bool loopClosed = IsPointOnExistingVertex(p, myActiveSketchLabel,
                                                   mySketchStartPoint, 0.5);

        if (myAutoContinue && !loopClosed) {
            mySketchStartPoint = p;
            myHasSketchStart = true;
            myOcctView->ShowCursorMarker(p);
            std::cout << "ApplyExactLine: line added, continuing\n";
        } else {
            myHasSketchStart = false;
            if (loopClosed) std::cout << "ApplyExactLine: loop closed\n";
        }

        std::strcpy(myLengthBuffer, "0");
        std::strcpy(myAngleBuffer, "0");
        myInputActive = false;
    } catch (...) {
        std::cerr << "ApplyExactLine exception\n";
    }
}

void Application::DrawPreviewInfo() {
    if (myActiveSketchLabel.IsNull()) return;
    if (!myHasSketchStart) return;

    ImGuiIO& io = ImGui::GetIO();

    // Фиксированное положение — правый верхний угол, отступ 20 пикселей
    ImVec2 pos = ImVec2(io.DisplaySize.x - 280, 60);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(260, 0), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.9f);

    ImGui::Begin("##preview_info", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);

    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Length: %.2f mm", myPreviewLength);
    ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Angle:  %.1f deg", myPreviewAngle);

    ImGui::Separator();
    ImGui::Text("Exact input:");

    // Tab — фокус на Length
    static bool focusLength = false;
    if (ImGui::IsKeyPressed(ImGuiKey_Tab) && !ImGui::GetIO().WantTextInput) {
        focusLength = true;
    }

    ImGui::SetNextItemWidth(120);
    if (focusLength) {
        ImGui::SetKeyboardFocusHere();
        focusLength = false;
    }
    bool enterLength = ImGui::InputText("Length", myLengthBuffer, sizeof(myLengthBuffer),
                                         ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::SetNextItemWidth(120);
    bool enterAngle = ImGui::InputText("Angle", myAngleBuffer, sizeof(myAngleBuffer),
                                        ImGuiInputTextFlags_EnterReturnsTrue);

    if (enterLength || enterAngle) {
        ApplyExactLine();
    }

    ImGui::Separator();
    if (ImGui::Button("Apply", ImVec2(120, 0))) {
        ApplyExactLine();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        // Отмена текущей цепочки
        myHasSketchStart = false;
        myOcctView->ClearPreviewLine();
    }

    ImGui::TextDisabled("Tab - focus Length");
    ImGui::TextDisabled("Enter - apply");

    ImGui::End();
}

void Application::DrawSketchOverlay() {
    // Пустая — весь UI перенесён в Ribbon и Status Bar
}

void Application::DrawViewCube() {
    if (!myOcctView) return;

    ImGuiIO& io = ImGui::GetIO();

    const float cubeSize   = 100.0f;
    const float cubeHalf   = 38.0f;
    const float margin     = 15.0f;
    const float topOffset  = 210.0f;

    ImVec2 center(io.DisplaySize.x - margin - cubeSize * 0.5f,
                  topOffset + cubeSize * 0.5f);

    ImDrawList* draw = ImGui::GetForegroundDrawList();

    // Фон
    draw->AddCircleFilled(center, cubeSize * 0.55f, IM_COL32(40, 45, 55, 180));

    // Ориентация камеры
    gp_Dir camDir   = myOcctView->GetCameraDirection();
    gp_Dir camUp    = myOcctView->GetCameraUp();

    // Защита: cross-product нулевой, если dir и up параллельны.
    // В этом случае подставляем правый вектор по эвристике.
    gp_Vec crossVec = camDir.Crossed(camUp);
    gp_Dir camRight;
    if (crossVec.SquareMagnitude() < 1e-9) {
        // Выбираем "right" так, чтобы он был не параллелен camDir
        gp_Dir fallback(1, 0, 0);
        if (std::abs(camDir.Dot(fallback)) > 0.9) fallback = gp_Dir(0, 1, 0);
        camRight = camDir.Crossed(fallback);
    } else {
        camRight = gp_Dir(crossVec);
    }

    // 8 вершин куба ±1
    gp_Pnt C[8] = {
        {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},
        {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}
    };

    auto project = [&](const gp_Pnt& p) -> ImVec2 {
        double x = p.X()*camRight.X() + p.Y()*camRight.Y() + p.Z()*camRight.Z();
        double y = p.X()*camUp.X()    + p.Y()*camUp.Y()    + p.Z()*camUp.Z();
        return ImVec2(center.x + (float)x * cubeHalf,
                      center.y - (float)y * cubeHalf);
    };
    auto depthOf = [&](const gp_Pnt& p) -> double {
        return p.X()*camDir.X() + p.Y()*camDir.Y() + p.Z()*camDir.Z();
    };

    struct Face { int v[4]; gp_Dir n; const char* label; };
    Face faces[6] = {
        {{4,5,6,7}, {0,0, 1}, "TOP"},
        {{0,3,2,1}, {0,0,-1}, "BOT"},
        {{0,1,5,4}, {0,-1,0}, "FRONT"},
        {{2,3,7,6}, {0, 1,0}, "BACK"},
        {{1,2,6,5}, { 1,0,0}, "RIGHT"},
        {{3,0,4,7}, {-1,0,0}, "LEFT"},
    };

    struct Visible { int idx; double depth; ImVec2 pts[4]; };
    std::vector<Visible> vis;

    for (int fi = 0; fi < 6; fi++) {
        if (faces[fi].n.Dot(camDir) >= -0.01) continue;  // back-facing
        Visible v;
        v.idx = fi;
        double d = 0;
        for (int i = 0; i < 4; i++) {
            v.pts[i] = project(C[faces[fi].v[i]]);
            d += depthOf(C[faces[fi].v[i]]);
        }
        v.depth = d / 4.0;
        vis.push_back(v);
    }

    std::sort(vis.begin(), vis.end(),
              [](const Visible& a, const Visible& b){ return a.depth < b.depth; });

    auto faceColor = [](const gp_Dir& n) -> ImU32 {
        if (n.Z() > 0.5)  return IM_COL32(180, 195, 210, 255);
        if (n.Z() < -0.5) return IM_COL32(120, 130, 145, 255);
        if (n.Y() < -0.5) return IM_COL32(160, 175, 195, 255);
        if (n.Y() > 0.5)  return IM_COL32(140, 155, 175, 255);
        if (n.X() > 0.5)  return IM_COL32(150, 165, 185, 255);
        return IM_COL32(130, 145, 165, 255);
    };

    // Mouse hover detection
    ImVec2 mouse = io.MousePos;
    int hoveredFace = -1;
    if (!io.WantCaptureMouse) {
        for (int i = (int)vis.size() - 1; i >= 0; i--) {
            auto& v = vis[i];
            int sign = 0; bool inside = true;
            for (int e = 0; e < 4; e++) {
                ImVec2 a = v.pts[e], b = v.pts[(e+1)%4];
                float cr = (b.x-a.x)*(mouse.y-a.y) - (b.y-a.y)*(mouse.x-a.x);
                int s = (cr > 0) ? 1 : -1;
                if (sign == 0) sign = s;
                else if (sign != s) { inside = false; break; }
            }
            if (inside) { hoveredFace = i; break; }
        }
    }

    // Draw faces
    for (int i = 0; i < (int)vis.size(); i++) {
        auto& v = vis[i];
        ImU32 col     = faceColor(faces[v.idx].n);
        ImU32 outline = IM_COL32(30, 35, 45, 255);
        if (i == hoveredFace) col = IM_COL32(255, 220, 120, 255);
        draw->AddConvexPolyFilled(v.pts, 4, col);
        draw->AddPolyline(v.pts, 4, outline, ImDrawFlags_Closed, 1.5f);
    }

    // Labels
    for (auto& v : vis) {
        ImVec2 c2(0, 0);
        for (int i = 0; i < 4; i++) { c2.x += v.pts[i].x; c2.y += v.pts[i].y; }
        c2.x *= 0.25f; c2.y *= 0.25f;
        ImVec2 ts = ImGui::CalcTextSize(faces[v.idx].label);
        draw->AddText(ImVec2(c2.x - ts.x*0.5f, c2.y - ts.y*0.5f),
                      IM_COL32(25, 30, 40, 255), faces[v.idx].label);
    }

    // Click handling — защита от выхода за границы
    if (hoveredFace >= 0 && hoveredFace < (int)vis.size()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            gp_Dir n = faces[vis[hoveredFace].idx].n;
            gp_Dir newDir = n.Reversed();
            gp_Dir newUp(0, 0, 1);
            if (std::abs(newDir.Z()) > 0.5) {
                newUp = gp_Dir(0, 1, 0);  // top/bottom view: Y up
            }
            myOcctView->SetCameraDirectionAndUp(newDir, newUp);
        }
    }

    // Rotation arrows below cube
    float arrowY = center.y + cubeSize * 0.5f + 10.0f;
    ImGui::SetNextWindowPos(ImVec2(center.x - 55, arrowY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(110, 28), ImGuiCond_Always);
    ImGui::Begin("##viewcube_arrows", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(60, 70, 90, 220));
    if (ImGui::Button("<<", ImVec2(50, 24))) {
        myOcctView->RotateAroundUp(90.0);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Повернуть влево на 90°");
    ImGui::SameLine();
    if (ImGui::Button(">>", ImVec2(50, 24))) {
        myOcctView->RotateAroundUp(-90.0);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Повернуть вправо на 90°");
    ImGui::PopStyleColor();

    ImGui::End();
}

void Application::DrawStatusBar() {
    ImGuiIO& io = ImGui::GetIO();
    const float h = 24.0f;

    ImGui::SetNextWindowPos(ImVec2(0, io.DisplaySize.y - h), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, h), ImGuiCond_Always);
    ImGui::Begin("##statusbar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (!myActiveSketchLabel.IsNull()) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s",
                            Tr("sketch.mode").c_str());
        ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();
        ImGui::Text("%s", Tr("sketch.hint.lmb").c_str());
        ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();
        ImGui::Text("%s", Tr("sketch.hint.shift").c_str());
        ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();
        ImGui::Text("%s", Tr("sketch.hint.esc").c_str());

        if (myHasSketchStart) {
            ImGui::SameLine(); ImGui::Text("|"); ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f),
                                "L=%.2f mm, A=%.1f deg",
                                myPreviewLength, myPreviewAngle);
        }
    } else {
        ImGui::Text(" ");
    }

    ImGui::End();
}

void Application::DrawAdvancedTooltip(const std::string& id,
                                       const std::string& name,
                                       const std::string& shortDesc,
                                       const std::string& longDesc,
                                       const std::string& imagePath) {
    // Вызывается сразу после кнопки. Проверяем — наведена ли она.
    if (!ImGui::IsItemHovered()) {
        // Если это тот же элемент, что сейчас в hover — сбрасываем
        if (myHoveredItemId == id) {
            myHoveredItemId.clear();
            myHoverStartTime = 0.0;
            myLastTooltipLevel = 0;
        }
        return;
    }

    // Только что навели на новый элемент
    if (myHoveredItemId != id) {
        myHoveredItemId = id;
        myHoverStartTime = ImGui::GetTime();
        myLastTooltipLevel = 0;
    }

    double elapsed = ImGui::GetTime() - myHoverStartTime;

    int level = 0;
    if (elapsed >= 3.0)      level = 3;
    else if (elapsed >= 1.5) level = 2;
    else if (elapsed >= 0.5) level = 1;
    else                     return;  // ещё рано

    if (level == 0) return;

    // Рисуем tooltip
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.0f);

    // Уровень 1: название
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", name.c_str());

    // Уровень 2: краткое описание
    if (level >= 2 && !shortDesc.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted(shortDesc.c_str());
    }

    // Уровень 3: подробное описание + картинка
    if (level >= 3) {
        if (!longDesc.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("%s", longDesc.c_str());
        }
        if (!imagePath.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("(картинка: %s)", imagePath.c_str());
            // TODO: загрузка PNG через imgui — следующая итерация
        }
    }

    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

void Application::RequestDelete(const TDF_Label& label) {
    if (label.IsNull()) return;

    if (mySkipDeleteConfirm) {
        // Удаляем сразу
        if (myDocument) myDocument->RemoveFeature(label);
        RefreshView();
        if (myOcctView) myOcctView->FitAll();
        return;
    }

    myPendingDeleteLabel = label;

    // Собираем информацию: имя + сколько детей удалится
    std::string name = "<unnamed>";
    Handle(TDataStd_Name) nameAttr;
    if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
        name = ExtToUtf8(nameAttr->Get());
    }

    int childCount = 0;
    TDF_ChildIterator it(label, false);
    for (; it.More(); it.Next()) childCount++;

    if (childCount > 0) {
        std::snprintf(myDeleteConfirmInfo, sizeof(myDeleteConfirmInfo),
                      "Удалить «%s»?\n\nВместе с ним будут удалены %d вложенных элементов.",
                      name.c_str(), childCount);
    } else {
        std::snprintf(myDeleteConfirmInfo, sizeof(myDeleteConfirmInfo),
                      "Удалить «%s»?", name.c_str());
    }

    myShowDeleteConfirm = true;
}

void Application::DrawRenameDialog() {
    if (!myShowRenameDialog) return;

    ImGui::OpenPopup("Переименовать");

    if (ImGui::BeginPopupModal("Переименовать", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Новое имя:");
        ImGui::SetNextItemWidth(280);
        ImGui::InputText("##rename_buf", myRenameBuffer, sizeof(myRenameBuffer),
                         ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::Separator();

        if (ImGui::Button("OK", ImVec2(120, 0))) {
            if (myDocument && !myPendingRenameLabel.IsNull() && myRenameBuffer[0] != '\0') {
                myDocument->RenameFeature(myPendingRenameLabel, myRenameBuffer);
                myPendingRenameLabel = TDF_Label();
            }
            myShowRenameDialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Отмена", ImVec2(120, 0))) {
            myPendingRenameLabel = TDF_Label();
            myShowRenameDialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Application::DrawDeleteConfirmDialog() {
    if (!myShowDeleteConfirm) return;

    ImGui::OpenPopup("Подтверждение удаления");

    if (ImGui::BeginPopupModal("Подтверждение удаления", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", myDeleteConfirmInfo);
        ImGui::Separator();
        ImGui::Checkbox("Не спрашивать снова (до перезапуска)", &mySkipDeleteConfirm);
        ImGui::Separator();

        if (ImGui::Button("Удалить", ImVec2(120, 0))) {
            if (myDocument && !myPendingDeleteLabel.IsNull()) {
                myDocument->RemoveFeature(myPendingDeleteLabel);
                myPendingDeleteLabel = TDF_Label();
                RefreshView();
                if (myOcctView) myOcctView->FitAll();
            }
            myShowDeleteConfirm = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Отмена", ImVec2(120, 0))) {
            myPendingDeleteLabel = TDF_Label();
            myShowDeleteConfirm = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Application::DrawUnsavedChangesDialog() {
    if (!myShowUnsavedDialog) return;

    ImGui::OpenPopup("Несохранённые изменения");

    if (ImGui::BeginPopupModal("Несохранённые изменения", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", myUnsavedDialogInfo);
        ImGui::Separator();

        if (ImGui::Button("Сохранить", ImVec2(120, 0))) {
            int idx = myPendingCloseDocIndex;
            bool wasWindow = myPendingWindowClose;

            if (idx >= 0 && idx < (int)myDocuments.size()) {
                BreezeDocument* prevDoc = myDocument;
                int prevIdx = myActiveDocIndex;
                myDocument = myDocuments[idx].get();
                myActiveDocIndex = idx;
                OnSave();
                bool saved = !myDocument->IsModified();
                if (!saved) {
                    myDocument = prevDoc;
                    myActiveDocIndex = prevIdx;
                    return;
                }
            }

            myPendingCloseDocIndex = -1;
            myPendingWindowClose = false;
            myShowUnsavedDialog = false;
            ImGui::CloseCurrentPopup();

            if (wasWindow) {
                glfwSetWindowShouldClose(myWindow->GetNativeWindow(), GLFW_TRUE);
            } else if (idx >= 0) {
                CloseDoc(idx);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Не сохранять", ImVec2(120, 0))) {
            int idx = myPendingCloseDocIndex;
            bool wasWindow = myPendingWindowClose;
            myPendingCloseDocIndex = -1;
            myPendingWindowClose = false;
            myShowUnsavedDialog = false;
            ImGui::CloseCurrentPopup();

            if (idx >= 0 && idx < (int)myDocuments.size()) {
                myDocuments[idx]->SetModified(false);
            }
            if (wasWindow) {
                glfwSetWindowShouldClose(myWindow->GetNativeWindow(), GLFW_TRUE);
            } else if (idx >= 0 && idx < (int)myDocuments.size()) {
                CloseDoc(idx);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Отмена", ImVec2(120, 0))) {
            myPendingCloseDocIndex = -1;
            myPendingWindowClose = false;
            myShowUnsavedDialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Application::DrawPropertiesDialog() {
    if (!myShowPropertiesDialog) return;

    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Свойства", &myShowPropertiesDialog)) {
        if (myPendingPropertiesLabel.IsNull() || !myDocument) {
            ImGui::TextDisabled("Объект не выбран");
        } else {
            std::string name = "<unnamed>";
            Handle(TDataStd_Name) nameAttr;
            if (myPendingPropertiesLabel.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
                name = ExtToUtf8(nameAttr->Get());
            }

            int type = BreezeDocument::GetType(myPendingPropertiesLabel);

            ImGui::Text("Имя: %s", name.c_str());
            ImGui::Text("Тип: %d", type);
            ImGui::Text("Тег: %d", myPendingPropertiesLabel.Tag());

            const char* typeName = "Неизвестно";
            switch (type) {
                case FT_Start:       typeName = "Папка Start"; break;
                case FT_Plane:       typeName = "Плоскость"; break;
                case FT_Axis:        typeName = "Ось"; break;
                case FT_OriginPoint: typeName = "Точка Origin"; break;
                case FT_Box:         typeName = "Куб"; break;
                case FT_Cylinder:    typeName = "Цилиндр"; break;
                case FT_Hole:        typeName = "Отверстие"; break;
                case FT_Sketch:      typeName = "Эскиз"; break;
                case FT_SketchLine:  typeName = "Линия эскиза"; break;
            }
            ImGui::Text("Описание: %s", typeName);

            ImGui::Separator();
            ImGui::TextDisabled("(расширенные свойства — позже)");
        }
    }
    ImGui::End();
}

bool Application::IsLabelVisibleByDefault(const TDF_Label& label, bool hasOps) const {
    if (label.IsNull()) return false;

    int type = BreezeDocument::GetType(label);

    // Скрываемые системные элементы
    bool isSystemHideable = (type == FT_Axis || type == FT_Plane);
    if (isSystemHideable && hasOps) return false;

    return true;
}

void Application::ShowAllRespectingDefaults() {
    if (!myOcctView || !myDocument) return;

    // Сбрасываем карту видимости полностью — она была навязана изоляцией
    myVisibility.clear();
    myIsolationBackup.clear();
    myIsolationActive = false;

    bool hasOps = myDocument->Has3DOperations();

    // Проходим по всем объектам и применяем правила видимости
    auto allLabels = myOcctView->GetAllLabels();
    for (const auto& lbl : allLabels) {
        bool visible = IsLabelVisibleByDefault(lbl, hasOps);
        myOcctView->SetShapeVisible(lbl, visible);
    }

    std::cout << "ShowAllRespectingDefaults: restored\n";
}

void Application::DrawContextMenuItems(const TDF_Label& label, bool hasOps) {
    if (label.IsNull()) return;

    std::string labelName = "<unnamed>";
    Handle(TDataStd_Name) nameAttr;
    if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
        labelName = ExtToUtf8(nameAttr->Get());
    }

    int type = BreezeDocument::GetType(label);
    bool isSystem = (type < 0);
    std::string key = LabelKey(label);

    // === Сброс изоляции (если активна) ===
    if (myIsolationActive) {
        if (ImGui::MenuItem("Показать всё (снять изоляцию)")) {
            ShowAllRespectingDefaults();
        }
        ImGui::Separator();
    }

    // === Плоскость ===
    if (type == FT_Plane) {
        if (ImGui::MenuItem("Создать эскиз")) {
            myPendingCreateSketchLabel = label;
        }
        ImGui::Separator();
    }

    // === Эскиз ===
    if (type == FT_Sketch) {
        if (ImGui::MenuItem("Редактировать эскиз")) {
            myPendingEnterSketchLabel = label;
        }
        ImGui::Separator();
    }

    // === 3D-операция ===
    if (type == FT_Box || type == FT_Cylinder || type == FT_Hole) {
        if (ImGui::MenuItem("Редактировать")) {}
        if (ImGui::MenuItem("Измерить")) {}
        if (ImGui::MenuItem("Изолировать")) {
            if (myOcctView && myDocument) {
                // 1. Копируем список меток ДО любых изменений
                std::vector<TDF_Label> allLabels = myOcctView->GetAllLabels();

                // 2. Сохраняем текущую видимость для последующего "Показать всё"
                myIsolationBackup = myVisibility;
                myIsolationActive = true;

                // 3. Скрываем всё, кроме выделенного
                for (const auto& entry : allLabels) {
                    if (entry.IsEqual(label)) continue;
                    if (BreezeDocument::IsSystem(entry)) continue;  // системные не трогаем

                    std::string k = LabelKey(entry);
                    myVisibility[k] = false;
                    myOcctView->SetShapeVisible(entry, false);
                }
            }
        }
        ImGui::Separator();
    }

    // === Start ===
    if (type == FT_Start) {
        if (ImGui::MenuItem("Показать все плоскости и оси")) {
            TDF_ChildIterator it(label, false);
            for (; it.More(); it.Next()) {
                TDF_Label child = it.Value();
                int ct = BreezeDocument::GetType(child);
                if (ct == FT_Plane || ct == FT_Axis) {
                    std::string k = LabelKey(child);
                    myVisibility[k] = true;
                    if (myOcctView) myOcctView->SetShapeVisible(child, true);
                }
            }
        }
        if (ImGui::MenuItem("Скрыть все плоскости и оси")) {
            TDF_ChildIterator it(label, false);
            for (; it.More(); it.Next()) {
                TDF_Label child = it.Value();
                int ct = BreezeDocument::GetType(child);
                if (ct == FT_Plane || ct == FT_Axis) {
                    std::string k = LabelKey(child);
                    myVisibility[k] = false;
                    if (myOcctView) myOcctView->SetShapeVisible(child, false);
                }
            }
        }
        ImGui::Separator();
    }

    // === Скрыть / Показать ===
    if (isSystem && (type == FT_Plane || type == FT_Axis)) {
        auto it = myVisibility.find(key);
        bool curVisible = (it != myVisibility.end()) ? it->second : !hasOps;

        if (ImGui::MenuItem(curVisible ? ICON_EYE_SLASH "  Скрыть" : ICON_EYE "  Показать")) {
            myVisibility[key] = !curVisible;
            if (myOcctView) myOcctView->SetShapeVisible(label, !curVisible);
        }
        ImGui::Separator();
    }

    // === Копировать ===
    if (!isSystem) {
        if (ImGui::MenuItem("Копировать")) {
            myClipboardLabel = label;
            std::cout << "Copied: " << labelName << "\n";
        }
    }

    // === Вставить ===
    if (!myClipboardLabel.IsNull()) {
        if (ImGui::MenuItem("Вставить")) {
            std::cout << "Paste — TODO\n";
        }
    }

    if (!isSystem) {
        ImGui::Separator();
    }

    // === Переименовать ===
    if (!isSystem) {
        if (ImGui::MenuItem("Переименовать")) {
            myPendingRenameLabel = label;
            std::strncpy(myRenameBuffer, labelName.c_str(), sizeof(myRenameBuffer) - 1);
            myRenameBuffer[sizeof(myRenameBuffer) - 1] = '\0';
            myShowRenameDialog = true;
        }
    }

    // === Свойства ===
    if (ImGui::MenuItem("Свойства...")) {
        myPendingPropertiesLabel = label;
        myShowPropertiesDialog = true;
    }

    // === Удалить ===
    if (!isSystem) {
        ImGui::Separator();
        if (ImGui::MenuItem("Удалить")) {
            RequestDelete(label);
        }
    }
}

void Application::DrawContextMenu3D() {
    if (myPendingContextMenu) {
        ImGui::SetNextWindowPos(myContextMenuPos, ImGuiCond_Always);
        ImGui::OpenPopup("##ctx_3d");
        myPendingContextMenu = false;
    }

    if (ImGui::BeginPopup("##ctx_3d")) {
        TDF_Label sel;
        if (myOcctView) sel = myOcctView->GetSelectedLabel();

        if (sel.IsNull()) {
            // === ПКМ по пустому месту ===
            if (ImGui::MenuItem("Домой (F6)")) {
                if (myOcctView) myOcctView->SetHomeView();
            }
            if (ImGui::MenuItem("Изометрия")) {
                if (myOcctView) myOcctView->SetHomeView();
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Вид")) {
                if (ImGui::MenuItem("Сверху"))
                    myOcctView->SetCameraDirectionAndUp(gp_Dir(0, 0, -1), gp_Dir(0, 1, 0));
                if (ImGui::MenuItem("Снизу"))
                    myOcctView->SetCameraDirectionAndUp(gp_Dir(0, 0, 1), gp_Dir(0, 1, 0));
                if (ImGui::MenuItem("Спереди"))
                    myOcctView->SetCameraDirectionAndUp(gp_Dir(0, -1, 0), gp_Dir(0, 0, 1));
                if (ImGui::MenuItem("Сзади"))
                    myOcctView->SetCameraDirectionAndUp(gp_Dir(0, 1, 0), gp_Dir(0, 0, 1));
                if (ImGui::MenuItem("Справа"))
                    myOcctView->SetCameraDirectionAndUp(gp_Dir(-1, 0, 0), gp_Dir(0, 0, 1));
                if (ImGui::MenuItem("Слева"))
                    myOcctView->SetCameraDirectionAndUp(gp_Dir(1, 0, 0), gp_Dir(0, 0, 1));
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Показать всё")) {
                if (myOcctView) myOcctView->FitAll();
            }
        } else {
            // === ПКМ по объекту — то же меню, что и в дереве ===
            std::string name = "<unnamed>";
            Handle(TDataStd_Name) nameAttr;
            if (sel.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
                name = ExtToUtf8(nameAttr->Get());
            }

            ImGui::TextDisabled("%s", name.c_str());
            ImGui::Separator();

            bool hasOps = myDocument ? myDocument->Has3DOperations() : false;
            DrawContextMenuItems(sel, hasOps);
        }
        ImGui::EndPopup();
    }
}

void Application::DrawTreeSplitter() {
    ImGuiIO& io = ImGui::GetIO();

    const float splitterW   = 5.0f;
    const float minTreeW    = 200.0f;
    const float maxTreeW    = 600.0f;

    const float topY    = 200.0f;
    const float bottomY = io.DisplaySize.y - 56.0f;
    const float xPos    = myTreeWidth;

    // Невидимое окно-полоса поверх границы
    ImGui::SetNextWindowPos(ImVec2(xPos - splitterW * 0.5f, topY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(splitterW, bottomY - topY), ImGuiCond_Always);
    ImGui::Begin("##tree_splitter", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::InvisibleButton("##splitter_btn", ImVec2(splitterW, bottomY - topY));

    bool hovered = ImGui::IsItemHovered();
    bool active  = ImGui::IsItemActive();

    if (hovered || active) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    if (active) {
        myTreeWidth += io.MouseDelta.x;
        if (myTreeWidth < minTreeW) myTreeWidth = minTreeW;
        if (myTreeWidth > maxTreeW) myTreeWidth = maxTreeW;
    }

    // Визуальный индикатор при перетаскивании
    if (active) {
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        draw->AddLine(ImVec2(xPos, topY), ImVec2(xPos, bottomY),
                      IM_COL32(80, 130, 180, 255), 2.0f);
    }

    ImGui::End();
}

void Application::DrawAddBoxDialog() {
    if (myShowAddBoxDialog) {
        ImGui::OpenPopup(Tr("dlg.addBox.title").c_str());
        myShowAddBoxDialog = false;
    }
    if (ImGui::BeginPopupModal(Tr("dlg.addBox.title").c_str(), nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText(Tr("dlg.name").c_str(), myNewBoxName, sizeof(myNewBoxName));
        ImGui::Separator();
        ImGui::Text("%s", Tr("dlg.position").c_str());
        ImGui::InputFloat("X##pos", &myNewBoxPos[0]);
        ImGui::InputFloat("Y##pos", &myNewBoxPos[1]);
        ImGui::InputFloat("Z##pos", &myNewBoxPos[2]);
        ImGui::Separator();
        ImGui::Text("%s", Tr("dlg.size").c_str());
        ImGui::InputFloat("DX", &myNewBoxSize[0]);
        ImGui::InputFloat("DY", &myNewBoxSize[1]);
        ImGui::InputFloat("DZ", &myNewBoxSize[2]);
        ImGui::Separator();
        if (ImGui::Button(Tr("btn.ok").c_str(), ImVec2(120, 0))) {
            if (myDocument) {
                myDocument->AddBoxFeature(myNewBoxName,
                    myNewBoxSize[0], myNewBoxSize[1], myNewBoxSize[2],
                    myNewBoxPos[0], myNewBoxPos[1], myNewBoxPos[2]);
                RefreshView();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(Tr("btn.cancel").c_str(), ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void Application::DrawDependenciesPanel() {
    ImGuiIO& io = ImGui::GetIO();

    // Панель только когда в эскизе
    if (myActiveSketchLabel.IsNull()) return;

    const float panelW = 280.0f;
    const float panelTop = 340.0f;   // под ViewCube + стрелками
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - panelW - 15, panelTop), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelW, 0), ImGuiCond_Always);

    ImGui::Begin(Tr("panel.dependencies").c_str(), nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::TextDisabled("%s", Tr("panel.deps.hint").c_str());

    // === Здесь будут отображаться ограничения эскиза (Coincident, Parallel, и т.д.) ===
    // Пока пусто — заполним, когда добавим solver.
    if (ImGui::BeginListBox("##deps_list", ImVec2(-1, 120))) {
        ImGui::TextDisabled("%s", Tr("panel.deps.empty").c_str());
        ImGui::EndListBox();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Fully Constrained: NO");
    ImGui::TextDisabled("DOF remaining: ?");

    ImGui::End();
}

void Application::DrawProjectTabs() {
    ImGuiIO& io = ImGui::GetIO();

    const float statusH = 24.0f;
    const float tabsH   = 32.0f;
    const float yPos    = io.DisplaySize.y - statusH - tabsH;

    ImGui::SetNextWindowPos(ImVec2(0, yPos), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, tabsH), ImGuiCond_Always);
    ImGui::Begin("##project_tabs", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    // === Вкладка "Главная" ===
    {
        bool isHome = (myActiveDocIndex < 0);
        if (isHome) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.95f, 1.0f));
        if (SvgIconBtn(myIcons, "tab_home", "house", 14, 120, 24, "Главная")) {
            // Home — сбросить активный документ (но не закрыть)
            myActiveDocIndex = -1;
            myDocument = nullptr;
            myActiveSketchLabel = TDF_Label();
            myHasSketchStart = false;
            if (myOcctView) {
                myOcctView->ClearAllShapes();
                myOcctView->ClearGrid();
                myOcctView->ExitSketchMode();
            }
        }
        if (isHome) ImGui::PopStyleColor();
    }

    // === Вкладки проектов ===
    static int pendingCloseIdx = -1;
    static int switchToIdx    = -2;  // -2 = no change

    for (int i = 0; i < (int)myDocuments.size(); i++) {
        ImGui::SameLine();

        ImGui::PushID(i);

        bool active = (i == myActiveDocIndex);
        bool isLight = (ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).x > 0.5f);
        ImVec4 accent(0.20f, 0.55f, 0.95f, 1.0f);
        ImVec4 accentHover(0.30f, 0.65f, 1.00f, 1.0f);
        ImVec4 accentActive(0.15f, 0.45f, 0.85f, 1.0f);

        ImVec4 colNormal = active ? accent
                                   : ImGui::GetStyleColorVec4(ImGuiCol_Button);
        ImVec4 colHover  = active ? accentHover
                                   : ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
        ImVec4 colActive = active ? accentActive
                                   : ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);

        ImGui::PushStyleColor(ImGuiCol_Button, colNormal);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, colActive);

        // Имя вкладки + " *" если не сохранён
        std::string label = myProjectNames[i];
        if (myProjectPaths[i].empty()) label += " *";

        if (ImGui::Button((label + "##tab").c_str(), ImVec2(140, 24))) {
            switchToIdx = i;
        }

        // ПКМ — контекстное меню
        if (ImGui::BeginPopupContextItem("##tab_ctx")) {
            if (ImGui::MenuItem("Сохранить")) {
                // TODO: save this doc
            }
            if (ImGui::MenuItem("Сохранить как...")) {
                // TODO: save as
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Закрыть")) {
                pendingCloseIdx = i;
            }
            if (ImGui::MenuItem("Закрыть другие")) {
                // TODO: close others
            }
            ImGui::EndPopup();
        }

        ImGui::PopStyleColor(3);

        // Кнопка × закрытия — только если больше одного документа
        if (myDocuments.size() > 1) {
            ImGui::SameLine(0, 2);
            if (SvgIconBtn(myIcons, "tab_close", "x", 12, 20, 24, "Закрыть")) {
                pendingCloseIdx = i;
            }
        }

        ImGui::PopID();
    }

    // Кнопка "+" — создать новый проект
    ImGui::SameLine();
    if (SvgIconBtn(myIcons, "tab_plus", "plus", 14, 28, 24, "Новый проект")) {
        CreateNewDoc();
        RefreshView();
        if (myOcctView) myOcctView->FitAll();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Новый проект");

    // === Прогресс-бар справа (placeholder) ===
    {
        float panelW = 200.0f;
        ImGui::SameLine(io.DisplaySize.x - panelW - 10);
        ImGui::TextDisabled("Готово");
    }

    ImGui::End();

    // === Отложенные операции (после закрытия окна — иначе ломаем итерацию) ===
    if (pendingCloseIdx >= 0) {
        CloseDoc(pendingCloseIdx);
        pendingCloseIdx = -1;
    }
    if (switchToIdx != -2) {
        if (switchToIdx >= 0) SwitchToDoc(switchToIdx);
        switchToIdx = -2;
    }
}

void Application::InitCommands() {
    myCommands.clear();

    auto add = [&](const std::string& id, const std::string& name,
                    const std::string& shortcut, std::function<void()> action) {
        myCommands.push_back({ id, name, shortcut, std::move(action) });
    };

    // Файл
    add("file.new",   "Создать",   "Ctrl+N",  [this]{ OnNewDocument(); });
    add("file.open",  "Открыть",   "Ctrl+O",  [this]{ OnOpen(); });
    add("file.save",  "Сохранить", "Ctrl+S",  [this]{ OnSave(); });

    // Правка
    add("edit.undo",  "Отменить",  "Ctrl+Z",  [this]{ OnUndo(); });
    add("edit.redo",  "Повторить", "Ctrl+Y",  [this]{ OnRedo(); });

    // Эскиз — инструменты
    add("sketch.line",   "Отрезок",         "L", [this]{
        myCurrentLineStyle = LS_Normal;
        myHasSketchStart = false;
        if (myOcctView) myOcctView->ClearPreviewLine();
    });
    add("sketch.rect",   "Прямоугольник",   "R", []{});
    add("sketch.circle", "Окружность",      "C", []{});
    add("sketch.arc",    "Дуга",            "A", []{});
    add("sketch.poly",   "Многоугольник",   "",  []{});
    add("sketch.finish", "Завершить эскиз", "Esc", [this]{ ExitSketch(); });

    // 3D
    add("model.extrude", "Выдавливание",   "E", []{});
    add("model.revolve", "Вращение",       "",  []{});
    add("model.hole",    "Отверстие",      "",  []{});

    // Настройки
    add("view.settings", "Настройки", "", [this]{ myShowSettingsDialog = true; });

    std::cout << "InitCommands: " << myCommands.size() << " commands loaded\n";
}

void Application::UpdateTelemetry() {
    // RAM через OCCT OSD_MemInfo — правильное имя: MemWorkingSet
    OSD_MemInfo memInfo;
    size_t ws = memInfo.Value(OSD_MemInfo::MemWorkingSet);
    if (ws != Standard_Size(-1)) {
        myLastRAMMB = (double)ws / (1024.0 * 1024.0);
    } else {
        myLastRAMMB = 0.0;
    }

    // Количество отображаемых объектов
    if (myOcctView) {
        myLastShapeCount = myOcctView->GetShapeCount();
    } else {
        myLastShapeCount = 0;
    }
}

void Application::DrawSettingsDialog() {
    if (!myShowSettingsDialog) return;

    ImGui::SetNextWindowSize(ImVec2(720, 480), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
                                    ImGui::GetIO().DisplaySize.y * 0.5f),
                             ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    if (ImGui::Begin("Настройки", &myShowSettingsDialog,
                      ImGuiWindowFlags_NoCollapse)) {

        // Левая панель — вкладки
        ImGui::BeginChild("##settings_tabs", ImVec2(180, 0), true);
        const char* tabs[] = {
            "Основные", "Графика", "Тема", "Язык", "Хоткеи", "О программе"
        };
        for (int i = 0; i < 6; i++) {
            if (ImGui::Selectable(tabs[i], myActiveSettingsTab == i)) {
                myActiveSettingsTab = i;
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Правая панель — контент
        ImGui::BeginChild("##settings_content", ImVec2(0, 0), true);

        switch (myActiveSettingsTab) {
            case 0: {
                ImGui::Text("Основные настройки");
                ImGui::Separator();
                ImGui::Spacing();

                static int units = 0;
                ImGui::Text("Единицы измерения:");
                ImGui::RadioButton("Миллиметры (мм)", &units, 0); ImGui::SameLine();
                ImGui::RadioButton("Дюймы (in)",      &units, 1);

                ImGui::Spacing();
                static int precision = 2;
                ImGui::SliderInt("Знаков после запятой", &precision, 0, 6);

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Автосохранение:");
                static bool autosave = true;
                ImGui::Checkbox("Включить автосохранение", &autosave);
                static int autosaveInterval = 5;
                ImGui::SliderInt("Интервал (мин)", &autosaveInterval, 1, 30);
                break;
            }
            case 1: {
                ImGui::Text("Графика");
                ImGui::Separator();
                ImGui::TextDisabled("(будет заполнено позже)");
                ImGui::Spacing();
                static int quality = 1;
                ImGui::Text("Качество рендеринга:");
                ImGui::RadioButton("Низкое",  &quality, 0); ImGui::SameLine();
                ImGui::RadioButton("Среднее", &quality, 1); ImGui::SameLine();
                ImGui::RadioButton("Высокое", &quality, 2);
                ImGui::Spacing();
                static bool aa = true;
                ImGui::Checkbox("Сглаживание (antialiasing)", &aa);
                static bool shadows = false;
                ImGui::Checkbox("Тени", &shadows);
                break;
            }
            case 2: {
                ImGui::Text("Тема оформления");
                ImGui::Separator();
                ImGui::Spacing();

                static int theme = myCurrentTheme;
                if (ImGui::RadioButton("Светлая", &theme, 0)) {
                    ApplyTheme(0);
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Тёмная", &theme, 1)) {
                    ApplyTheme(1);
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Как в системе", &theme, 2)) {
                    // Пока используем тёмную по умолчанию
                    ApplyTheme(2);
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("DeepSColor", &theme, 3)) {
                    ApplyTheme(3);
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Цвета линий эскиза:");

                static ImVec4 colNormal(0.75f, 0.2f, 1.0f, 1.0f);
                ImGui::ColorEdit3("Основная (недоопределённая)", (float*)&colNormal);

                static ImVec4 colConstruction(0.55f, 0.55f, 0.75f, 1.0f);
                ImGui::ColorEdit3("Вспомогательная", (float*)&colConstruction);

                static ImVec4 colCenterline(0.9f, 0.45f, 0.15f, 1.0f);
                ImGui::ColorEdit3("Осевая", (float*)&colCenterline);

                ImGui::Spacing();
                ImGui::TextDisabled("(применение цветов — позже)");
                break;
            }
            case 3: {
                ImGui::Text("Язык интерфейса");
                ImGui::Separator();
                Language cur = Localization::Instance().GetLanguage();
                if (ImGui::RadioButton("English", cur == Language::EN))
                    Localization::Instance().SetLanguage(Language::EN);
                if (ImGui::RadioButton("Русский", cur == Language::RU))
                    Localization::Instance().SetLanguage(Language::RU);
                break;
            }
            case 4: {
                ImGui::Text("Горячие клавиши");
                ImGui::Separator();
                ImGui::TextDisabled("(редактор хоткеев — позже)");
                ImGui::Spacing();
                ImGui::Text("Ctrl+N  — Новый проект");
                ImGui::Text("Ctrl+O  — Открыть");
                ImGui::Text("Ctrl+S  — Сохранить");
                ImGui::Text("Ctrl+Z  — Отменить");
                ImGui::Text("Ctrl+Y  — Повторить");
                ImGui::Text("F11     — Полный экран");
                ImGui::Text("F6      — Изометрия (домашний вид)");
                ImGui::Text("Esc     — Прервать линию / выйти из эскиза");
                break;
            }
            case 5: {
                ImGui::Text("О программе BreezeCAD");
                ImGui::Separator();
                ImGui::Text("Версия: 0.1.0 (prototype)");
                ImGui::Text("Сборка: %s %s", __DATE__, __TIME__);
                ImGui::Spacing();
                ImGui::TextWrapped(
                    "BreezeCAD — лёгкая кроссплатформенная САПР на базе "
                    "OpenCASCADE и Dear ImGui.");
                ImGui::Spacing();
                ImGui::Text("Ядро: OpenCASCADE 8.0");
                ImGui::Text("UI: Dear ImGui + GLFW");
                break;
            }
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

std::string Application::GetLabelName(const TDF_Label& label) const {
    Handle(TDataStd_Name) nameAttr;
    if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
        TCollection_AsciiString ascii(nameAttr->Get());
        return ascii.ToCString();
    }
    // Если имени нет — генерируем "Line <tag>"
    return "Line" + std::to_string(label.Tag());
}

void Application::OpenDependencyDialog(int type) {
    myDependencyDialogOpen = true;
    myCurrentDependencyType = type;
    myDependencyBase.clear();
    myDependencyDependent.clear();
    // Позиция сохранится (myDependencyDialogPosSet)
}

void Application::DrawDependencyDialog() {
    if (!myDependencyDialogOpen) return;

    // Названия зависимостей по индексу
    static const char* depNames[] = {
        "Совпадение", "Параллельность", "Перпендикулярность",
        "Горизонталь", "Вертикаль", "Касание",
        "Концентричность", "Равенство"
    };
    const char* title = depNames[myCurrentDependencyType % 8];

    // Позиция — по умолчанию по центру, но запоминается после первого перетаскивания
    ImGuiIO& io = ImGui::GetIO();
    if (!myDependencyDialogPosSet) {
        myDependencyDialogPos = ImVec2(io.DisplaySize.x * 0.5f - 150,
                                        io.DisplaySize.y * 0.5f - 150);
        myDependencyDialogPosSet = true;
    }

    // Первая установка позиции — только один раз
    ImGui::SetNextWindowPos(myDependencyDialogPos, ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_Once);

    std::string winTitle = std::string(title) + "##dep_dialog";
    ImGui::Begin(winTitle.c_str(), nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);

    // Запоминаем позицию — она сохранится до конца сеанса
    myDependencyDialogPos = ImGui::GetWindowPos();

    // === Базовый объект ===
    ImGui::Text("Базовый объект:");
    ImGui::BeginChild("##base_list", ImVec2(0, 80), true);
    if (myDependencyBase.empty()) {
        ImGui::TextDisabled("(кликните по линии в сцене)");
    } else {
        for (size_t i = 0; i < myDependencyBase.size(); i++) {
            std::string name = GetLabelName(myDependencyBase[i]);
            ImGui::PushID((int)i);
            ImGui::Text("%s", name.c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20);
            if (ImGui::SmallButton("x")) {
                myDependencyBase.erase(myDependencyBase.begin() + i);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();

    // === Зависимые объекты ===
    ImGui::Text("Зависимые объекты:");
    ImGui::BeginChild("##dep_list", ImVec2(0, 80), true);
    if (myDependencyDependent.empty()) {
        ImGui::TextDisabled("(кликните по линиям в сцене)");
    } else {
        for (size_t i = 0; i < myDependencyDependent.size(); i++) {
            std::string name = GetLabelName(myDependencyDependent[i]);
            ImGui::PushID((int)(1000 + i));
            ImGui::Text("%s", name.c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20);
            if (ImGui::SmallButton("x")) {
                myDependencyDependent.erase(myDependencyDependent.begin() + i);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::Separator();

    // === Кнопки ===
    float btnW = 95.0f;
    if (ImGui::Button("Отмена", ImVec2(btnW, 0))) {
        myDependencyDialogOpen = false;
        myDependencyBase.clear();
        myDependencyDependent.clear();
        if (myOcctView) myOcctView->ClearSelection();
    }
    ImGui::SameLine();
    if (ImGui::Button("Применить", ImVec2(btnW, 0))) {
        // TODO: сохранить зависимость в OCAF
        std::cout << "Dependency '" << title << "' applied with "
                  << myDependencyBase.size() << " base and "
                  << myDependencyDependent.size() << " dependent objects\n";
        // Очищаем поля, но окно оставляем открытым
        myDependencyBase.clear();
        myDependencyDependent.clear();
        if (myOcctView) myOcctView->ClearSelection();
    }
    ImGui::SameLine();
    if (ImGui::Button("Готово", ImVec2(btnW, 0))) {
        myDependencyDialogOpen = false;
        myDependencyBase.clear();
        myDependencyDependent.clear();
        if (myOcctView) myOcctView->ClearSelection();
    }

    ImGui::End();
}

void Application::Run() {
    bool prevF11 = false;
    bool prevDelete = false;
    bool prevCtrlS = false;
    bool prevCtrlO = false;

    while (!myWindow->ShouldClose()) {
      try {
        TRACE("iter: start");
        myWindow->PollEvents();

        // Перехват X / Alt+F4: если есть несохранённые — показать диалог
        if (glfwWindowShouldClose(myWindow->GetNativeWindow()) && !myShowUnsavedDialog) {
            int modIdx = -1;
            for (int i = 0; i < (int)myDocuments.size(); i++) {
                if (myDocuments[i]->IsModified()) { modIdx = i; break; }
            }
            if (modIdx >= 0) {
                glfwSetWindowShouldClose(myWindow->GetNativeWindow(), GLFW_FALSE);
                myPendingWindowClose = true;
                myPendingCloseDocIndex = modIdx;
                std::snprintf(myUnsavedDialogInfo, sizeof(myUnsavedDialogInfo),
                    "Проект «%s» содержит "
                    "несохранённые изменения.\n"
                    "Сохранить перед выходом?",
                    myProjectNames[modIdx].c_str());
                myShowUnsavedDialog = true;
            }
        }

        bool curF11 = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_F11) == GLFW_PRESS;
        if (curF11 && !prevF11) myWindow->ToggleFullscreen();
        prevF11 = curF11;

        // === Polling ПКМ через GLFW напрямую (ImGui колбэки перезаписаны нашими) ===
        if (!myDependencyDialogOpen && !myShowRenameDialog &&
            !myShowDeleteConfirm && !myShowPropertiesDialog) {
            ImGuiIO& io = ImGui::GetIO();

            // Мышь в области 3D-сцены?
            bool inScene = io.MousePos.x > myTreeWidth + 10
                        && io.MousePos.y > 165
                        && io.MousePos.x < io.DisplaySize.x - 320
                        && io.MousePos.y < io.DisplaySize.y - 70;

            static bool  prevRMB = false;
            static ImVec2 rmbStartPos(0, 0);

            bool curRMB = glfwGetMouseButton(myWindow->GetNativeWindow(),
                                              GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

            if (curRMB && !prevRMB && inScene) {
                rmbStartPos = io.MousePos;
            }
            if (!curRMB && prevRMB && inScene) {
                float dx = io.MousePos.x - rmbStartPos.x;
                float dy = io.MousePos.y - rmbStartPos.y;
                if (dx*dx + dy*dy < 25.0f) {
                    // ПКМ-клик без движения → меню
                    myPendingContextMenu = true;
                    myContextMenuPos = rmbStartPos;

                    // Выделяем объект под курсором
                    if (myOcctView) {
                        int winW = 1, winH = 1, fbW = 1, fbH = 1;
                        glfwGetWindowSize(myWindow->GetNativeWindow(), &winW, &winH);
                        glfwGetFramebufferSize(myWindow->GetNativeWindow(), &fbW, &fbH);
                        int px = (int)(rmbStartPos.x * fbW / winW);
                        int py = (int)(rmbStartPos.y * fbH / winH);
                        myOcctView->SelectAtPixel(px, py);
                    }
                }
                // Если было движение — вращение уже отработало в OcctView, ничего не делаем
            }
            prevRMB = curRMB;
        }

        // В режиме эскиза: всегда показываем маркер под курсором,
        // а если есть старт — ещё и линию-превью
        if (!myActiveSketchLabel.IsNull() && myOcctView) {
            if (!ImGui::GetIO().WantCaptureMouse) {
                static double lastPreviewX = -1e9, lastPreviewY = -1e9;

                double mx, my;
                glfwGetCursorPos(myWindow->GetNativeWindow(), &mx, &my);

                double dxp = mx - lastPreviewX;
                double dyp = my - lastPreviewY;
                if (dxp*dxp + dyp*dyp > 9.0) {  // сдвиг > 3 пикселей
                    lastPreviewX = mx;
                    lastPreviewY = my;

                    int winW = 1, winH = 1, fbW = 1, fbH = 1;
                    glfwGetWindowSize(myWindow->GetNativeWindow(), &winW, &winH);
                    glfwGetFramebufferSize(myWindow->GetNativeWindow(), &fbW, &fbH);
                    int px = (int)(mx * fbW / winW);
                    int py = (int)(my * fbH / winH);

                    gp_Pnt cur;
                    if (myOcctView->ScreenToPlane(px, py, cur)) {
                        bool shiftHeld = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
                                      || glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

                        if (myHasSketchStart && shiftHeld) {
                            cur = ApplyOrtho(myOcctView->GetSketchPlane(), mySketchStartPoint, cur);
                        } else {
                            bool curSnapped = false;
                            if (mySnapToPoints) {
                                gp_Pnt snapP;
                                if (SnapToKnownPoint(cur, myActiveSketchLabel,
                                                      myDocument->GetDoc(),
                                                      3.0, snapP)) {
                                    cur = snapP;
                                    curSnapped = true;
                                }
                            }
                            if (!curSnapped && mySnapToGrid) {
                                cur = SnapToGridPoint(cur, myOcctView->GetSketchPlane());
                            }
                        }

                        if (myHasSketchStart) {
                            myOcctView->ShowPreviewLine(mySketchStartPoint, cur);

                            // Считаем длину и угол для отображения
                            ComputeLengthAngle(myOcctView->GetSketchPlane(),
                                                mySketchStartPoint, cur,
                                                myPreviewLength, myPreviewAngle);
                            myHasPreview = true;
                        } else {
                            // Проверяем: подсветить зелёным, если точка попадает
                            // в существующую вершину (снап сработает при клике)
                            bool willSnap = IsPointOnExistingVertex(cur,
                                              myActiveSketchLabel,
                                              gp_Pnt(1e9, 1e9, 1e9),  // skip невозможно
                                              3.0);
                            myOcctView->ShowCursorMarkerHighlighted(cur, willSnap);
                            myHasPreview = false;
                        }
                    }
                }
            }

            static bool prevLMB = false;
            bool curLMB = glfwGetMouseButton(myWindow->GetNativeWindow(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            if (curLMB && !prevLMB) {
                // Получаем позицию курсора в координатах framebuffer
                double mx, my;
                glfwGetCursorPos(myWindow->GetNativeWindow(), &mx, &my);
                int winW = 1, winH = 1, fbW = 1, fbH = 1;
                glfwGetWindowSize(myWindow->GetNativeWindow(), &winW, &winH);
                glfwGetFramebufferSize(myWindow->GetNativeWindow(), &fbW, &fbH);
                int px = (int)(mx * fbW / winW);
                int py = (int)(my * fbH / winH);
                // Не обрабатывать клики по панелям ImGui
                if (!ImGui::GetIO().WantCaptureMouse) {
                    OnSketchClick(px, py);
                }
            }
            prevLMB = curLMB;
        }

        bool curDelete = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_DELETE) == GLFW_PRESS;
        if (curDelete && !prevDelete && myOcctView && myDocument) {
            TDF_Label sel = myOcctView->GetSelectedLabel();
            if (!sel.IsNull() && !BreezeDocument::IsSystem(sel)) {
                // Через диалог подтверждения
                RequestDelete(sel);
                myOcctView->ClearSelection();
            }
        }
        prevDelete = curDelete;

        bool ctrl = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS
                 || glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
        bool curCtrlS = ctrl && glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_S) == GLFW_PRESS;
        if (curCtrlS && !prevCtrlS) OnSave();
        prevCtrlS = curCtrlS;

        bool curCtrlO = ctrl && glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_O) == GLFW_PRESS;
        if (curCtrlO && !prevCtrlO) OnOpen();
        prevCtrlO = curCtrlO;

        // Ctrl+C — копировать выделенное
        {
            bool ctrl = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS
                     || glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
            static bool prevCtrlC = false;
            bool curCtrlC = ctrl && glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_C) == GLFW_PRESS;
            if (curCtrlC && !prevCtrlC && myOcctView) {
                TDF_Label sel = myOcctView->GetSelectedLabel();
                if (!sel.IsNull() && !BreezeDocument::IsSystem(sel)) {
                    myClipboardLabel = sel;
                    std::cout << "Copied: tag=" << sel.Tag() << "\n";
                }
            }
            prevCtrlC = curCtrlC;

            // Ctrl+V — вставить
            static bool prevCtrlV = false;
            bool curCtrlV = ctrl && glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_V) == GLFW_PRESS;
            if (curCtrlV && !prevCtrlV && !myClipboardLabel.IsNull()) {
                // TODO: клонирование
                std::cout << "Paste — TODO\n";
            }
            prevCtrlV = curCtrlV;
        }

        // Ctrl+C — копировать выделенное
        {
            bool ctrl = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS
                     || glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
            static bool prevCtrlC = false;
            bool curCtrlC = ctrl && glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_C) == GLFW_PRESS;
            if (curCtrlC && !prevCtrlC && myOcctView) {
                TDF_Label sel = myOcctView->GetSelectedLabel();
                if (!sel.IsNull() && !BreezeDocument::IsSystem(sel)) {
                    myClipboardLabel = sel;
                    std::cout << "Copied: tag=" << sel.Tag() << "\n";
                }
            }
            prevCtrlC = curCtrlC;

            static bool prevCtrlV = false;
            bool curCtrlV = ctrl && glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_V) == GLFW_PRESS;
            if (curCtrlV && !prevCtrlV && !myClipboardLabel.IsNull()) {
                std::cout << "Paste — TODO\n";
            }
            prevCtrlV = curCtrlV;
        }

        // Alt+V — переключить видимость выделенного объекта
        {
            bool altHeld = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_LEFT_ALT) == GLFW_PRESS
                        || glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
            static bool prevAltV = false;
            bool curAltV = altHeld && glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_V) == GLFW_PRESS;
            if (curAltV && !prevAltV && myOcctView && myDocument) {
                TDF_Label sel = myOcctView->GetSelectedLabel();
                if (!sel.IsNull()) {
                    std::string key = LabelKey(sel);
                    auto it = myVisibility.find(key);
                    bool curVis = (it != myVisibility.end()) ? it->second : true;
                    myVisibility[key] = !curVis;
                    myOcctView->SetShapeVisible(sel, !curVis);
                    std::cout << "Alt+V: visibility toggled for tag=" << sel.Tag()
                              << " -> " << (!curVis ? "visible" : "hidden") << "\n";
                }
            }
            prevAltV = curAltV;
        }

        // F6 — Home view + сброс изоляции
        static bool prevF6 = false;
        bool curF6 = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_F6) == GLFW_PRESS;
        if (curF6 && !prevF6 && myOcctView) {
            myOcctView->SetHomeView();
            if (myIsolationActive) {
                ShowAllRespectingDefaults();
            }
        }
        prevF6 = curF6;

        // Ctrl+Z — Undo
        static bool prevCtrlZ = false;
        bool curCtrlZ = ctrl && glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_Z) == GLFW_PRESS;
        if (curCtrlZ && !prevCtrlZ) OnUndo();
        prevCtrlZ = curCtrlZ;

        // Ctrl+Y — Redo
        static bool prevCtrlY = false;
        bool curCtrlY = ctrl && glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_Y) == GLFW_PRESS;
        if (curCtrlY && !prevCtrlY) OnRedo();
        prevCtrlY = curCtrlY;

        // Esc — сначала прерывает текущую линию, потом выходит из эскиза
        static bool prevEsc = false;
        bool curEsc = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_ESCAPE) == GLFW_PRESS;
        if (curEsc && !prevEsc && !myActiveSketchLabel.IsNull()) {
            if (myHasSketchStart) {
                // Прервать текущую цепочку линий
                myHasSketchStart = false;
                if (myOcctView) myOcctView->ClearPreviewLine();
                myHasPreview = false;
                std::cout << "Sketch: line chain cancelled\n";
            } else {
                // Нечего прерывать — выходим из эскиза
                ExitSketch();
            }
        }
        prevEsc = curEsc;

        glfwMakeContextCurrent(myWindow->GetNativeWindow());
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Запоминаем позицию мыши для превью
        {
            double mx, my;
            glfwGetCursorPos(myWindow->GetNativeWindow(), &mx, &my);
            myMouseX = mx;
            myMouseY = my;
        }

        if (!myShowHomeScreen) {
            DrawRibbon();
            DrawDocumentTree();
            DrawTreeSplitter();
            DrawAddBoxDialog();
            DrawPreviewInfo();
            DrawProjectTabs();
            DrawStatusBar();
            DrawViewCube();
            DrawDependencyDialog();
            DrawRenameDialog();
            DrawDeleteConfirmDialog();
            DrawUnsavedChangesDialog();
            DrawPropertiesDialog();
            DrawContextMenu3D();
            DrawSelectionRect();
            DrawRightPanel();
        }
        DrawHomeScreen();
        DrawSettingsDialog();
        TRACE("iter: before UpdateTelemetry");
        UpdateTelemetry();
        TRACE("iter: after UpdateTelemetry");

        ImGui::Render();

        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(myWindow->GetNativeWindow(), &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (myOcctView) myOcctView->Redraw();

        glViewport(0, 0, fbW, fbH);
        glfwMakeContextCurrent(myWindow->GetNativeWindow());
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        TRACE("iter: before SwapBuffers");
        myWindow->SwapBuffers();
        TRACE("iter: after SwapBuffers");
      } catch (const std::exception& e) {
        std::cerr << "Runtime exception in main loop: " << e.what() << "\n";
      } catch (...) {
        std::cerr << "Unknown exception in main loop (ignored)\n";
      }
    }
}

void Application::DrawSelectionRect() {
    if (!myOcctView) return;
    if (!myOcctView->IsSelectionRectActive()) return;
    if (ImGui::GetIO().WantCaptureMouse) return;

    int x1, y1, x2, y2;
    myOcctView->GetSelectionRect(x1, y1, x2, y2);

    // Координаты в framebuffer'е — переводим в логические (ImGui)
    int winW = 1, winH = 1, fbW = 1, fbH = 1;
    glfwGetWindowSize(myWindow->GetNativeWindow(), &winW, &winH);
    glfwGetFramebufferSize(myWindow->GetNativeWindow(), &fbW, &fbH);

    float sx = (fbW > 0) ? (float)winW / fbW : 1.0f;
    float sy = (fbH > 0) ? (float)winH / fbH : 1.0f;

    ImVec2 p1(x1 * sx, y1 * sy);
    ImVec2 p2(x2 * sx, y2 * sy);

    ImVec2 tl(std::min(p1.x, p2.x), std::min(p1.y, p2.y));
    ImVec2 br(std::max(p1.x, p2.x), std::max(p1.y, p2.y));

    ImDrawList* draw = ImGui::GetForegroundDrawList();

    draw->AddRectFilled(tl, br, IM_COL32(80, 130, 180, 60));
    draw->AddRect(tl, br, IM_COL32(80, 130, 180, 255), 0.0f, 0, 1.5f);
}

void Application::DrawRightPanel() {
    ImGuiIO& io = ImGui::GetIO();

    const float toolW    = 40.0f;
    const float toolH    = 32.0f;
    const float arrowH   = 14.0f;
    const float handleW  = 26.0f;   // заметный
    const float pad      = 2.0f;
    const float panelW   = toolW + pad * 2.0f;
    const float topY     = 385.0f;
    const float bottomY  = io.DisplaySize.y - 56.0f;
    const float panelH   = bottomY - topY;
    const float rightEdge = io.DisplaySize.x;

    // === Хлястик (стрелка-таб) слева от панели ===
    const float edgeMargin = 14.0f;  // отступ от правого края
    float handleX = myRightPanelExpanded
                    ? (rightEdge - panelW - handleW - edgeMargin)
                    : (rightEdge - handleW - edgeMargin);
    float handleH = 84.0f;
    float handleY = topY + (panelH - handleH) * 0.5f;

    ImGui::SetNextWindowPos(ImVec2(handleX, handleY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(handleW, handleH), ImGuiCond_Always);
    ImGui::Begin("##right_handle", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Кастомный хлястик — рисуем через GetWindowPos (надёжнее)
    ImVec2 wpos = ImGui::GetWindowPos();
    ImVec2 hMin = wpos;
    ImVec2 hMax = ImVec2(wpos.x + handleW, wpos.y + handleH);

    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::InvisibleButton("##right_handle_btn", ImVec2(handleW, handleH));
    bool hClicked = ImGui::IsItemClicked();
    bool hHovered = ImGui::IsItemHovered();

    ImDrawList* hdl = ImGui::GetWindowDrawList();

    // Фон хлястика — отчётливо видимый
    ImU32 hBg;
    bool isLight = (ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).x > 0.5f);
    if (hHovered) {
        hBg = IM_COL32(60, 120, 200, 255);
    } else if (isLight) {
        hBg = IM_COL32(140, 130, 110, 240);   // тёмно-бежевый в светлой
    } else {
        hBg = IM_COL32(90, 100, 120, 240);    // серо-синий в тёмной
    }
    hdl->AddRectFilled(hMin, hMax, hBg, 6.0f);

    // Иконка шеврона
    const char* hIcon = myRightPanelExpanded ? "chevron-right" : "chevron-left";
    GLuint hTex = myIcons.Get(hIcon, 20);
    if (hTex != 0) {
        ImVec2 ip(hMin.x + (handleW - 20) * 0.5f,
                  hMin.y + (handleH - 20) * 0.5f);
        hdl->AddImage((ImTextureID)(intptr_t)hTex, ip,
                      ImVec2(ip.x + 20, ip.y + 20),
                      ImVec2(0, 0), ImVec2(1, 1),
                      IM_COL32(255, 255, 255, 255));
    }

    if (hClicked) {
        myRightPanelExpanded = !myRightPanelExpanded;
    }
    if (hHovered) {
        ImGui::SetTooltip(myRightPanelExpanded ? "Свернуть панель" : "Развернуть панель");
    }
    ImGui::End();

    if (!myRightPanelExpanded) return;

    // === Тело панели ===
    float panelX = rightEdge - panelW - edgeMargin;
    ImGui::SetNextWindowPos(ImVec2(panelX, topY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_Always);
    ImGui::Begin("##right_panel", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));

    // --- 1. Панорамировать ---
    if (SvgIconBtn(myIcons, "panel_pan", "hand", 20, toolW, toolH, "Панорамировать")) {
        // TODO: активировать режим панорамирования
    }
    DrawAdvancedTooltip("panel.pan", "Панорамировать",
        "Сдвинуть вид мышью",
        "Активирует инструмент панорамирования. После активации зажмите ЛКМ "
        "в 3D-сцене и перемещайте мышь, чтобы сдвинуть вид.");

    ImGui::Separator();

    // --- 2. Показать рамкой (с выпадающим меню) ---
    if (SvgIconBtn(myIcons, "panel_zoom", "search", 20, toolW, toolH, "Показать рамкой")) {
        // TODO: активировать режим "показать рамкой"
    }
    DrawAdvancedTooltip("panel.zoom", "Показать рамкой",
        "Зуммирование выделенной области",
        "Основное действие — приблизить область, выделенную рамкой. "
        "Стрелка вниз открывает дополнительные режимы зуммирования.");

    if (ImGui::Button(ICON_CHEVRON_DOWN "##zoom_dd", ImVec2(toolW, arrowH))) {
        ImGui::OpenPopup("##zoom_menu");
    }
    if (ImGui::BeginPopup("##zoom_menu")) {
        if (ImGui::MenuItem("Зуммировать")) {}
        if (ImGui::MenuItem("Показать всё")) {
            if (myOcctView) {
                if (myIsolationActive) {
                    ShowAllRespectingDefaults();
                }
                myOcctView->FitAll();
            }
        }
        if (ImGui::MenuItem("Показать рамкой")) {}
        if (ImGui::MenuItem("Показать выбранное")) {}
        ImGui::EndPopup();
    }

    ImGui::Separator();

    // --- 3. Свободная орбита (с выпадающим меню) ---
    if (SvgIconBtn(myIcons, "panel_orbit", "rotate-cw", 20, toolW, toolH, "Орбита")) {
        // TODO: активировать орбиту
    }
    DrawAdvancedTooltip("panel.orbit", "Свободная орбита",
        "Вращение модели вокруг центра",
        "Основное действие — свободное вращение вида. Стрелка вниз открывает "
        "различные режимы вращения (с зависимостями, поворот под углом).");

    if (ImGui::Button(ICON_CHEVRON_DOWN "##orbit_dd", ImVec2(toolW, arrowH))) {
        ImGui::OpenPopup("##orbit_menu");
    }
    if (ImGui::BeginPopup("##orbit_menu")) {
        if (ImGui::MenuItem("Орбита")) {}
        if (ImGui::MenuItem("Орбита с зависимостями")) {}
        if (ImGui::MenuItem("Повернуть под углом")) {}
        ImGui::EndPopup();
    }

    ImGui::Separator();

    // --- 4. Вид на объект ---
    if (SvgIconBtn(myIcons, "panel_viewon", "eye", 20, toolW, toolH, "Вид на объект")) {
        // TODO: активировать режим "вид на грань"
    }
    DrawAdvancedTooltip("panel.viewon", "Вид на объект",
        "Ориентировать вид по выбранной грани",
        "После активации кликните по любой грани модели — камера развернётся "
        "так, чтобы вы смотрели на эту грань перпендикулярно.");

    ImGui::Separator();

    // --- 5. Стили отображения (с выпадающим меню) ---
    if (SvgIconBtn(myIcons, "panel_style", "palette", 20, toolW, toolH, "Стиль отображения")) {
        // TODO: циклить стиль
    }
    DrawAdvancedTooltip("panel.style", "Стили отображения",
        "Тонированный, каркасный и другие режимы",
        "Основное действие — переключение стиля отображения модели. "
        "Стрелка вниз открывает полный список стилей.");

    if (ImGui::Button(ICON_CHEVRON_DOWN "##style_dd", ImVec2(toolW, arrowH))) {
        ImGui::OpenPopup("##style_menu");
    }
    if (ImGui::BeginPopup("##style_menu")) {
        if (ImGui::MenuItem("Тонированный с рёбрами")) {
            if (myOcctView) myOcctView->SetDisplayModeAll(1, true);   // AIS_Shaded = 1
        }
        if (ImGui::MenuItem("Тонированный")) {
            if (myOcctView) myOcctView->SetDisplayModeAll(1, false);
        }
        if (ImGui::MenuItem("Каркасный")) {
            if (myOcctView) myOcctView->SetDisplayModeAll(0, false);  // AIS_WireFrame = 0
        }
        ImGui::Separator();
        ImGui::MenuItem("Реалистичный (скоро)", nullptr, false, false);
        ImGui::MenuItem("Скрытые линии (скоро)", nullptr, false, false);
        ImGui::MenuItem("Прозрачный (скоро)", nullptr, false, false);
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);
    ImGui::End();
}

void Application::DrawHomeScreen() {
    if (!myShowHomeScreen) return;

    ImGuiIO& io = ImGui::GetIO();

    // === Фоновое затемнение 3D-сцены ===
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(20, 22, 28, 240));
    ImGui::Begin("##home_bg", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // === Заголовок ===
    ImGui::SetCursorPos(ImVec2(40, 40));
    ImGui::TextColored(ImVec4(0.9f, 0.95f, 1.0f, 1.0f), "BreezeCAD");
    ImGui::SetCursorPos(ImVec2(40, 70));
    ImGui::TextDisabled("Проекты");

    // === Кнопки управления (сверху справа) ===
    // Общая ширина: 180 + 10 + 180 + 10 + 50 = 430. Отступ от правого края — 40.
    const float totalBtnW = 430.0f;
    float btnX = io.DisplaySize.x - totalBtnW - 40.0f;
    if (btnX < 40.0f) btnX = 40.0f;  // защита от узкого окна

    ImGui::SetCursorPos(ImVec2(btnX, 40));

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20, 12));

    if (ImGui::Button(ICON_PLUS "  Создать", ImVec2(180, 0))) {
        CreateNewDoc();
        myShowHomeScreen = false;
        RefreshView();
        if (myOcctView) myOcctView->FitAll();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FILE "  Открыть", ImVec2(180, 0))) {
        OnOpen();
        if (myActiveDocIndex >= 0) myShowHomeScreen = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_SETTINGS, ImVec2(50, 0))) {
        myShowSettingsDialog = true;
    }

    ImGui::PopStyleVar();

    // === Разделитель ===
    ImGui::SetCursorPos(ImVec2(40, 120));
    ImGui::Separator();
    ImGui::SetCursorPos(ImVec2(40, 140));

    // === Плитки проектов ===
    const float tileW = 240.0f;
    const float tileH = 220.0f;
    const float spacing = 20.0f;
    int col = 0;
    float x = 40.0f;
    float y = 180.0f;

    ImGui::BeginChild("##home_tiles", ImVec2(0, 0), false);

    for (int i = 0; i < (int)myDocuments.size(); i++) {
        ImGui::PushID(i);

        ImVec2 pos(x + col * (tileW + spacing), y);

        // Плитка — это группа кнопок, стилизованная под карточку
        ImGui::SetCursorPos(pos);

        // Фон плитки
        ImGui::BeginGroup();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(35, 40, 50, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(60, 70, 90, 255));

        ImGui::BeginChild(("##tile_" + std::to_string(i)).c_str(),
                           ImVec2(tileW, tileH), true,
                           ImGuiWindowFlags_NoScrollbar);

        // Миниатюра: пробуем загрузить PNG превью
        ImVec2 thumbPos = ImGui::GetCursorScreenPos();
        ImVec2 thumbSize(tileW - 20, 140);
        bool thumbLoaded = false;

        std::string thumbPath = GetThumbnailPath(i);
        if (!thumbPath.empty() && std::filesystem::exists(thumbPath)) {
            // Кешированные текстуры: ключ — путь, значение — GLuint
            static std::map<std::string, ImTextureID> s_thumbs;
            static std::map<std::string, std::time_t> s_thumbTimes;

            // Проверяем, надо ли перезагрузить (файл новее кеша)
            std::error_code ec;
            auto writeTime = std::filesystem::last_write_time(thumbPath, ec);
            std::time_t wt = 0;
            if (!ec) {
                wt = std::chrono::system_clock::to_time_t(
                    std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                        writeTime - std::filesystem::file_time_type::clock::now()
                        + std::chrono::system_clock::now()));
            }

            auto cacheIt = s_thumbs.find(thumbPath);
            bool needLoad = (cacheIt == s_thumbs.end()) ||
                            (s_thumbTimes[thumbPath] != wt);

            if (needLoad) {
                // Загружаем PNG через stb_image или ImGui
                // Простой путь: используем ImGui::GetIO().Fonts->... — нет, надо самим.
                // Используем stb_image из imgui (extern в глобальном namespace)
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
                    s_thumbTimes[thumbPath] = wt;
                    cacheIt = s_thumbs.find(thumbPath);
                }
            }

            if (cacheIt != s_thumbs.end()) {
                ImGui::GetWindowDrawList()->AddImage(
                    cacheIt->second,
                    thumbPos,
                    ImVec2(thumbPos.x + thumbSize.x, thumbPos.y + thumbSize.y));
                thumbLoaded = true;
            }
        }

        if (!thumbLoaded) {
            // Заглушка — тёмный прямоугольник с иконкой куба
            ImGui::GetWindowDrawList()->AddRectFilled(
                thumbPos,
                ImVec2(thumbPos.x + thumbSize.x, thumbPos.y + thumbSize.y),
                IM_COL32(25, 28, 35, 255), 6.0f);

            ImGui::SetCursorPos(ImVec2(tileW * 0.5f - 15, 60));
            ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.7f, 0.6f), ICON_CUBE);
        }

        ImGui::SetCursorPosY(150);

        // Имя проекта
        ImGui::Text("%s", myProjectNames[i].c_str());

        // Путь (или «не сохранён»)
        ImGui::TextDisabled("%s", myProjectPaths[i].empty()
                                     ? "<не сохранён>"
                                     : myProjectPaths[i].c_str());

        // Кнопки на плитке
        ImGui::SetCursorPosY(180);
        if (ImGui::Button("Открыть", ImVec2(100, 30))) {
            SwitchToDoc(i);
            myShowHomeScreen = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_CLOSE " Удалить", ImVec2(100, 30))) {
            // TODO: подтверждение + удаление
        }

        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
        ImGui::EndGroup();

        ImGui::PopID();

        col++;
        if (col >= 4) {  // 4 плитки в строке
            col = 0;
            y += tileH + spacing;
        }
    }

    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleColor();
}

} // namespace BreezeCAD
