// SPDX-License-Identifier: MPL-2.0
#include "core/CrashCode.h"

#include <cstdint>
#include <format>

namespace bz::crash {
namespace {

std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

/// One stack line without what changes from run to run: the frame number
/// ("#3"), the absolute address ("[0x7f…]", moved by ASLR) and the folder
/// the program was unpacked to. What stays is the module and the offset or
/// symbol in it: "libTKBRep.so.8(+0x1a2b)", "breezecad.exe+0x1234".
std::string StableFrame(std::string_view line) {
    line = Trim(line);
    if (!line.empty() && line.front() == '#') {   // "#3 breezecad.exe+0x…" (Windows)
        const auto space = line.find(' ');
        line = space == std::string_view::npos ? std::string_view{} : Trim(line.substr(space + 1));
    }
    // glibc writes "lib.so(+0x…)[0x7f…]", with or without a space before "[".
    if (line.ends_with(']')) {
        if (const auto bracket = line.rfind('['); bracket != std::string_view::npos) line = Trim(line.substr(0, bracket));
    }
    const auto paren = line.find('(');
    const std::string_view path = line.substr(0, paren);
    const auto slash = path.find_last_of("/\\");
    std::string out(slash == std::string_view::npos ? path : path.substr(slash + 1));
    if (paren != std::string_view::npos) out += line.substr(paren);
    return out;
}

/// "SEGV" from "SIGSEGV", a name from a Windows exception code, "HANG".
std::string Kind(std::string_view report) {
    if (!ReportField(report, "Hang").empty()) return "HANG";
    const std::string field = ReportField(report, "Crash");
    const std::string_view crash = field;
    if (crash.empty()) return "UNKNOWN";
    if (crash.starts_with("SIG")) {
        const auto end = crash.find_first_of(" \t");
        return std::string(crash.substr(3, end == std::string_view::npos ? std::string_view::npos : end - 3));
    }
    if (crash.starts_with("exception 0x")) {
        const std::string_view code = crash.substr(12, 8);
        if (code == "c0000005") return "ACCESS";
        if (code == "c0000094") return "DIV0";
        if (code == "c00000fd") return "STACK";
        if (code == "c000001d") return "ILLEGAL";
        if (code == "c0000409") return "BUFFER";
        std::string upper(code);
        for (char& c : upper)
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        return "X" + upper;
    }
    if (crash.starts_with("uncaught exception")) return "EXCEPT";
    return "OTHER";
}

/// FNV-1a: small, the same on every system and compiler.
void Mix(std::uint32_t& h, std::string_view text) {
    for (const char c : text) {
        h ^= static_cast<unsigned char>(c);
        h *= 16777619u;
    }
    h ^= '\n';
    h *= 16777619u;
}

} // namespace

std::string ReportField(std::string_view report, std::string_view key) {
    std::size_t at = 0;
    while (at < report.size()) {
        auto end = report.find('\n', at);
        if (end == std::string_view::npos) end = report.size();
        const std::string_view line = report.substr(at, end - at);
        if (line.size() > key.size() && line.starts_with(key) && line[key.size()] == ':')
            return std::string(Trim(line.substr(key.size() + 1)));
        at = end + 1;
    }
    return {};
}

std::string ErrorCode(std::string_view report) {
    const std::string kind = Kind(report);
    std::uint32_t h = 2166136261u;
    Mix(h, kind);
    Mix(h, ReportField(report, "Version"));
    if (kind == "HANG") {
        Mix(h, ReportField(report, "Last action"));
    } else {
        // Every stack line: the place of the failure is what tells crashes apart.
        const auto stack = report.find("\nStack:");
        if (stack != std::string_view::npos) {
            std::size_t at = report.find('\n', stack + 1);
            while (at != std::string_view::npos && at + 1 < report.size()) {
                auto end = report.find('\n', at + 1);
                if (end == std::string_view::npos) end = report.size();
                const std::string frame = StableFrame(report.substr(at + 1, end - at - 1));
                if (!frame.empty()) Mix(h, frame);
                at = end;
            }
        } else {
            Mix(h, ReportField(report, "Crash"));
            Mix(h, ReportField(report, "Reason"));
        }
    }
    return std::format("BZ-{}-{:06X}", kind, h & 0xFFFFFFu);
}

} // namespace bz::crash
