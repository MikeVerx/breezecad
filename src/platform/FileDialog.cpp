#include "platform/FileDialog.h"

#ifdef _WIN32
  #include <windows.h>
  #include <commdlg.h>
  #include <vector>
  #include <string>
#endif

#include <cstring>

namespace OrdoCAD {

#ifdef _WIN32

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], size);
    return out;
}

static std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(),
                                    nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(),
                        &out[0], size, nullptr, nullptr);
    return out;
}

bool ShowFileDialog(bool save, std::string& outPath,
                    const char* title,
                    const char* filter,
                    const char* defaultExt) {
    wchar_t fileName[MAX_PATH] = {0};

    // Если outPath уже задан — предзаполняем
    if (!outPath.empty()) {
        std::wstring initial = Utf8ToWide(outPath);
        wcsncpy(fileName, initial.c_str(), MAX_PATH - 1);
    }

    std::wstring wTitle = title ? Utf8ToWide(title)
                                 : (save ? L"Сохранить как" : L"Открыть файл");
    std::wstring wDefExt = Utf8ToWide(defaultExt ? defaultExt : "");

    // Фильтр: нужно скопировать в буфер, т.к. WinAPI хочет wchar_t*
    std::wstring wFilter;
    if (filter) {
        const char* p = filter;
        while (true) {
            // Каждая строка в фильтре — ANSI, конвертируем
            std::string line(p);
            if (line.empty()) { wFilter.push_back(L'\0'); break; }
            wFilter += Utf8ToWide(line);
            wFilter.push_back(L'\0');
            p += line.size() + 1;
        }
        wFilter.push_back(L'\0');
    } else {
        wFilter = L"OCAF (*.cbf)\0*.cbf\0All (*.*)\0*.*\0\0";
    }

    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = nullptr;
    ofn.lpstrFilter = wFilter.c_str();
    ofn.lpstrFile   = fileName;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = wTitle.c_str();
    ofn.lpstrDefExt = wDefExt.empty() ? nullptr : wDefExt.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (save) ofn.Flags |= OFN_OVERWRITEPROMPT;
    else      ofn.Flags |= OFN_FILEMUSTEXIST;

    BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    if (!ok) return false;

    outPath = WideToUtf8(fileName);
    return true;
}

#else  // не Windows — заглушка

bool ShowFileDialog(bool /*save*/, std::string& /*outPath*/,
                    const char* /*title*/, const char* /*filter*/,
                    const char* /*defaultExt*/) {
    return false;
}

#endif

} // namespace OrdoCAD
