#include "Utf8.h"

#include <cstdint>
#include <vector>

namespace BreezeCAD::Util {

TCollection_ExtendedString Utf8ToExt(const std::string& s) {
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

std::string ExtToUtf8(const TCollection_ExtendedString& es) {
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

} // namespace BreezeCAD::Util
