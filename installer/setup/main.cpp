// BreezeCAD Setup: the installer, the updater ("Change" in Apps & features)
// and the uninstaller in one small .exe.
//
//   BreezeCAD-Setup.exe                       the pages (install or update)
//   uninstall.exe --uninstall [--quiet]       removal (copies itself to %TEMP% first)
//   … --unpack <zip> <dir> <version>          the elevated helper for folders
//                                              that need administrator rights
//
// Window: Win32 + Direct3D 11 + Dear ImGui (no OpenGL: virtual machines and
// old drivers often lack it, and the installer must run everywhere the
// program will). The look follows the mockup approved on 31.10.2026.

#include "Install.h"
#include "Payload.h"
#include "Strings.h"
#include "../thumbnails/Badge.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <miniz.h>
#include <stb_image.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

#ifndef BZ_SETUP_VERSION
#define BZ_SETUP_VERSION "0"
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace fs = std::filesystem;
using namespace bz::setup;

namespace {

// --- look ---------------------------------------------------------------------

struct Palette {
    ImU32 bg, panel, rail, line, text, muted, accent, accentHover, accentSoft, accentBorder, ok, okSoft, bad, badSoft, scrim;
};
Palette Light() {
    return {IM_COL32(0xE9, 0xEC, 0xF1, 255), IM_COL32(255, 255, 255, 255), IM_COL32(0xF4, 0xF6, 0xF9, 255), IM_COL32(0xDC, 0xE1, 0xE8, 255),
            IM_COL32(0x1B, 0x1F, 0x24, 255), IM_COL32(0x5A, 0x62, 0x6E, 255), IM_COL32(0x1D, 0x4E, 0xD8, 255), IM_COL32(0x1A, 0x3F, 0xB0, 255),
            IM_COL32(0xE6, 0xEE, 0xFB, 255), IM_COL32(0xBF, 0xD3, 0xF5, 255), IM_COL32(0x15, 0x80, 0x3D, 255), IM_COL32(0xE7, 0xF5, 0xEC, 255),
            IM_COL32(0xB4, 0x23, 0x18, 255), IM_COL32(0xFD, 0xEC, 0xEA, 255), IM_COL32(10, 16, 28, 96)};
}
Palette Dark() {
    return {IM_COL32(0x10, 0x12, 0x15, 255), IM_COL32(0x1B, 0x1E, 0x22, 255), IM_COL32(0x16, 0x19, 0x1C, 255), IM_COL32(0x2C, 0x31, 0x38, 255),
            IM_COL32(0xE6, 0xE8, 0xEB, 255), IM_COL32(0x9A, 0xA1, 0xAB, 255), IM_COL32(0x3F, 0x6F, 0xEA, 255), IM_COL32(0x4F, 0x7D, 0xF5, 255),
            IM_COL32(0x1F, 0x2A, 0x40, 255), IM_COL32(0x3A, 0x5B, 0xA0, 255), IM_COL32(0x4A, 0xDE, 0x80, 255), IM_COL32(0x16, 0x32, 0x22, 255),
            IM_COL32(0xF8, 0x71, 0x71, 255), IM_COL32(0x3A, 0x1B, 0x1B, 255), IM_COL32(0, 0, 0, 140)};
}
bool SystemIsDark() {
    DWORD light = 1, size = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
                 RRF_RT_REG_DWORD, nullptr, &light, &size);
    return light == 0;
}

Palette P = Light();
float   gScale = 1.0f;
float   S(float v) { return std::round(v * gScale); }
ImVec4  V4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

struct Fonts {
    ImFont *body = nullptr, *medium = nullptr, *small = nullptr, *h2 = nullptr, *hero = nullptr, *slide = nullptr, *eyebrow = nullptr,
           *mono = nullptr;
} F;

// --- graphics -----------------------------------------------------------------

ID3D11Device*           gDevice = nullptr;
ID3D11DeviceContext*    gContext = nullptr;
IDXGISwapChain*         gSwap = nullptr;
ID3D11RenderTargetView* gTarget = nullptr;
HWND                    gWindow = nullptr;
bool                    gCloseAsked = false;
bool                    gFontsDirty = true;

void MakeTarget() {
    ID3D11Texture2D* back = nullptr;
    gSwap->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        gDevice->CreateRenderTargetView(back, nullptr, &gTarget);
        back->Release();
    }
}
void DropTarget() {
    if (gTarget) gTarget->Release();
    gTarget = nullptr;
}

bool MakeDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got{};
    // The graphics card first; WARP (Windows' own software renderer) when there is none.
    for (D3D_DRIVER_TYPE type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP})
        if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, type, nullptr, 0, levels, 3, D3D11_SDK_VERSION, &sd, &gSwap, &gDevice, &got,
                                                    &gContext))) {
            MakeTarget();
            return true;
        }
    return false;
}

struct Texture {
    ID3D11ShaderResourceView* view = nullptr;
    int                       w = 0, h = 0;
    explicit operator bool() const { return view != nullptr; }
    ImTextureID id() const { return reinterpret_cast<ImTextureID>(view); }
};

Texture MakeTexture(const std::uint8_t* rgba, int w, int h) {
    Texture t;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(w);
    desc.Height = static_cast<UINT>(h);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{rgba, static_cast<UINT>(w * 4), 0};
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(gDevice->CreateTexture2D(&desc, &data, &tex))) return t;
    gDevice->CreateShaderResourceView(tex, nullptr, &t.view);
    tex->Release();
    t.w = w;
    t.h = h;
    return t;
}
void Drop(Texture& t) {
    if (t.view) t.view->Release();
    t = {};
}

/// The BreezeCAD mark `height` pixels high in `colour` (tested point by
/// point like the previews' badge: no SVG renderer in the installer).
Texture MarkTexture(int height, ImU32 colour) {
    const int w = static_cast<int>(std::ceil(height * 91.0 / 108.0));
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * height * 4);
    constexpr int n = 4;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < w; ++x) {
            int hits = 0;
            for (int j = 0; j < n; ++j)
                for (int i = 0; i < n; ++i)
                    hits += bz::thumbs::InMark({(x + (i + 0.5) / n) / height * 108.0 + 14, (y + (j + 0.5) / n) / height * 108.0 + 6});
            std::uint8_t* p = &rgba[(static_cast<std::size_t>(y) * w + x) * 4];
            p[0] = colour & 0xFF;
            p[1] = (colour >> 8) & 0xFF;
            p[2] = (colour >> 16) & 0xFF;
            p[3] = static_cast<std::uint8_t>(255 * hits / (n * n));
        }
    return MakeTexture(rgba.data(), w, height);
}

Texture JpegTexture(const wchar_t* resource) {
    const auto bytes = Resource(resource);
    int w = 0, h = 0, c = 0;
    stbi_uc* rgba = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &c, 4);
    if (!rgba) return {};
    Texture t = MakeTexture(rgba, w, h);
    stbi_image_free(rgba);
    return t;
}

// --- fonts --------------------------------------------------------------------

std::vector<std::uint8_t> ReadFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

/// Windows' own font for a script the IBM Plex faces lack (CJK), first found.
std::optional<fs::path> SystemFont(std::initializer_list<const wchar_t*> names) {
    wchar_t dir[MAX_PATH] = {};
    GetWindowsDirectoryW(dir, MAX_PATH);
    for (const wchar_t* n : names) {
        const fs::path p = fs::path(dir) / L"Fonts" / n;
        std::error_code ec;
        if (fs::exists(p, ec)) return p;
    }
    return std::nullopt;
}

void BuildFonts() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    static const ImWchar latin[] = {0x0020, 0x00FF, 0x0100, 0x017F, 0x0180, 0x024F, 0x0300, 0x036F, 0x0400, 0x052F,
                                    0x1E00, 0x1EFF, 0x2010, 0x205E, 0x20AC, 0x20BD, 0x2190, 0x21FF, 0x2212, 0x2212, 0};
    const std::string& lang = CurrentLanguage();
    // The names of the CJK languages in the list need their glyphs whatever the language is.
    static ImVector<ImWchar> names;
    if (names.empty()) {
        ImFontGlyphRangesBuilder b;
        for (const LanguageInfo& l : Languages()) b.AddText(l.name);
        b.BuildRanges(&names);
    }
    const ImWchar* cjk = nullptr;
    std::optional<fs::path> cjkFile;
    if (lang == "zh") cjk = io.Fonts->GetGlyphRangesChineseSimplifiedCommon(), cjkFile = SystemFont({L"msyh.ttc", L"simsun.ttc"});
    if (lang == "ja") cjk = io.Fonts->GetGlyphRangesJapanese(), cjkFile = SystemFont({L"YuGothM.ttc", L"meiryo.ttc", L"msgothic.ttc"});
    if (lang == "ko") cjk = io.Fonts->GetGlyphRangesKorean(), cjkFile = SystemFont({L"malgun.ttf", L"gulim.ttc"});
    static std::vector<std::uint8_t> cjkBytes, zh, ja, ko;
    if (cjkFile) cjkBytes = ReadFile(*cjkFile);
    if (zh.empty()) if (auto f = SystemFont({L"msyh.ttc", L"simsun.ttc"})) zh = ReadFile(*f);
    if (ja.empty()) if (auto f = SystemFont({L"YuGothM.ttc", L"meiryo.ttc", L"msgothic.ttc"})) ja = ReadFile(*f);
    if (ko.empty()) if (auto f = SystemFont({L"malgun.ttf", L"gulim.ttc"})) ko = ReadFile(*f);

    const auto add = [&](const wchar_t* resource, float size, bool withNames) -> ImFont* {
        const auto bytes = Resource(resource);
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;   // the bytes live in the .exe
        cfg.OversampleH = 2;
        cfg.PixelSnapH = true;
        ImFont* font = io.Fonts->AddFontFromMemoryTTF(const_cast<std::uint8_t*>(bytes.data()), static_cast<int>(bytes.size()), S(size), &cfg, latin);
        ImFontConfig merge;
        merge.MergeMode = true;
        merge.FontDataOwnedByAtlas = false;
        merge.PixelSnapH = true;
        if (cjk && !cjkBytes.empty())
            io.Fonts->AddFontFromMemoryTTF(cjkBytes.data(), static_cast<int>(cjkBytes.size()), S(size), &merge, cjk);
        if (withNames)
            for (auto* f : {&zh, &ja, &ko})
                if (!f->empty()) io.Fonts->AddFontFromMemoryTTF(f->data(), static_cast<int>(f->size()), S(size), &merge, names.Data);
        return font;
    };
    F.body = add(L"FONT_REGULAR", 14.0f, true);
    F.medium = add(L"FONT_MEDIUM", 14.0f, false);
    F.small = add(L"FONT_REGULAR", 12.5f, false);
    F.h2 = add(L"FONT_SEMIBOLD", 20.0f, false);
    F.slide = add(L"FONT_SEMIBOLD", 21.0f, false);
    F.hero = add(L"FONT_SEMIBOLD", 32.0f, false);
    F.eyebrow = add(L"FONT_SEMIBOLD", 11.5f, false);
    F.mono = add(L"FONT_MONO", 12.5f, false);
    io.FontDefault = F.body;
    ImGui_ImplDX11_InvalidateDeviceObjects();
}

void ApplyStyle() {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowPadding = {0, 0};
    st.WindowBorderSize = 0;
    st.WindowRounding = 0;
    st.FrameRounding = S(6);
    st.FramePadding = {S(10), S(8)};
    st.ItemSpacing = {S(8), S(8)};
    st.ScrollbarSize = S(10);
    st.ScrollbarRounding = S(5);
    st.PopupRounding = S(10);
    st.PopupBorderSize = 1;
    st.FrameBorderSize = 1;
    st.ChildRounding = S(8);
    st.ChildBorderSize = 1;
    st.GrabRounding = S(4);
    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = V4(P.text);
    c[ImGuiCol_TextDisabled] = V4(P.muted);
    c[ImGuiCol_WindowBg] = V4(P.panel);
    c[ImGuiCol_ChildBg] = V4(0);
    c[ImGuiCol_PopupBg] = V4(P.panel);
    c[ImGuiCol_Border] = V4(P.line);
    c[ImGuiCol_FrameBg] = V4(P.panel);
    c[ImGuiCol_FrameBgHovered] = V4(P.panel);
    c[ImGuiCol_FrameBgActive] = V4(P.panel);
    c[ImGuiCol_CheckMark] = V4(IM_COL32_WHITE);
    c[ImGuiCol_Button] = V4(P.panel);
    c[ImGuiCol_ButtonHovered] = V4(P.accentSoft);
    c[ImGuiCol_ButtonActive] = V4(P.accentSoft);
    c[ImGuiCol_Header] = V4(P.accentSoft);
    c[ImGuiCol_HeaderHovered] = V4(P.accentSoft);
    c[ImGuiCol_HeaderActive] = V4(P.accentSoft);
    c[ImGuiCol_ScrollbarBg] = V4(0);
    c[ImGuiCol_ScrollbarGrab] = V4(P.line);
    c[ImGuiCol_ScrollbarGrabHovered] = V4(P.muted);
    c[ImGuiCol_ScrollbarGrabActive] = V4(P.muted);
    c[ImGuiCol_TextSelectedBg] = V4((P.accent & 0x00FFFFFF) | 0x40000000);
    c[ImGuiCol_NavCursor] = V4(P.accent);
    c[ImGuiCol_ModalWindowDimBg] = V4(P.scrim);
}

// --- widgets ------------------------------------------------------------------

enum class Kind { Primary, Secondary, Ghost, Danger };

bool Button(const char* label, Kind kind, bool enabled = true, float minWidth = 0) {
    const ImU32 fill = kind == Kind::Primary ? P.accent : kind == Kind::Danger ? P.bad : P.panel;
    const ImU32 hover = kind == Kind::Primary ? P.accentHover : kind == Kind::Danger ? P.bad : kind == Kind::Ghost ? P.panel : P.panel;
    const ImU32 border = kind == Kind::Primary ? P.accent : kind == Kind::Danger ? P.bad : kind == Kind::Ghost ? 0 : P.line;
    const ImU32 text = kind == Kind::Primary || kind == Kind::Danger ? IM_COL32_WHITE : kind == Kind::Ghost ? P.muted : P.text;
    ImGui::PushStyleColor(ImGuiCol_Button, V4(fill));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, V4(hover));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, V4(hover));
    ImGui::PushStyleColor(ImGuiCol_Border, V4(border));
    ImGui::PushStyleColor(ImGuiCol_Text, V4(text));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {S(16), S(8)});
    if (kind == Kind::Primary) ImGui::PushFont(F.medium);
    if (!enabled) ImGui::BeginDisabled();
    const ImVec2 size = ImGui::CalcTextSize(label, nullptr, true);
    const bool hit = ImGui::Button(label, {std::max(minWidth, size.x + S(32)), 0});
    if (!enabled) ImGui::EndDisabled();
    if (kind == Kind::Primary) ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(5);
    if (enabled && ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return hit;
}

float ButtonWidth(const char* label, bool primary = false) {
    if (primary) ImGui::PushFont(F.medium);
    const float w = ImGui::CalcTextSize(label, nullptr, true).x + S(32);
    if (primary) ImGui::PopFont();
    return w;
}

/// A checkbox drawn like the mockup: a rounded square, white tick on blue.
bool Check(const char* id, const char* label, bool* value, const char* sub = nullptr, bool enabled = true) {
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float box = S(17);
    const float wrap = ImGui::GetContentRegionAvail().x - box - S(10);
    ImGui::PushFont(F.body);
    const ImVec2 t1 = ImGui::CalcTextSize(label, nullptr, false, wrap);
    ImGui::PopFont();
    ImVec2 t2{};
    if (sub) {
        ImGui::PushFont(F.small);
        t2 = ImGui::CalcTextSize(sub, nullptr, false, wrap);
        ImGui::PopFont();
    }
    const float h = std::max(box, t1.y + (sub ? t2.y + S(1) : 0)) + S(8);
    bool changed = false;
    if (!enabled) ImGui::BeginDisabled();
    if (ImGui::InvisibleButton("##c", {box + S(10) + std::max(t1.x, t2.x), h})) {
        *value = !*value;
        changed = true;
    }
    if (!enabled) ImGui::EndDisabled();
    const bool hovered = ImGui::IsItemHovered();
    if (hovered && enabled) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    const ImVec2 b0{at.x, at.y + S(5)}, b1{at.x + box, at.y + S(5) + box};
    if (*value) {
        dl->AddRectFilled(b0, b1, enabled ? P.accent : P.muted, S(4));
        const ImVec2 a{b0.x + box * 0.24f, b0.y + box * 0.52f}, m{b0.x + box * 0.43f, b0.y + box * 0.70f}, e{b0.x + box * 0.77f, b0.y + box * 0.32f};
        dl->AddLine(a, m, IM_COL32_WHITE, S(2));
        dl->AddLine(m, e, IM_COL32_WHITE, S(2));
    } else {
        dl->AddRectFilled(b0, b1, P.panel, S(4));
        dl->AddRect(b0, b1, hovered ? P.accent : P.muted, S(4), 0, S(1.5f));
    }
    const ImU32 textColour = enabled ? P.text : P.muted;
    dl->AddText(F.body, F.body->FontSize, {at.x + box + S(10), at.y + S(4)}, textColour, label, nullptr, wrap);
    if (sub) dl->AddText(F.small, F.small->FontSize, {at.x + box + S(10), at.y + S(5) + t1.y + S(1)}, P.muted, sub, nullptr, wrap);
    ImGui::PopID();
    return changed;
}

void Text(ImFont* font, ImU32 colour, const std::string& text, float wrapWidth = -1) {
    ImGui::PushFont(font);
    ImGui::PushStyleColor(ImGuiCol_Text, V4(colour));
    if (wrapWidth != 0) ImGui::PushTextWrapPos(wrapWidth < 0 ? 0.0f : ImGui::GetCursorPosX() + wrapWidth);
    ImGui::TextUnformatted(text.c_str());
    if (wrapWidth != 0) ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

/// A coloured note (good / bad / info) the width of the column.
void Note(const std::string& text, ImU32 fg, ImU32 bg) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::PushFont(F.small);
    const ImVec2 size = ImGui::CalcTextSize(text.c_str(), nullptr, false, w - S(20));
    ImGui::PopFont();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(at, {at.x + w, at.y + size.y + S(16)}, bg, S(6));
    dl->AddText(F.small, F.small->FontSize, {at.x + S(10), at.y + S(8)}, fg, text.c_str(), nullptr, w - S(20));
    ImGui::Dummy({w, size.y + S(16)});
}

/// The small "opens a list" mark (the font has no ▾).
void Chevron(ImDrawList* dl, ImVec2 c, ImU32 colour) {
    const float w = S(4.5f), h = S(2.5f);
    dl->AddLine({c.x - w, c.y - h}, {c.x, c.y + h}, colour, S(1.5f));
    dl->AddLine({c.x, c.y + h}, {c.x + w, c.y - h}, colour, S(1.5f));
}

// --- the license --------------------------------------------------------------

void License(const std::string& lang, float height) {
    const auto bytes = Resource(lang == "ru" || lang == "uk" || lang == "be" || lang == "kk" ? L"LIC_ru" : L"LIC_en");
    const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, V4(P.rail));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {S(18), S(14)});
    ImGui::BeginChild("license", {0, height}, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    std::size_t at = 0;
    bool first = true;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) {
            ImGui::Dummy({0, S(2)});
            continue;
        }
        if (first) {
            Text(F.mono, P.muted, line);
            first = false;
        } else if (line.starts_with("# ")) {
            ImGui::Dummy({0, S(6)});
            Text(F.medium, P.text, line.substr(2));
        } else {
            Text(F.body, P.text, line);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// --- the wizard ---------------------------------------------------------------

enum class Screen { Language, License, Folder, Modules, Options, Progress, Done, Uninstall, Removed };
enum class Dialog { None, Decline, Cancel, Running, Error, Network };

struct Work {
    std::mutex  m;
    float       fraction = 0;
    std::string status;
    bool        running = false, finished = false, failed = false, cancelled = false, waitingForProgram = false;
    std::string error;
};

struct Wizard {
    Screen screen = Screen::Language;
    Dialog dialog = Dialog::None;
    std::string dialogText;

    std::optional<Installed> existing;
    bool updating = false;
    std::string pathText;
    PathCheck check;
    std::string checkedText;
    std::uint64_t checkedNeed = 0;

    Choices choices;
    std::string uiLanguage;
    bool langPopup = false;

    // The manifest, loaded in the background (online: from GitHub).
    std::mutex manifestMutex;
    std::optional<Manifest> manifest;
    std::string manifestError;
    std::atomic<bool> manifestBusy{false};
    std::thread loader;

    Work work;
    std::thread worker;
    std::atomic<bool> cancel{false}, retry{false};

    int slide = 0;
    double slideStart = 0;
    bool runAfter = true, newsAfter = false;
    bool keepSettings = true;
    fs::path uninstallDir;
    std::string uninstallVersion;
    bool quit = false;

    Texture markRail, markHero, markSmall;
    Texture pain[4];
};

Wizard W;

void FetchManifest() {
    if (W.manifestBusy.exchange(true)) return;
    if (W.loader.joinable()) W.loader.join();
    W.loader = std::thread([] {
        std::string error;
        std::optional<Manifest> m = bz::setup::LoadManifest(error);
        std::lock_guard lock(W.manifestMutex);
        W.manifest = std::move(m);
        W.manifestError = error;
        W.manifestBusy = false;
    });
}

std::optional<Manifest> CurrentManifest() {
    std::lock_guard lock(W.manifestMutex);
    return W.manifest;
}

/// What the chosen parts take on disk (a guess while the manifest is on its way).
std::uint64_t NeedBytes(const std::optional<Manifest>& m) {
    if (!m) return 220ull << 20;
    std::uint64_t need = m->program.unpacked ? m->program.unpacked : m->program.size * 3;
    for (const std::string& id : W.choices.modules)
        if (const Component* c = m->Module(id)) need += c->unpacked ? c->unpacked : c->size * 2;
    if (const Component* c = m->Language(W.uiLanguage)) need += c->unpacked ? c->unpacked : c->size * 2;
    return need;
}
std::uint64_t DownloadBytes(const std::optional<Manifest>& m) {
    if (!m) return 0;
    std::uint64_t total = m->program.size;
    for (const std::string& id : W.choices.modules)
        if (const Component* c = m->Module(id)) total += c->size;
    if (const Component* c = m->Language(W.uiLanguage)) total += c->size;
    return total;
}

void SetStatus(const std::string& s, float fraction) {
    std::lock_guard lock(W.work.m);
    W.work.status = s;
    W.work.fraction = fraction;
}
void Fail(const std::string& why) {
    std::lock_guard lock(W.work.m);
    W.work.failed = true;
    W.work.error = why;
    W.work.running = false;
}

/// The installation itself, off the interface thread.
void Install(Manifest m, Choices c, bool needsAdmin) {
    // The program holds its files while it runs.
    while (ProgramRunning(c.dir)) {
        {
            std::lock_guard lock(W.work.m);
            W.work.waitingForProgram = true;
        }
        while (!W.retry && !W.cancel) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        W.retry = false;
        if (W.cancel) {
            std::lock_guard lock(W.work.m);
            W.work.cancelled = true;
            W.work.running = false;
            W.work.waitingForProgram = false;
            return;
        }
    }
    {
        std::lock_guard lock(W.work.m);
        W.work.waitingForProgram = false;
    }

    // Downloads (or reads from inside) take 70 % of the bar when online.
    const bool offline = Offline();
    const float downloadShare = offline ? 0.15f : 0.70f;
    std::vector<const Component*> parts = {&m.program};
    for (const std::string& id : c.modules)
        if (const Component* mod = m.Module(id)) parts.push_back(mod);
    const Component* pack = m.Language(c.language);
    if (pack) parts.push_back(pack);
    std::uint64_t total = 0, before = 0;
    for (const Component* p : parts) total += std::max<std::uint64_t>(p->size, 1);
    std::vector<std::vector<std::uint8_t>> files;
    for (const Component* p : parts) {
        const std::string what = p == &m.program ? Tr("st.program") : p == pack ? Tr("st.lang") : Trf("st.module", p->name.empty() ? p->id : p->name);
        const std::string status = Trf("st.download", what);
        SetStatus(status, downloadShare * before / total);
        std::string error;
        auto bytes = Fetch(m, *p, [&](std::uint64_t done, std::uint64_t) {
            SetStatus(status, downloadShare * static_cast<float>(before + std::min(done, p->size)) / total);
        }, W.cancel, error);
        if (!bytes) {
            if (W.cancel) {
                std::lock_guard lock(W.work.m);
                W.work.cancelled = true;
                W.work.running = false;
                return;
            }
            return Fail(error);
        }
        before += std::max<std::uint64_t>(p->size, 1);
        files.push_back(std::move(*bytes));
    }

    // From here on the work is not stopped halfway.
    const float unpackShare = 0.92f - downloadShare;
    SetStatus(Tr("st.unpack"), downloadShare);
    std::wstring error;
    if (needsAdmin) {
        SetStatus(Tr("st.admin"), downloadShare);
        std::error_code ec;
        const fs::path zip = fs::temp_directory_path(ec) / std::format(L"bz-setup-{}.zip", GetCurrentProcessId());
        {
            std::ofstream out(zip, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(files[0].data()), static_cast<std::streamsize>(files[0].size()));
        }
        const std::wstring args = L"--unpack \"" + zip.wstring() + L"\" \"" + c.dir.wstring() + L"\" \"" + Wide(m.version) + L"\"";
        SetStatus(Tr("st.unpack"), downloadShare);
        const auto code = RunElevated(args);
        fs::path log = zip;
        log += L".err";
        std::string why;
        if (std::ifstream in(log); in) std::getline(in, why);
        fs::remove(zip, ec);
        fs::remove(log, ec);
        if (!code) return Fail(Tr("admin.no"));
        if (*code != 0) return Fail(why.empty() ? std::string("unpack") : why);
    } else {
        std::vector<std::string> written;
        if (!Unzip(files[0], c.dir, true, [&](std::uint64_t done, std::uint64_t all) {
                SetStatus(Tr("st.unpack"), downloadShare + unpackShare * static_cast<float>(done) / std::max<std::uint64_t>(all, 1));
            }, &written, error))
            return Fail(Utf8(error));
        ForgetStaleFiles(c.dir, written);
        if (!FinishFolder(c.dir, m.version, InstallerExe(), error)) return Fail(Utf8(error));
    }

    std::size_t at = 1;
    for (const std::string& id : c.modules)
        if (const Component* mod = m.Module(id)) {
            SetStatus(Trf("st.connect", mod->name.empty() ? mod->id : mod->name), 0.93f);
            if (!PlaceModule(files[at++], mod->file, error)) return Fail(Utf8(error));
        }
    if (pack && !PlaceLanguage(files[at++], error)) return Fail(Utf8(error));
    SetProgramLanguage(c.language);

    SetStatus(Tr("st.register"), 0.96f);
    const TypeNames names{Wide(Tr("ft.part")), Wide(Tr("ft.asm")), Wide(Tr("ft.mod")), Wide(Tr("ft.lib")), Wide(Tr("ft.prj"))};
    if (!Register(c, m.version, NeedBytes(m), names, error)) return Fail(Utf8(error));
    SetStatus(Tr("st.shortcuts"), 0.99f);
    MakeShortcuts(c, error);   // a missing shortcut is no reason to call the installation failed
    SetStatus(Tr("st.shortcuts"), 1.0f);
    std::lock_guard lock(W.work.m);
    W.work.finished = true;
    W.work.running = false;
}

void StartInstall() {
    const auto m = CurrentManifest();
    if (!m) {
        W.dialog = Dialog::Network;
        return;
    }
    W.choices.dir = fs::path(Wide(W.pathText)).lexically_normal();
    W.choices.language = W.uiLanguage;
    const bool needsAdmin = CheckPath(W.choices.dir.wstring(), 0).state == PathState::NeedsAdmin && !IsElevated();
    {
        std::lock_guard lock(W.work.m);
        W.work.fraction = 0;
        W.work.finished = W.work.failed = W.work.cancelled = W.work.waitingForProgram = false;
        W.work.error.clear();
        W.work.running = true;
        W.work.status = Tr("st.fetch");
    }
    W.cancel = false;
    W.retry = false;
    W.slide = 0;
    W.slideStart = ImGui::GetTime();
    W.screen = Screen::Progress;
    if (W.worker.joinable()) W.worker.join();
    W.worker = std::thread(Install, *m, W.choices, needsAdmin);
}

// --- pages --------------------------------------------------------------------

constexpr float kWindowW = 820, kWindowH = 540, kRail = 210, kBar = 58;

void DrawRail(int step) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetWindowPos();
    const float h = ImGui::GetWindowHeight();
    dl->AddRectFilled(o, {o.x + S(kRail), o.y + h}, P.rail);
    dl->AddLine({o.x + S(kRail) - 0.5f, o.y}, {o.x + S(kRail) - 0.5f, o.y + h}, P.line);
    // Brand: mark, name, version.
    const float x = o.x + S(18), y = o.y + S(22);
    if (W.markRail) dl->AddImage(W.markRail.id(), {x, y}, {x + W.markRail.w, y + W.markRail.h});
    const float tx = x + W.markRail.w + S(10);
    dl->AddText(F.medium, S(16), {tx, y + S(1)}, P.text, "Breeze");
    const float bw = F.medium->CalcTextSizeA(S(16), FLT_MAX, 0, "Breeze").x;
    dl->AddText(F.medium, S(16), {tx + bw, y + S(1)}, P.accent, "CAD");
    const auto m = CurrentManifest();
    const std::string version = m ? m->version : (W.existing ? W.existing->version : std::string("…"));
    dl->AddText(F.mono, F.mono->FontSize, {tx, y + S(21)}, P.muted, version.c_str());
    // The steps, except while installing (the bar says enough then).
    if (step >= 0) {
        float sy = y + S(64);
        for (int i = 0; i < 7; ++i) {
            const std::string label = Tr(std::format("step.{}", i));
            const bool done = i < step, cur = i == step;
            if (cur) dl->AddRectFilled({o.x + S(10), sy - S(6)}, {o.x + S(kRail) - S(10), sy + S(24)}, P.accentSoft, S(6));
            const ImVec2 c{o.x + S(28), sy + S(9)};
            if (done) {
                dl->AddCircleFilled(c, S(9), P.accent);
                dl->AddLine({c.x - S(4), c.y}, {c.x - S(1), c.y + S(3)}, IM_COL32_WHITE, S(1.6f));
                dl->AddLine({c.x - S(1), c.y + S(3)}, {c.x + S(4), c.y - S(3)}, IM_COL32_WHITE, S(1.6f));
            } else {
                dl->AddCircle(c, S(9), cur ? P.accent : P.line, 0, S(1.5f));
                const std::string n = std::to_string(i + 1);
                const ImVec2 ts = F.small->CalcTextSizeA(S(10), FLT_MAX, 0, n.c_str());
                dl->AddText(F.small, S(10), {c.x - ts.x / 2, c.y - ts.y / 2}, cur ? P.accent : P.muted, n.c_str());
            }
            dl->AddText(cur ? F.medium : F.body, S(13), {o.x + S(46), sy + S(1)}, cur ? P.accent : done ? P.text : P.muted, label.c_str());
            sy += S(32);
        }
    }
    const std::string foot = Tr("foot");
    const float fw = S(kRail) - S(36);
    const ImVec2 fs = F.small->CalcTextSizeA(S(11.5f), FLT_MAX, fw, foot.c_str());
    dl->AddText(F.small, S(11.5f), {o.x + S(18), o.y + h - S(20) - fs.y}, P.muted, foot.c_str(), nullptr, fw);
}

/// The content column right of the rail; returns false when nothing to draw.
void BeginContent(bool rail) {
    const float left = rail ? S(kRail) : 0;
    ImGui::SetCursorPos({left + S(30), S(26)});
    ImGui::BeginChild("content", {ImGui::GetWindowWidth() - left - S(60), ImGui::GetWindowHeight() - S(kBar) - S(26) - S(8)}, 0,
                      ImGuiWindowFlags_NoBackground);
}
void EndContent() { ImGui::EndChild(); }

void Heading(const std::string& title, const std::string& sub) {
    Text(F.h2, P.text, title);
    ImGui::Dummy({0, S(2)});
    Text(F.body, P.muted, sub, ImGui::GetContentRegionAvail().x * 0.95f);
    ImGui::Dummy({0, S(10)});
}

/// The bottom bar: `left` and `right` draw their buttons into it.
template <class L, class R>
void Bar(bool rail, L&& left, R&& right, float rightWidth) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetWindowPos();
    const float x0 = rail ? S(kRail) : 0, top = ImGui::GetWindowHeight() - S(kBar);
    dl->AddLine({o.x + x0, o.y + top}, {o.x + ImGui::GetWindowWidth(), o.y + top}, P.line);
    ImGui::SetCursorPos({x0 + S(18), top + S(12)});
    left();
    ImGui::SetCursorPos({ImGui::GetWindowWidth() - S(18) - rightWidth, top + S(12)});
    right();
}

void PageLanguage() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetWindowPos();
    const float w = ImGui::GetWindowWidth(), h = ImGui::GetWindowHeight() - S(kBar);
    // Hero: mark, name, tagline, the language in use.
    const float cx = o.x + w / 2;
    float y = o.y + h * 0.5f - S(130);
    if (W.markHero) dl->AddImage(W.markHero.id(), {cx - W.markHero.w / 2.0f, y}, {cx + W.markHero.w / 2.0f, y + W.markHero.h});
    y += W.markHero.h + S(16);
    const float bw = F.hero->CalcTextSizeA(F.hero->FontSize, FLT_MAX, 0, "Breeze").x;
    const float cw = F.hero->CalcTextSizeA(F.hero->FontSize, FLT_MAX, 0, "CAD").x;
    dl->AddText(F.hero, F.hero->FontSize, {cx - (bw + cw) / 2, y}, P.text, "Breeze");
    dl->AddText(F.hero, F.hero->FontSize, {cx - (bw + cw) / 2 + bw, y}, P.accent, "CAD");
    y += F.hero->FontSize + S(4);
    const auto m = CurrentManifest();
    const std::string tag = std::string(Tr("tagline")) + (m ? " · " + m->version : "");
    const float tw = F.body->CalcTextSizeA(F.body->FontSize, FLT_MAX, 0, tag.c_str()).x;
    dl->AddText(F.body, F.body->FontSize, {cx - tw / 2, y}, P.muted, tag.c_str());
    y += S(36);

    // The language field (opens the list, like the Language button).
    const LanguageInfo* cur = nullptr;
    for (const LanguageInfo& l : Languages())
        if (CurrentLanguage() == l.code) cur = &l;
    const std::string label = cur ? cur->name : "English";
    const std::string hint = SystemLanguage() == CurrentLanguage() ? std::string(Tr("system")) : std::string();
    const float fw = S(300);
    ImGui::SetCursorScreenPos({cx - fw / 2, y});
    const ImVec2 f0 = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("picked", {fw, S(42)})) W.langPopup = !W.langPopup;
    const bool hov = ImGui::IsItemHovered();
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    dl->AddRectFilled(f0, {f0.x + fw, f0.y + S(42)}, P.panel, S(8));
    dl->AddRect(f0, {f0.x + fw, f0.y + S(42)}, hov ? P.accentBorder : P.line, S(8));
    dl->AddText(F.body, F.body->FontSize, {f0.x + S(14), f0.y + S(12)}, P.text, label.c_str());
    const float hw = F.small->CalcTextSizeA(F.small->FontSize, FLT_MAX, 0, hint.c_str()).x;
    dl->AddText(F.small, F.small->FontSize, {f0.x + fw - S(32) - hw, f0.y + S(13)}, P.muted, hint.c_str());
    Chevron(dl, {f0.x + fw - S(20), f0.y + S(21)}, P.muted);

    // "Language" in the corner: the one word most people know whatever they speak.
    const char* lang = "Language";
    const float lw = F.body->CalcTextSizeA(S(13), FLT_MAX, 0, lang).x + S(40);
    ImGui::SetCursorScreenPos({o.x + w - S(16) - lw, o.y + S(14)});
    const ImVec2 l0 = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("language", {lw, S(30)})) W.langPopup = !W.langPopup;
    const bool lh = ImGui::IsItemHovered();
    if (lh) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    dl->AddRectFilled(l0, {l0.x + lw, l0.y + S(30)}, P.panel, S(15));
    dl->AddRect(l0, {l0.x + lw, l0.y + S(30)}, lh ? P.accentBorder : P.line, S(15));
    const ImVec2 g{l0.x + S(17), l0.y + S(15)};
    const ImU32 gc = lh ? P.accent : P.text;
    dl->AddCircle(g, S(7), gc, 0, S(1.4f));
    dl->AddLine({g.x - S(7), g.y}, {g.x + S(7), g.y}, gc, S(1.2f));
    dl->AddBezierCubic({g.x, g.y - S(7)}, {g.x + S(4), g.y - S(3)}, {g.x + S(4), g.y + S(3)}, {g.x, g.y + S(7)}, gc, S(1.2f));
    dl->AddBezierCubic({g.x, g.y - S(7)}, {g.x - S(4), g.y - S(3)}, {g.x - S(4), g.y + S(3)}, {g.x, g.y + S(7)}, gc, S(1.2f));
    dl->AddText(F.body, S(13), {l0.x + S(30), l0.y + S(7)}, gc, lang);

    if (W.langPopup) {
        const float pw = std::min(S(420), w - S(32));
        ImGui::SetNextWindowPos({o.x + w - S(16) - pw, o.y + S(52)});
        ImGui::SetNextWindowSize({pw, 0});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {S(8), S(8)});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(10));
        ImGui::Begin("##languages", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
        if (ImGui::BeginTable("langs", 2)) {
            for (const LanguageInfo& l : Languages()) {
                ImGui::TableNextColumn();
                const bool current = CurrentLanguage() == l.code;
                ImGui::PushStyleColor(ImGuiCol_Text, V4(current ? P.accent : P.text));
                if (ImGui::Selectable(l.name, false, 0, {0, S(26)})) {
                    UseLanguage(l.code);
                    W.uiLanguage = l.code;
                    gFontsDirty = true;
                    W.langPopup = false;
                }
                ImGui::PopStyleColor();
            }
            ImGui::EndTable();
        }
        ImGui::Separator();
        Text(F.small, P.muted, Tr("lang.hint"), pw - S(24));
        if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseClicked(0) && !lh && !hov) W.langPopup = false;
        ImGui::End();
        ImGui::PopStyleVar(3);
    }

    const char* next = Tr("next");
    Bar(false, [] {}, [&] {
        if (Button(next, Kind::Primary)) W.screen = Screen::License;
    }, ButtonWidth(next, true));
}

void PageLicense() {
    DrawRail(1);
    BeginContent(true);
    Heading(Tr("lic.title"), Tr("lic.sub"));
    const std::string& lang = CurrentLanguage();
    const bool other = !(lang == "ru" || lang == "en" || lang == "uk" || lang == "be" || lang == "kk");
    if (other) {
        Text(F.small, P.muted, Tr("lic.only"));
        ImGui::Dummy({0, S(2)});
    }
    License(lang, ImGui::GetContentRegionAvail().y - S(4));
    EndContent();
    const char *back = Tr("back"), *decline = Tr("decline"), *accept = Tr("accept");
    const float rw = ButtonWidth(decline) + ButtonWidth(accept, true) + S(8);
    Bar(true, [&] {
        if (Button(back, Kind::Ghost)) W.screen = Screen::Language;
    }, [&] {
        if (Button(decline, Kind::Secondary)) W.dialog = Dialog::Decline;
        ImGui::SameLine();
        if (Button(accept, Kind::Primary)) W.screen = Screen::Folder;
    }, rw);
}

void PageFolder() {
    DrawRail(2);
    BeginContent(true);
    const auto m = CurrentManifest();
    const bool locked = W.updating;
    if (locked) Heading(Tr("upd.title"), Tr("upd.sub"));
    else Heading(Tr("path.title"), Tr("path.sub"));
    Text(F.small, P.muted, Tr("path.label"));
    const std::uint64_t need = NeedBytes(m);
    if (W.checkedText != W.pathText || W.checkedNeed != need) {
        W.check = CheckPath(Wide(W.pathText), need);
        W.checkedText = W.pathText;
        W.checkedNeed = need;
    }
    const PathState st = W.check.state;
    const bool bad = !locked && st != PathState::Ok && st != PathState::NeedsAdmin;
    const char* browse = Tr(locked ? "upd.change" : "browse");
    const float bw = ButtonWidth(browse);
    ImGui::PushFont(F.mono);
    ImGui::PushStyleColor(ImGuiCol_Border, V4(bad ? P.bad : st == PathState::Ok && !locked ? P.ok : P.line));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - bw - S(8));
    char buffer[2048];
    std::snprintf(buffer, sizeof(buffer), "%s", W.pathText.c_str());
    if (locked) ImGui::BeginDisabled();
    if (ImGui::InputText("##path", buffer, sizeof(buffer))) W.pathText = buffer;
    if (locked) ImGui::EndDisabled();
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine();
    if (Button(browse, Kind::Secondary)) {
        if (locked) W.updating = false;
        else if (auto picked = PickFolder(gWindow, fs::path(Wide(W.pathText)), Wide(Tr("pick.title")))) {
            fs::path p = *picked;
            if (p.filename() != L"BreezeCAD") p /= L"BreezeCAD";
            W.pathText = Utf8(p.wstring());
        }
    }
    ImGui::Dummy({0, S(2)});
    const std::string freeText = Size(W.check.freeBytes), needText = Size(need);
    if (locked) Note(Trf("upd.found", W.existing ? W.existing->version : std::string()), P.accent, P.accentSoft);
    else
        switch (st) {
        case PathState::Ok: Note(Trf("p.ok", freeText, needText), P.ok, P.okSoft); break;
        case PathState::NeedsAdmin: Note(Trf("p.admin", freeText, needText), P.accent, P.accentSoft); break;
        case PathState::Empty: Note(Tr("p.empty"), P.bad, P.badSoft); break;
        case PathState::NotAbsolute: Note(Tr("p.notabs"), P.bad, P.badSoft); break;
        case PathState::DriveRoot: Note(Tr("p.root"), P.bad, P.badSoft); break;
        case PathState::BadCharacters: Note(Tr("p.chars"), P.bad, P.badSoft); break;
        case PathState::NoDrive: Note(Trf("p.nodrive", Utf8(std::wstring(1, W.check.drive))), P.bad, P.badSoft); break;
        case PathState::SystemFolder: Note(Tr("p.system"), P.bad, P.badSoft); break;
        case PathState::Foreign: Note(Tr("p.foreign"), P.bad, P.badSoft); break;
        case PathState::NoSpace: Note(Trf("p.nospace", freeText, needText), P.bad, P.badSoft); break;
        }
    ImGui::Dummy({0, S(10)});
    Text(F.small, P.muted, Tr("ui.lang"));
    const char* currentName = "English";
    for (const LanguageInfo& l : Languages())
        if (W.uiLanguage == l.code) currentName = l.name;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    const bool open = ImGui::BeginCombo("##uilang", currentName, ImGuiComboFlags_NoArrowButton);
    {
        const ImVec2 r = ImGui::GetItemRectMax(), l = ImGui::GetItemRectMin();
        Chevron(ImGui::GetWindowDrawList(), {r.x - S(16), (l.y + r.y) / 2}, P.muted);
    }
    if (open) {
        for (const LanguageInfo& l : Languages()) {
            std::string item = l.name;
            if (m)
                if (const Component* c = m->Language(l.code)) item += "   +" + Size(c->size);
            if (ImGui::Selectable(item.c_str(), W.uiLanguage == l.code)) W.uiLanguage = l.code;
        }
        ImGui::EndCombo();
    }
    EndContent();
    const char *back = Tr("back"), *next = Tr("next");
    Bar(true, [&] {
        if (Button(back, Kind::Ghost)) W.screen = Screen::License;
    }, [&] {
        if (Button(next, Kind::Primary, !bad)) W.screen = Screen::Modules;
    }, ButtonWidth(next, true));
}

void Card(const char* id, const std::string& title, const std::string& text, const std::string& size, bool* value, bool fixed) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;
    const float textW = w - S(28) - S(40) - S(70);
    const ImVec2 th = F.small->CalcTextSizeA(F.small->FontSize, FLT_MAX, textW, text.c_str());
    const float h = S(14) + F.medium->FontSize + S(4) + th.y + S(14);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    bool hovered = false;
    if (!fixed && value) {
        if (ImGui::InvisibleButton("card", {w, h})) *value = !*value;
        hovered = ImGui::IsItemHovered();
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    } else {
        ImGui::Dummy({w, h});
    }
    ImGui::PopID();
    const bool on = value && *value;
    dl->AddRectFilled(at, {at.x + w, at.y + h}, fixed ? P.rail : on ? P.accentSoft : P.panel, S(8));
    dl->AddRect(at, {at.x + w, at.y + h}, on && !fixed ? P.accentBorder : hovered ? P.accentBorder : P.line, S(8));
    float tx = at.x + S(14);
    if (value) {
        const float box = S(17);
        const ImVec2 b0{at.x + S(14), at.y + S(15)}, b1{b0.x + box, b0.y + box};
        if (*value) {
            dl->AddRectFilled(b0, b1, fixed ? P.muted : P.accent, S(4));
            dl->AddLine({b0.x + box * 0.24f, b0.y + box * 0.52f}, {b0.x + box * 0.43f, b0.y + box * 0.70f}, IM_COL32_WHITE, S(2));
            dl->AddLine({b0.x + box * 0.43f, b0.y + box * 0.70f}, {b0.x + box * 0.77f, b0.y + box * 0.32f}, IM_COL32_WHITE, S(2));
        } else {
            dl->AddRect(b0, b1, P.muted, S(4), 0, S(1.5f));
        }
        tx += box + S(12);
    }
    dl->AddText(F.medium, F.medium->FontSize, {tx, at.y + S(14)}, P.text, title.c_str());
    dl->AddText(F.small, F.small->FontSize, {tx, at.y + S(14) + F.medium->FontSize + S(4)}, P.muted, text.c_str(), nullptr, textW);
    if (!size.empty()) {
        const float sw = F.mono->CalcTextSizeA(F.mono->FontSize, FLT_MAX, 0, size.c_str()).x;
        dl->AddText(F.mono, F.mono->FontSize, {at.x + w - S(14) - sw, at.y + S(16)}, P.muted, size.c_str());
    }
    ImGui::Dummy({0, S(2)});
}

void PageModules() {
    DrawRail(3);
    BeginContent(true);
    Heading(Tr("mod.title"), Tr("mod.sub"));
    const auto m = CurrentManifest();
    if (!m) {
        Text(F.body, P.muted, W.manifestBusy ? std::string(Tr("st.fetch")) + "…" : std::string(Tr("net.title")));
    } else {
        if (m->modules.empty()) Text(F.body, P.muted, Tr("mod.none"));
        for (const Component& c : m->modules) {
            // Known modules speak the installer's language; others say what their module.json says.
            const std::string key = "mod." + c.id;
            const std::string name = std::string(Tr(key + ".name")) != key + ".name" ? Tr(key + ".name") : c.name.empty() ? c.id : c.name;
            const std::string text = std::string(Tr(key + ".desc")) != key + ".desc" ? Tr(key + ".desc") : c.description;
            bool on = std::find(W.choices.modules.begin(), W.choices.modules.end(), c.id) != W.choices.modules.end();
            const bool was = on;
            Card(c.id.c_str(), name, text, Size(c.size), &on, false);
            if (on != was) {
                if (on) W.choices.modules.push_back(c.id);
                else std::erase(W.choices.modules, c.id);
            }
        }
        if (const Component* pack = m->Language(W.uiLanguage)) {
            bool on = true;
            const char* name = "";
            for (const LanguageInfo& l : Languages())
                if (W.uiLanguage == l.code) name = l.name;
            Card("lang", std::string(Tr("langpack")) + ": " + name, Tr("langpack.desc"), Size(pack->size), &on, true);
        }
    }
    Card("core", Tr("core"), Tr("core.desc"), "", nullptr, true);
    EndContent();
    const char *back = Tr("back"), *next = Tr("next");
    Bar(true, [&] {
        if (Button(back, Kind::Ghost)) W.screen = Screen::Folder;
    }, [&] {
        if (Button(next, Kind::Primary)) W.screen = Screen::Options;
    }, ButtonWidth(next, true));
}

void PageOptions() {
    DrawRail(4);
    BeginContent(true);
    Heading(Tr("opt.title"), Tr("opt.sub"));
    const float colW = (ImGui::GetContentRegionAvail().x - S(28)) / 2;
    if (ImGui::BeginTable("opts", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("a", ImGuiTableColumnFlags_WidthFixed, colW);
        ImGui::TableSetupColumn("b", ImGuiTableColumnFlags_WidthFixed, colW);
        ImGui::TableNextColumn();
        Text(F.eyebrow, P.muted, Tr("shortcuts"));
        ImGui::Dummy({0, S(2)});
        Check("desk", Tr("desktop"), &W.choices.desktop);
        Check("start", Tr("startmenu"), &W.choices.startMenu);
        ImGui::TableNextColumn();
        Text(F.eyebrow, P.muted, Tr("assoc"));
        ImGui::Dummy({0, S(2)});
        Check("bzc", (std::string(".bzc   ") + Tr("t.part")).c_str(), &W.choices.openParts);
        Check("bza", (std::string(".bza   ") + Tr("t.asm")).c_str(), &W.choices.openAssemblies);
        Check("bzmod", (std::string(".bzmod   ") + Tr("t.mod")).c_str(), &W.choices.openModules, Tr("t.mod.sub"));
        Check("bzlib", (std::string(".bzlib   ") + Tr("t.lib")).c_str(), &W.choices.openLibraries);
        Check("bzp", (std::string(".bzp   ") + Tr("t.prj")).c_str(), &W.choices.openProjects, Tr("t.prj.sub"));
        ImGui::EndTable();
    }
    EndContent();
    const auto m = CurrentManifest();
    const char *back = Tr("back"), *go = Tr(W.updating ? "update" : "install");
    const std::string total = Offline() ? std::string(Tr("total.offline")) : m ? Trf("total", Size(DownloadBytes(m))) : std::string(Tr("st.fetch")) + "…";
    Bar(true, [&] {
        if (Button(back, Kind::Ghost)) W.screen = Screen::Modules;
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(8));
        Text(F.small, P.muted, total, 0);
    }, [&] {
        if (Button(go, Kind::Primary, m.has_value() || !W.manifestBusy)) StartInstall();
    }, ButtonWidth(go, true));
}

void PageProgress() {
    DrawRail(-1);
    float fraction;
    std::string status;
    bool finished, failed, cancelled, waiting;
    std::string error;
    {
        std::lock_guard lock(W.work.m);
        fraction = W.work.fraction;
        status = W.work.status;
        finished = W.work.finished;
        failed = W.work.failed;
        cancelled = W.work.cancelled;
        waiting = W.work.waitingForProgram;
        error = W.work.error;
    }
    if (finished) W.screen = Screen::Done;
    if (cancelled) {
        W.screen = Screen::Options;
        W.dialog = Dialog::None;
    }
    if (failed && W.dialog == Dialog::None) {
        W.dialog = Dialog::Error;
        W.dialogText = error;
    }
    if (waiting && W.dialog == Dialog::None) W.dialog = Dialog::Running;

    // The engineer's pain, one story every 7 seconds.
    const double now = ImGui::GetTime();
    if (now - W.slideStart > 7.0) {
        W.slide = (W.slide + 1) % 4;
        W.slideStart = now;
    }
    const float alpha = static_cast<float>(std::clamp((now - W.slideStart) / 0.45, 0.25, 1.0));
    const float left = S(kRail) + S(30);
    const float width = ImGui::GetWindowWidth() - left - S(30);
    const float bottom = ImGui::GetWindowHeight() - S(86);
    ImGui::SetCursorPos({left, S(26)});
    Text(F.eyebrow, P.accent, std::format("{} · {}/4", Tr("pain.eyebrow"), W.slide + 1));
    ImGui::SetCursorPos({left, S(52)});
    const float textW = width * 0.48f, imgW = width - textW - S(22);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::BeginChild("story", {textW, bottom - S(52) - S(30)}, 0, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
    const std::string k = std::format("pain.{}.", W.slide + 1);
    Text(F.slide, P.text, Tr(k + "t"), textW);
    ImGui::Dummy({0, S(4)});
    Text(F.body, P.muted, Tr(k + "s"), textW);
    ImGui::Dummy({0, S(10)});
    const ImVec2 fixAt = ImGui::GetCursorScreenPos();
    ImGui::Indent(S(14));
    Text(F.medium, P.text, std::format("{}: {}", Tr("fix"), Tr(k + "f")), textW - S(14));
    ImGui::Dummy({0, S(2)});
    Text(F.body, P.text, Tr(k + "x"), textW - S(14));
    ImGui::Unindent(S(14));
    ImGui::GetWindowDrawList()->AddRectFilled(fixAt, {fixAt.x + S(3), ImGui::GetCursorScreenPos().y}, P.accent, S(1.5f));
    ImGui::EndChild();
    // The picture beside it, as on the site.
    const Texture& pic = W.pain[W.slide];
    if (pic) {
        const float ih = imgW * pic.h / pic.w;
        const float iy = S(52) + std::max(0.0f, (bottom - S(52) - S(30) - ih) / 2);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 o = ImGui::GetWindowPos();
        const ImVec2 a{o.x + left + textW + S(22), o.y + iy}, b{a.x + imgW, a.y + ih};
        dl->AddImageRounded(pic.id(), a, b, {0, 0}, {1, 1}, IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)), S(8));
        dl->AddRect(a, b, P.line, S(8));
    }
    ImGui::PopStyleVar();
    // Dots: pick a story.
    ImGui::SetCursorPos({left, bottom - S(22)});
    for (int i = 0; i < 4; ++i) {
        ImGui::PushID(i);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("dot", {S(22), S(10)})) {
            W.slide = i;
            W.slideStart = now;
        }
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::GetWindowDrawList()->AddRectFilled({at.x, at.y + S(3)}, {at.x + S(22), at.y + S(7)}, i == W.slide ? P.accent : P.line, S(2));
        ImGui::PopID();
        ImGui::SameLine(0, S(6));
    }
    // The bar, what is happening, Cancel.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetWindowPos();
    const float top = ImGui::GetWindowHeight() - S(86);
    dl->AddLine({o.x + S(kRail), o.y + top}, {o.x + ImGui::GetWindowWidth(), o.y + top}, P.line);
    const float x0 = o.x + S(kRail) + S(18), x1 = o.x + ImGui::GetWindowWidth() - S(18);
    const std::string pct = std::format("{:.0f} %", fraction * 100);
    dl->AddText(F.small, F.small->FontSize, {x0, o.y + top + S(12)}, P.text, (status + "…").c_str());
    const float pw = F.mono->CalcTextSizeA(F.mono->FontSize, FLT_MAX, 0, pct.c_str()).x;
    dl->AddText(F.mono, F.mono->FontSize, {x1 - pw, o.y + top + S(12)}, P.muted, pct.c_str());
    const float by = o.y + top + S(34);
    dl->AddRectFilled({x0, by}, {x1, by + S(6)}, P.line, S(3));
    dl->AddRectFilled({x0, by}, {x0 + (x1 - x0) * std::clamp(fraction, 0.0f, 1.0f), by + S(6)}, P.accent, S(3));
    dl->AddText(F.small, S(11.5f), {x0, o.y + top + S(54)}, P.muted, Tr("hint"));
    const char* cancel = Tr("cancel");
    ImGui::SetCursorScreenPos({x1 - ButtonWidth(cancel), o.y + top + S(46)});
    if (Button(cancel, Kind::Ghost, fraction < (Offline() ? 0.15f : 0.70f))) W.dialog = Dialog::Cancel;
}

void PageDone() {
    DrawRail(6);
    BeginContent(true);
    const auto m = CurrentManifest();
    ImGui::Dummy({0, ImGui::GetContentRegionAvail().y * 0.18f});
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c = ImGui::GetCursorScreenPos();
    dl->AddCircleFilled({c.x + S(23), c.y + S(23)}, S(23), P.okSoft);
    dl->AddLine({c.x + S(14), c.y + S(24)}, {c.x + S(20), c.y + S(30)}, P.ok, S(2.6f));
    dl->AddLine({c.x + S(20), c.y + S(30)}, {c.x + S(32), c.y + S(17)}, P.ok, S(2.6f));
    ImGui::Dummy({S(46), S(56)});
    const std::string version = m ? m->version : std::string();
    Text(F.h2, P.text, W.updating ? Trf("fin.upd", version) : Trf("fin.title", version));
    ImGui::Dummy({0, S(2)});
    Text(F.body, P.muted, Tr("fin.sub"), ImGui::GetContentRegionAvail().x * 0.9f);
    ImGui::Dummy({0, S(10)});
    Check("run", Tr("run"), &W.runAfter);
    Check("news", Tr("news"), &W.newsAfter);
    EndContent();
    const char* finish = Tr("finish");
    Bar(true, [] {}, [&] {
        if (Button(finish, Kind::Primary)) {
            if (W.runAfter) Launch(W.choices.dir);
            if (W.newsAfter && m) OpenUrl(L"https://github.com/MikeVerx/breezecad/releases/tag/" + Wide(m->tag));
            W.quit = true;
        }
    }, ButtonWidth(finish, true));
}

void PageUninstall() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetWindowPos();
    const float w = ImGui::GetWindowWidth();
    const bool removed = W.screen == Screen::Removed;
    float y = o.y + S(70);
    if (W.markHero) dl->AddImage(W.markHero.id(), {o.x + w / 2 - W.markHero.w / 2.0f, y}, {o.x + w / 2 + W.markHero.w / 2.0f, y + W.markHero.h});
    y += W.markHero.h + S(24);
    ImGui::SetCursorScreenPos({o.x + S(110), y});
    ImGui::BeginGroup();
    const float tw = w - S(220);
    Text(F.h2, P.text, removed ? Tr("un.done") : Tr("un.title"), tw);
    ImGui::Dummy({0, S(4)});
    Text(F.body, P.muted, removed ? std::string(Tr("un.done.sub")) : Trf("un.text", W.uninstallVersion), tw);
    ImGui::Dummy({0, S(14)});
    if (!removed) Check("keep", Tr("un.keep"), &W.keepSettings);
    ImGui::EndGroup();
    if (removed) {
        const char* close = Tr("close");
        Bar(false, [] {}, [&] {
            if (Button(close, Kind::Primary)) W.quit = true;
        }, ButtonWidth(close, true));
        return;
    }
    const char *cancel = Tr("cancel"), *remove = Tr("un.btn");
    Bar(false, [] {}, [&] {
        if (Button(cancel, Kind::Secondary)) W.quit = true;
        ImGui::SameLine();
        if (Button(remove, Kind::Danger)) {
            std::wstring error;
            if (ProgramRunning(W.uninstallDir)) W.dialog = Dialog::Running;
            else if (Uninstall(W.uninstallDir, W.keepSettings, error)) W.screen = Screen::Removed;
            else {
                W.dialog = Dialog::Error;
                W.dialogText = Utf8(error);
            }
        }
    }, ButtonWidth(cancel) + ButtonWidth(remove) + S(8));
}

void DrawDialog() {
    if (W.dialog == Dialog::None) return;
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    // The scrim: its own window above the page and below the dialog.
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, V4(P.scrim));
    ImGui::Begin("##scrim", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::End();
    ImGui::PopStyleColor();
    const float w = std::min(S(420), size.x - S(32));
    ImGui::SetNextWindowPos({size.x / 2, size.y / 2}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({w, 0});
    ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {S(22), S(20)});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(10));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);
    ImGui::Begin("##dialog", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    std::string title, text;
    switch (W.dialog) {
    case Dialog::Decline: title = Tr("dec.title"), text = Tr("dec.text"); break;
    case Dialog::Cancel: title = Tr("cancel.title"), text = Tr("cancel.text"); break;
    case Dialog::Running: title = Tr("running.title"), text = Tr("running.text"); break;
    case Dialog::Error: title = Tr("fail.title"), text = Trf("fail.text", W.dialogText); break;
    case Dialog::Network: title = Tr("net.title"), text = Tr("net.text"); break;
    case Dialog::None: break;
    }
    const float inner = w - S(44);
    Text(F.h2, P.text, title, inner);
    ImGui::Dummy({0, S(2)});
    Text(F.body, P.muted, text, inner);
    ImGui::Dummy({0, S(10)});
    const auto right = [&](std::initializer_list<std::pair<const char*, Kind>> buttons) -> int {
        float total = 0;
        for (const auto& [label, kind] : buttons) total += ButtonWidth(label, kind == Kind::Primary) + S(8);
        ImGui::SetCursorPosX(w - S(22) - total + S(8));
        int i = 0, hit = -1;
        for (const auto& [label, kind] : buttons) {
            if (i) ImGui::SameLine();
            if (Button(label, kind)) hit = i;
            ++i;
        }
        return hit;
    };
    switch (W.dialog) {
    case Dialog::Decline:
        if (const int hit = right({{Tr("dec.back"), Kind::Secondary}, {Tr("dec.close"), Kind::Danger}}); hit == 0) W.dialog = Dialog::None;
        else if (hit == 1) W.quit = true;
        break;
    case Dialog::Cancel:
        if (const int hit = right({{Tr("cancel.no"), Kind::Secondary}, {Tr("cancel.yes"), Kind::Danger}}); hit == 0) W.dialog = Dialog::None;
        else if (hit == 1) {
            W.cancel = true;
            W.dialog = Dialog::None;
        }
        break;
    case Dialog::Running:
        if (const int hit = right({{Tr("cancel"), Kind::Secondary}, {Tr("retry"), Kind::Primary}}); hit == 0) {
            W.cancel = true;
            W.dialog = Dialog::None;
            if (W.screen == Screen::Uninstall) W.quit = true;
        } else if (hit == 1) {
            W.retry = true;
            W.dialog = Dialog::None;
        }
        break;
    case Dialog::Error:
        if (right({{Tr("close"), Kind::Primary}}) == 0) {
            W.dialog = Dialog::None;
            if (W.screen == Screen::Progress) W.screen = Screen::Options;
        }
        break;
    case Dialog::Network:
        if (const int hit = right({{Tr("net.page"), Kind::Secondary}, {Tr("retry"), Kind::Primary}}); hit == 0)
            OpenUrl(L"https://breezecad.ru");
        else if (hit == 1) {
            W.dialog = Dialog::None;
            FetchManifest();
        }
        break;
    case Dialog::None: break;
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void Frame() {
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, V4(P.panel));
    ImGui::Begin("##setup", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleColor();
    const bool modal = W.dialog != Dialog::None;
    if (modal) ImGui::BeginDisabled();
    switch (W.screen) {
    case Screen::Language: PageLanguage(); break;
    case Screen::License: PageLicense(); break;
    case Screen::Folder: PageFolder(); break;
    case Screen::Modules: PageModules(); break;
    case Screen::Options: PageOptions(); break;
    case Screen::Progress: PageProgress(); break;
    case Screen::Done: PageDone(); break;
    case Screen::Uninstall:
    case Screen::Removed: PageUninstall(); break;
    }
    if (modal) ImGui::EndDisabled();
    ImGui::End();
    DrawDialog();

    // Closing the window: during the installation it asks first.
    if (gCloseAsked) {
        gCloseAsked = false;
        if (W.screen == Screen::Progress) {
            if (W.dialog == Dialog::None) W.dialog = Dialog::Cancel;
        } else {
            W.quit = true;
        }
    }
}

// --- the window ---------------------------------------------------------------

void MakeTextures() {
    for (Texture* t : {&W.markRail, &W.markHero, &W.markSmall}) Drop(*t);
    W.markRail = MarkTexture(static_cast<int>(S(34)), P.accent);
    W.markHero = MarkTexture(static_cast<int>(S(84)), P.accent);
    W.markSmall = MarkTexture(static_cast<int>(S(14)), P.accent);
}

void ResizeToDpi(HWND hwnd, UINT dpi) {
    gScale = dpi / 96.0f;
    RECT r{0, 0, static_cast<LONG>(S(kWindowW)), static_cast<LONG>(S(kWindowH))};
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0, dpi);
    SetWindowPos(hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, w, l)) return true;
    switch (msg) {
    case WM_SIZE:
        if (gDevice && w != SIZE_MINIMIZED) {
            DropTarget();
            gSwap->ResizeBuffers(0, LOWORD(l), HIWORD(l), DXGI_FORMAT_UNKNOWN, 0);
            MakeTarget();
        }
        return 0;
    case WM_DPICHANGED: {
        gScale = HIWORD(w) / 96.0f;
        const RECT* r = reinterpret_cast<RECT*>(l);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        gFontsDirty = true;
        return 0;
    }
    case WM_CLOSE: gCloseAsked = true; return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_SYSCOMMAND:
        if ((w & 0xfff0) == SC_KEYMENU) return 0;
        break;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

// --- pictures of every page (CI) -------------------------------------------------
// BZ_SETUP_SHOTS=<folder>: the installer walks through its pages on its own
// (a made-up release, nothing downloaded or written) and saves each one as
// a PNG, so the build shows the real windows on a real Windows.

struct Shot {
    const char* name;
    std::function<void()> set;
};
std::vector<Shot> gShots;
std::size_t       gShot = 0;
int               gShotFrame = 0;
fs::path          gShotDir;

void SaveShot(const fs::path& file) {
    ID3D11Texture2D* back = nullptr;
    gSwap->GetBuffer(0, IID_PPV_ARGS(&back));
    if (!back) return;
    D3D11_TEXTURE2D_DESC d{};
    back->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    if (SUCCEEDED(gDevice->CreateTexture2D(&d, nullptr, &staging))) {
        gContext->CopyResource(staging, back);
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(gContext->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
            std::vector<std::uint8_t> rgba(static_cast<std::size_t>(d.Width) * d.Height * 4);
            for (UINT y = 0; y < d.Height; ++y) {
                std::memcpy(&rgba[static_cast<std::size_t>(y) * d.Width * 4], static_cast<const std::uint8_t*>(m.pData) + y * m.RowPitch, d.Width * 4);
                for (UINT x = 0; x < d.Width; ++x) rgba[(static_cast<std::size_t>(y) * d.Width + x) * 4 + 3] = 255;
            }
            gContext->Unmap(staging, 0);
            std::size_t size = 0;
            if (void* png = tdefl_write_image_to_png_file_in_memory(rgba.data(), static_cast<int>(d.Width), static_cast<int>(d.Height), 4, &size)) {
                std::ofstream(file, std::ios::binary).write(static_cast<const char*>(png), static_cast<std::streamsize>(size));
                mz_free(png);
            }
        }
        staging->Release();
    }
    back->Release();
}

void Language(const char* code) {
    if (CurrentLanguage() != code) {
        UseLanguage(code);
        gFontsDirty = true;
    }
    W.uiLanguage = code;
}

void PrepareShots(const fs::path& dir) {
    gShotDir = dir;
    std::error_code ec;
    fs::create_directories(dir, ec);
    Manifest m;
    m.version = BZ_SETUP_VERSION;
    m.tag = "next-build";
    m.program = {"program", "BreezeCAD-windows.zip", "", "", 61ull << 20, 196ull << 20, ""};
    m.modules = {{"mcp", "bz-mcp-1.0.0-windows-x64.bzmod", "MCP", "", 2411724, 6ull << 20, ""}};
    m.languages = {{"de", "lang-de.zip", "", "", 400000, 900000, ""}, {"zh", "lang-zh.zip", "", "", 8178892, 9ull << 20, ""}};
    {
        std::lock_guard lock(W.manifestMutex);
        W.manifest = m;
    }
    W.manifestBusy = false;
    W.existing.reset();
    W.updating = false;
    W.choices.modules = {"mcp"};
    const auto progress = [](float f, const char* status, int slide) {
        return [f, status, slide] {
            W.screen = Screen::Progress;
            W.slide = slide;
            W.slideStart = ImGui::GetTime() - 2;
            std::lock_guard lock(W.work.m);
            W.work.running = true;
            W.work.fraction = f;
            W.work.status = status;
        };
    };
    gShots = {
        {"01-language", [] { Language("ru"); W.screen = Screen::Language; W.langPopup = false; }},
        {"02-language-list", [] { W.langPopup = true; }},
        {"03-license", [] { W.langPopup = false; W.screen = Screen::License; }},
        {"04-decline", [] { W.dialog = Dialog::Decline; }},
        {"05-folder", [] { W.dialog = Dialog::None; W.screen = Screen::Folder; W.pathText = Utf8(DefaultDir().wstring()); }},
        {"06-folder-error", [] { W.pathText = "Q:\\BreezeCAD"; }},
        {"07-folder-admin", [] { W.pathText = "C:\\Program Files\\BreezeCAD"; }},
        {"08-folder-update", [] { W.updating = true; W.existing = Installed{DefaultDir(), "0.2.4.1-Beta"}; W.pathText = Utf8(DefaultDir().wstring()); }},
        {"09-modules", [] { W.updating = false; W.existing.reset(); W.pathText = Utf8(DefaultDir().wstring()); W.screen = Screen::Modules; }},
        {"10-options", [] { W.screen = Screen::Options; }},
        {"11-progress-1", progress(0.18f, "Скачивание: программа", 0)},
        {"12-progress-2", progress(0.47f, "Скачивание: программа", 1)},
        {"13-progress-3", progress(0.76f, "Распаковка программы", 2)},
        {"14-progress-4", progress(0.97f, "Регистрация типов файлов и превью", 3)},
        {"15-running", [] { W.dialog = Dialog::Running; }},
        {"16-done", [] { W.dialog = Dialog::None; W.screen = Screen::Done; }},
        {"17-uninstall", [] { W.screen = Screen::Uninstall; W.uninstallVersion = BZ_SETUP_VERSION; }},
        {"18-removed", [] { W.screen = Screen::Removed; }},
        {"19-en-language", [] { Language("en"); W.screen = Screen::Language; }},
        {"20-en-progress", progress(0.52f, "Downloading: the program", 0)},
        {"21-zh-language", [] { Language("zh"); W.screen = Screen::Language; }},
        {"22-zh-modules", [] { W.screen = Screen::Modules; }},
        {"23-zh-progress", progress(0.33f, "正在下载：程序", 1)},
        {"24-ja-options", [] { Language("ja"); W.screen = Screen::Options; }},
        {"25-kk-folder", [] { Language("kk"); W.screen = Screen::Folder; }},
        {"26-de-progress", progress(0.64f, "Download: Programm", 3)},
    };
}

/// Called once per frame after drawing, before Present: sets up the next
/// picture or saves the current one.
void ShotTick() {
    if (gShots.empty()) return;
    if (gShotFrame == 0 && gShot < gShots.size()) gShots[gShot].set();
    if (++gShotFrame == 10) {
        SaveShot(gShotDir / (std::string(gShots[gShot].name) + ".png"));
        gShotFrame = 0;
        if (++gShot == gShots.size()) W.quit = true;
    }
}

int RunWindow(const std::wstring& title) {
    WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, WndProc, 0, 0, GetModuleHandleW(nullptr), nullptr, LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr,
                   L"BreezeCADSetup", nullptr};
    wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), L"APPICON");
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);
    const UINT dpi = GetDpiForSystem();
    gScale = dpi / 96.0f;
    RECT r{0, 0, static_cast<LONG>(S(kWindowW)), static_cast<LONG>(S(kWindowH))};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRectExForDpi(&r, style, FALSE, 0, dpi);
    const int ww = r.right - r.left, wh = r.bottom - r.top;
    gWindow = CreateWindowExW(0, wc.lpszClassName, title.c_str(), style, (GetSystemMetrics(SM_CXSCREEN) - ww) / 2,
                              (GetSystemMetrics(SM_CYSCREEN) - wh) / 2, ww, wh, nullptr, nullptr, wc.hInstance, nullptr);
    if (const UINT real = GetDpiForWindow(gWindow); real && real != dpi) ResizeToDpi(gWindow, real);
    const BOOL dark = SystemIsDark();
    DwmSetWindowAttribute(gWindow, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    if (!MakeDevice(gWindow)) {
        MessageBoxW(nullptr, L"Direct3D 11 is not available on this computer.", title.c_str(), MB_ICONERROR);
        return 1;
    }
    ShowWindow(gWindow, SW_SHOWDEFAULT);
    UpdateWindow(gWindow);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().LogFilename = nullptr;
    ImGui_ImplWin32_Init(gWindow);
    ImGui_ImplDX11_Init(gDevice, gContext);
    for (int i = 0; i < 4; ++i) {
        const std::wstring name = std::format(L"PAIN{}", i + 1);
        W.pain[i] = JpegTexture(name.c_str());
    }

    float lastScale = 0;
    while (!W.quit) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) W.quit = true;
        }
        if (W.quit) break;
        if (IsIconic(gWindow)) {
            Sleep(30);
            continue;
        }
        if (gFontsDirty || lastScale != gScale) {
            BuildFonts();
            ApplyStyle();
            MakeTextures();
            gFontsDirty = false;
            lastScale = gScale;
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        Frame();
        ImGui::Render();
        const ImVec4 clear = V4(P.panel);
        const float c[4] = {clear.x, clear.y, clear.z, 1};
        gContext->OMSetRenderTargets(1, &gTarget, nullptr);
        gContext->ClearRenderTargetView(gTarget, c);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        ShotTick();
        gSwap->Present(gShots.empty() ? 1 : 0, 0);
    }
    W.cancel = true;
    if (W.worker.joinable()) W.worker.join();
    if (W.loader.joinable()) W.loader.join();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    DestroyWindow(gWindow);
    return 0;
}

// --- entry points -------------------------------------------------------------

/// The elevated helper: unpack into a folder the person cannot write to.
int Unpack(const fs::path& zip, const fs::path& dir, const std::string& version) {
    fs::path log = zip;
    log += L".err";
    const auto fail = [&](const std::wstring& why) {
        std::ofstream out(log, std::ios::trunc);
        out << Utf8(why);
        return 1;
    };
    std::ifstream in(zip, std::ios::binary);
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
    if (bytes.empty()) return fail(L"no package");
    std::vector<std::string> written;
    std::wstring error;
    if (!Unzip(bytes, dir, true, {}, &written, error)) return fail(error);
    ForgetStaleFiles(dir, written);
    if (!FinishFolder(dir, version, InstallerExe(), error)) return fail(error);
    return 0;
}

std::string LanguageFromSettings() {
    std::ifstream in(ConfigDir() / L"settings.json");
    std::string text{std::istreambuf_iterator<char>(in), {}};
    const auto at = text.find("\"language\"");
    if (at == std::string::npos) return {};
    const auto q1 = text.find('"', text.find(':', at) + 1);
    const auto q2 = q1 == std::string::npos ? q1 : text.find('"', q1 + 1);
    if (q2 == std::string::npos) return {};
    return text.substr(q1 + 1, q2 - q1 - 1);
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args(argv + 1, argv + argc);
    LocalFree(argv);
    const auto has = [&](const wchar_t* a) { return std::find(args.begin(), args.end(), a) != args.end(); };
    P = SystemIsDark() ? Dark() : Light();

    if (args.size() >= 4 && args[0] == L"--unpack") return Unpack(args[1], args[2], Utf8(args[3]));

    // CI: the whole installation without the pages (every module, no
    // shortcuts) into <dir>; the outcome goes to <dir>.result.
    if (args.size() >= 2 && args[0] == L"--test-install") {
        UseLanguage("ru");
        const fs::path dir = args[1];
        fs::path result = dir;
        result += L".result";
        std::string error;
        const auto m = bz::setup::LoadManifest(error);
        if (m) {
            Choices c;
            c.dir = dir;
            c.language = "ru";
            c.desktop = c.startMenu = false;
            for (const Component& mod : m->modules) c.modules.push_back(mod.id);
            Install(*m, c, false);
            error = W.work.finished ? std::string() : W.work.error;
        }
        std::ofstream(result) << (error.empty() ? std::string("ok") : error);
        return error.empty() ? 0 : 1;
    }

    // The language: the program's own when it is installed, else the system's.
    std::string lang = LanguageFromSettings();
    if (lang.empty()) lang = SystemLanguage();
    bool known = false;
    for (const LanguageInfo& l : Languages()) known = known || lang == l.code;
    if (!known) lang = "en";
    UseLanguage(lang);
    W.uiLanguage = lang;

    if (has(L"--uninstall")) {
        // Runs from a copy in %TEMP%: the folder (with this very file) is removed.
        const auto found = FindInstalled();
        fs::path dir = found ? found->dir : SelfPath().parent_path();
        if (!has(L"--from-temp")) {
            std::error_code ec;
            const fs::path copy = fs::temp_directory_path(ec) / std::format(L"BreezeCAD-uninstall-{}.exe", GetCurrentProcessId());
            const auto exe = InstallerExe();
            std::ofstream(copy, std::ios::binary).write(reinterpret_cast<const char*>(exe.data()), static_cast<std::streamsize>(exe.size()));
            std::wstring params = L"--uninstall --from-temp --dir \"" + dir.wstring() + L"\"";
            if (has(L"--quiet")) params += L" --quiet";
            ShellExecuteW(nullptr, L"open", copy.c_str(), params.c_str(), nullptr, SW_SHOWNORMAL);
            return 0;
        }
        for (std::size_t i = 0; i + 1 < args.size(); ++i)
            if (args[i] == L"--dir") dir = args[i + 1];
        if (has(L"--quiet")) {
            std::wstring error;
            return Uninstall(dir, true, error) ? 0 : 1;
        }
        W.uninstallDir = dir;
        W.uninstallVersion = found ? found->version : std::string();
        W.screen = Screen::Uninstall;
        return RunWindow(Wide(Tr("un.title")));
    }

    W.existing = FindInstalled();
    W.updating = W.existing.has_value();
    W.pathText = Utf8((W.existing ? W.existing->dir : DefaultDir()).wstring());
    if (const wchar_t* shots = _wgetenv(L"BZ_SETUP_SHOTS"); shots && *shots) {
        PrepareShots(shots);
        return RunWindow(L"BreezeCAD");
    }
    FetchManifest();
    return RunWindow(L"BreezeCAD");
}
